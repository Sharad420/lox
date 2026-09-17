// #include <cstdarg>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "line.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

// Note that we are using a static variable. This is a pedagogical choice for the sake of simplicity. Ideally a VM pointer is needed in case multiple VMs are required.
VM vm;

// Native functions.
static Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

// Resets the inline array by just pointing to the first element.
static void resetStack() {
    // In most expressions, an array "automatically" decays into a pointer to its first element.
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
}

// Alerts the user about a runtime error and provides the line number. This is a variadic function, like printf. Read more if interested.
// Challenge EDIT: Gets the line number from the line run array.
// EDIT: Now gets the topmost call frame and reads its chunk and ip.
static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // Prints the stack trace.
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", getLine(&function->chunk.lines, instruction));
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    resetStack();
}

// Foreign function interface to allow users to define native functions.
// Given the name and the function pointer, Makes an ObjString and a Value and defines them in globals.
static void defineNative(const char* name, NativeFn function, int arity) {
    // Push and pop to avoid GC trigger.
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function, arity)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

// Initializes the VM and any native functions.
void initVM() {
    resetStack();
    vm.objects = NULL;
    initTable(&vm.globals);
    initTable(&vm.finalGlobals);
    initTable(&vm.strings);

    // Explicit arity provided by VM implementer.
    defineNative("clock", clockNative, 0);
}

// Frees up any remaining objects in memory after program is completed.
void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.finalGlobals);
    freeTable(&vm.strings);
    freeObjects();
}

// Dereferences the stack top slot and pushes a value.
void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

// Moves stack pointer back to the most recent used slot and returns the value.
// This slot is no longer in use according to the logic invariant of the stack.
Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

// Returns a value in the stack without popping from the top.
static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

// Creates a call frame and initializes it's window on the stack for the verified function.
// Literally no work needed to calculate the global offset for the local variables of the function!
static bool call(ObjClosure* closure, int argCount) {
    // Dynamic checking of arity.
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1; // -1 accounts for the function object stored at stack slot 0.
    return true;
}

// Checks if the callee(Value) is a function or a class dynamically, and then creates a CallFrame for the callee.
static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                // Invokes the C function and runs it right there, clears the stack and pushes the result. No CallFrames.
                ObjNative* native = AS_NATIVE(callee);
                if (argCount != native->arity) {
                    runtimeError("Expected %d arguments but got %d.", native->arity, argCount);
                    return false;
                }
                NativeFn function = native->function;
                Value result = function(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break; // Not a function or a class.
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

// Handles the truthyness/falseyness that Lox provides, negates it and returns a boolean.
// Only nil and false are falsey, everything else is truthy.
static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static bool isFinalGlobal(ObjString* name) {
    Value ignore;
    return tableGet(&vm.finalGlobals, name, &ignore);
}

// Concatenates two strings into a new string object and pushes it onto the stack.
static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}

// The heart of the interpreter. Handles instruction pointed to and increments until the end of the loop.
// Reads the current CallFrame's chunk and accesses it's variables.
// EDIT: Changed run() to now work with the current call frame i.e the function being called(including script function).
// Challenge EDIT 24.1: Making ip a local variable and giving a memory access specifier hint to C for potentially faster dispatch. Reduces previous pointer indirection which may have led to C compiler accessing ip from memory via frame.
// Changed all frame->ip references to ip, added synchronization at frame beginning & end boundaries.
static InterpretResult run() {
    // Storing in local variable for succicntness and to take advantage of C storing local variables in a register.
    CallFrame* frame = &vm.frames[vm.frameCount - 1];


    register uint8_t* ip = frame->ip;

    #define READ_BYTE() (*ip++)

    // Accesses the current call frame's variables from the constant pool.
    #define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

    // C's comma operator, evaluates the left expression, discards its value and returns the right expression next.
    #define READ_SHORT() \
    (ip += 2, (uint16_t)((ip[-2] << 8 | ip[-1])))
    
    #define READ_STRING() AS_STRING(READ_CONSTANT())

    // do-while(false) loop is a neat little trick to get around macro's problems. See 15.3.1 for more.
    // Non first-class operator and macro passed as an argument to the macro. The regulator is that macro is just text substitution.
    #define BINARY_OP(valueType, op) \
        do { \
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
                runtimeError("Operands must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(valueType(a op b)); \
        } while(false)

    for (;;) {
        // If debug flag is enabled, dynamically disassemble instruction.
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");

        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");

        disassembleInstruction(&frame->closure->function->chunk, 
            (int)(ip - frame->closure->function->chunk.code));
#endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT : {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP: pop(); break;
            case OP_POPN: {
                // EDIT: instruction to handle multiple pops together.
                int numPops = READ_BYTE();
                while (numPops-- > 0) {
                    pop();
                }
                break;
            }
            case OP_GET_LOCAL: {
                // Reads the relative offset of a variable.
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();

                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();

                // Challenge EDIT: Added support for handling declaration of single assignment global variables.
                if (isFinalGlobal(name)) {
                    runtimeError("Cannot assign to final variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                // Does not pop because assignment is an expression.
                break;
            }
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();

                // Challenge EDIT: Added support for single assignment global variables.
                if (isFinalGlobal(name)) {
                    runtimeError("Cannot redefine final variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                tableSet(&vm.globals, name, peek(0));
                pop(); // pop AFTER adding to hash table to allow for garbage collection anomalies while hash table resizing.
                break;
            }
            // Challenge EDIT: Added support for handling declaration of single assignment global variables.
            case OP_DEFINE_FINAL_GLOBAL: {
                ObjString* name = READ_STRING();

                if (isFinalGlobal(name)) {
                    runtimeError("Cannot redeclare final variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                tableSet(&vm.globals, name, peek(0));
                tableSet(&vm.finalGlobals, name, BOOL_VAL(true));
                pop();
                break;

            }
            case OP_NEGATE: 
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-(AS_NUMBER(pop()))));
                break;
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError("Operands must be two numbers or two strings");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                ip -= offset;
                break;
            }
            case OP_CALL:  {
                int argCount = READ_BYTE();

                // Challenge EDIT: 24.1
                frame->ip = ip; // Save caller's current ip.

                // Gets the function object by peeking beyond the arguments.
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];

                // Challenge EDIT: 24.1 
                ip = frame->ip;

                break;
            }
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));
                break;
            }
            case OP_RETURN: {
                Value result = pop();
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    // Pop the main script function and exit.
                    pop();
                    return INTERPRET_OK;
                }

                // Discard the frame and return to the caller function's call frame.
                vm.stackTop = frame->slots;
                push(result);
                frame = &vm.frames[vm.frameCount - 1];

                // Challenge EDIT: 24.1
                ip = frame->ip; // Restore the callee's current ip.

                break;
            }
        }
    }
    #undef READ_BYTE
    #undef READ_SHORT
    #undef READ_CONSTANT
    #undef READ_STRING
    #undef BINARY_OP
}

// Interpretes the code and returns a result value.
// EDIT: Compiler returns a function which contains a chunk, VM no longer provides a chunk.
InterpretResult interpret(const char* source) {
    // Gets the script function.
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPET_COMPILE_ERROR;

    // Creates a first class Clsoure value for the script function and manually pushes it to the base of the stack.
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    // Creates the call frame for script function.
    call(closure, 0);

    return run();
}
