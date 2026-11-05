
// #include <cstdarg>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

// Resets the inline array by just pointing to the first element.
static void resetStack() {
    // In most expressions, an array "automatically" decays into a pointer to its first element.
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
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
    // Regulator: accessing frame->ip led to stale ip being accessed because of caching ip for challenge 24.1. Hence Macro to update ip created.
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

// NATIVE FUNCTIONS:
// Convention: a native writes its result into args[-1] (the callee's stack slot) and returns true. callValue just discards all arguments when control is returned.
// On failure it stores an error message there via nativeError() and returns false, and callValue() raises the runtime error.
// Natives must NOT call runtimeError() themselves: it resets the stack while callValue() is still using it.

// Formats an error message into the callee's slot and returns false, so a native can just `return nativeError(...)`.
static bool nativeError(Value* args, const char* format, ...) {
    char message[256];
    va_list list;
    va_start(list, format);
    int length = vsnprintf(message, sizeof(message), format, list);
    va_end(list);

    if (length < 0) length = 0;
    if (length >= (int)sizeof(message)) length = (int)sizeof(message) - 1;

    args[-1] = OBJ_VAL(copyString(message, length));
    return false;
}

// Native function to return the current clock time.
static bool clockNative(int argCount, Value* args) {
    args[-1] = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
    return true;
}

// Native function to return type based on value.
static bool typeNative(int argCount, Value* args) {
    Value value = args[0];
    const char* name;

    if (IS_NIL(value)) name = "nil";
    else if (IS_BOOL(value)) name = "bool";
    else if (IS_NUMBER(value)) name = "number";
    else {
        switch (OBJ_TYPE(value)) {
            case OBJ_STRING:       name = "string"; break;
            case OBJ_LIST:         name = "list"; break;
            case OBJ_FUNCTION:
            case OBJ_CLOSURE:
            case OBJ_NATIVE:
            case OBJ_BOUND_METHOD: name = "function"; break;
            case OBJ_CLASS:        name = "class"; break;
            case OBJ_INSTANCE:     name = "instance"; break;
            default:               name = "unknown type"; break;
        }
    }

    args[-1] = OBJ_VAL(copyString(name, (int)strlen(name)));
    return true;
}

// Native function to read a line from stdin: input() or input(prompt).
// Returns the line without its trailing newline, or nil at end of input. Lines can be any length.
static bool inputNative(int argCount, Value* args) {
    if (argCount > 1) return nativeError(args, "input() takes 0 or 1 arguments but got %d.", argCount);
    if (argCount == 1) {
        if (!IS_STRING(args[0])) return nativeError(args, "input() prompt must be a string.");
        fputs(AS_CSTRING(args[0]), stdout);
    }
    fflush(stdout); // Make sure the prompt (and any earlier print output) shows before blocking.

    char* buffer = NULL;
    int capacity = 0;
    int length = 0;
    int c;

    // GROW_ARRAY instead of malloc so the GC's byte count stays accurate.
    while ((c = getchar()) != EOF && c != '\n') {
        if (length + 1 >= capacity) {
            int oldCapacity = capacity;
            capacity = GROW_CAPACITY(oldCapacity);
            buffer = GROW_ARRAY(char, buffer, oldCapacity, capacity);
        }
        buffer[length++] = (char)c;
    }

    // EOF with nothing read signals end of input.
    if (c == EOF && length == 0) {
        FREE_ARRAY(char, buffer, capacity);
        args[-1] = NIL_VAL;
        return true;
    }

    // Tolerate Windows line endings.
    if (length > 0 && buffer[length - 1] == '\r') length--;

    // takeString() frees length + 1 bytes if the string is already interned, so shrink to exactly that.
    buffer = GROW_ARRAY(char, buffer, capacity, length + 1);
    buffer[length] = '\0';

    args[-1] = OBJ_VAL(takeString(buffer, length));
    return true;
}

// Native function to parse a number from a string. Returns nil if the string isn't a number. Numbers pass through unchanged.
static bool numNative(int argCount, Value* args) {
    if (IS_NUMBER(args[0])) {
        args[-1] = args[0];
        return true;
    }
    if (!IS_STRING(args[0])) return nativeError(args, "num() expects a string or number.");

    const char* start = AS_CSTRING(args[0]);
    char* end;
    double value = strtod(start, &end);
    if (isnan(value)) value = NAN; // strdod accepts "nan(0x...)" which creates a NaN with the rest of the bits as payload bits, which can potentially produce bits read as other Lox Values.

    if (end == start) { // Nothing parsed.
        args[-1] = NIL_VAL;
        return true;
    }
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0') { // Trailing junk, e.g. "12abc".
        args[-1] = NIL_VAL;
        return true;
    }

    args[-1] = NUMBER_VAL(value);
    return true;
}

// Native function to convert a number, bool, nil or string into a string, mainly so it can be concatenated with +.
static bool strNative(int argCount, Value* args) {
    Value value = args[0];
    if (IS_STRING(value)) {
        args[-1] = value;
        return true;
    }

    char buffer[32];
    int length;
    if (IS_NUMBER(value)) {
        length = snprintf(buffer, sizeof(buffer), "%g", AS_NUMBER(value)); // Same format as printValue().
    } else if (IS_BOOL(value)) {
        length = snprintf(buffer, sizeof(buffer), "%s", AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        length = snprintf(buffer, sizeof(buffer), "nil");
    } else {
        return nativeError(args, "str() can only convert numbers, booleans, nil and strings.");
    }

    args[-1] = OBJ_VAL(copyString(buffer, length));
    return true;
}

// Native function to get the length of a string or a list.
static bool lenNative(int argCount, Value* args) {
    if (IS_STRING(args[0])) {
        args[-1] = NUMBER_VAL(AS_STRING(args[0])->length);
    } else if (IS_LIST(args[0])) {
        args[-1] = NUMBER_VAL(AS_LIST(args[0])->count);
    } else {
        return nativeError(args, "len() expects a string or a list.");
    }
    return true;
}

// Native function to read a whole file into a string.
static bool readFileNative(int argCount, Value* args) {
    if (!IS_STRING(args[0])) return nativeError(args, "readFile() expects a string path.");

    ObjString* path = AS_STRING(args[0]);

    FILE* file = fopen(path->chars, "rb");
    if (file == NULL) return nativeError(args, "Could not open file '%s'.", path->chars);

    fseek(file, 0L, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    if (fileSize < 0) {
        fclose(file);
        return nativeError(args, "Could not determine size of file '%s'.", path->chars);
    }

    char* buffer = ALLOCATE(char, fileSize + 1);

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    fclose(file);

    buffer[bytesRead] = '\0';

    ObjString* result = copyString(buffer, (int)bytesRead);

    FREE_ARRAY(char, buffer, fileSize + 1);
    args[-1] = OBJ_VAL(result);
    return true;
}

// Helper function to check if values are required type and to assign pointers to those values if they are.
static bool getInstanceAndName(ObjInstance** instance, ObjString** name, Value* args) {
    if (!IS_INSTANCE(args[0])) return nativeError(args, "First argument must be an instance.");
    if (!IS_STRING(args[1])) return nativeError(args, "Second argument must be a string.");

    *instance = AS_INSTANCE(args[0]);
    *name = AS_STRING(args[1]);
    return true;
}

// Challenge EDIT 27.1: Adding a native field lookup function, crude version of Python's getattr. User can check if a field exists.
static bool hasFieldNative(int argCount, Value* args) {
    ObjInstance* instance;
    ObjString* name;
    if (!getInstanceAndName(&instance, &name, args)) return false;

    Value value;
    args[-1] = BOOL_VAL(tableGet(&instance->fields, name, &value));
    return true;
}

// Challenge EDIT 27.2: Implementing dynamic property access by following Python's strategy of having native functions.
// Could implement PHP's -> operator for dynamic property access, decided against it because it was disregulating.
static bool setFieldNative(int argCount, Value* args) {
    ObjInstance* instance;
    ObjString* name;
    if (!getInstanceAndName(&instance, &name, args)) return false;

    tableSet(&instance->fields, name, args[2]);
    args[-1] = args[2];
    return true;
}

// Native function to get a field(dynamic lookup allowed) of an instance.
static bool getFieldNative(int argCount, Value* args) {
    ObjInstance* instance;
    ObjString* name;
    if (!getInstanceAndName(&instance, &name, args)) return false;

    Value value;
    if (!tableGet(&instance->fields, name, &value)) {
        return nativeError(args, "Undefined property '%s'.", name->chars);
    }
    args[-1] = value;
    return true;
}

// Challenge EDIT 27.3: Implementation to delete a field(mainly for dynamic lookup) in an instance, following Python's strategy of native functions.
static bool deleteFieldNative(int argCount, Value* args) {
    ObjInstance* instance;
    ObjString* name;
    if (!getInstanceAndName(&instance, &name, args)) return false;

    args[-1] = BOOL_VAL(tableDelete(&instance->fields, name));
    return true;
}

// EDIT: Added native append and delete for lists. Couple of forward declarations.
static void appendToList(ObjList* list, Value value);
static void deleteFromList(ObjList* list, int index);
bool isValidListIndex(ObjList* list, double index);

// Native function to append to a list given a value.
static bool appendToListNative(int argCount, Value* args) {
    if (!IS_LIST(args[0])) return nativeError(args, "First argument must be a list.");

    appendToList(AS_LIST(args[0]), args[1]);
    args[-1] = NIL_VAL;
    return true;
}

// Native function to delete from a list given an index.
static bool deleteFromListNative(int argCount, Value* args) {
    if (!IS_LIST(args[0])) return nativeError(args, "First argument must be a list.");
    if (!IS_NUMBER(args[1])) return nativeError(args, "Second argument must be a number.");

    ObjList* list = AS_LIST(args[0]);
    double index = AS_NUMBER(args[1]);

    if (!isValidListIndex(list, index)) return nativeError(args, "List index out of bounds.");

    deleteFromList(list, (int)index);
    args[-1] = NIL_VAL;
    return true;
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
// Working gray list as well. Self adjusting GC threshold as well.
void initVM() {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.globals);
    initTable(&vm.finalGlobals);
    initTable(&vm.strings);

    // Intern the initString for quick access. Defensively assigning NULL to it so GC does not crash.
    vm.initString = NULL; 
    vm.initString = copyString("init", 4);

    // Explicit arity provided by VM implementer.
    defineNative("clock", clockNative, 0);
    defineNative("type", typeNative, 1);
    defineNative("hasField", hasFieldNative, 2);
    defineNative("setField", setFieldNative, 3);
    defineNative("getField", getFieldNative, 2);
    defineNative("deleteField", deleteFieldNative, 2);
    defineNative("appendToList", appendToListNative, 2);
    defineNative("deleteFromList", deleteFromListNative, 2);
    defineNative("readFile", readFileNative, 1);
    defineNative("input", inputNative, -1); // -1 = variadic, the native checks argCount itself.
    defineNative("num", numNative, 1);
    defineNative("str", strNative, 1);
    defineNative("len", lenNative, 1);
}

// Frees up any remaining objects in memory after program is completed.
// initString is cleared and freeObjects frees the unreachable ObjString.
void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.finalGlobals);
    freeTable(&vm.strings);
    vm.initString = NULL;
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
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                // Replaces the closure on stack slot 0 in the frame window with the receiver instance. The pointer to the closure is still passed to call, creating the CallFrame.
                vm.stackTop[-argCount - 1] = bound->receiver;
                return call(bound->method, argCount);
            }
            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                // Instance replaces the class object on the stack evaluated before the opening paranthesis. Instance doesn't need to be bound to init.
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                Value initializer;
                if (tableGet(&klass->methods, vm.initString, &initializer)) {
                    return call(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                // Invokes the C function and runs it right there. No CallFrames.
                // The native writes its result into the callee's slot (args[-1]), so only the arguments need discarding.
                // On failure that slot holds the error message instead, and the error is raised here, after the native is done with the stack.
                ObjNative* native = AS_NATIVE(callee);
                if (native->arity != -1 && argCount != native->arity) {
                    runtimeError("Expected %d arguments but got %d.", native->arity, argCount);
                    return false;
                }
                Value* args = vm.stackTop - argCount;
                if (!native->function(argCount, args)) {
                    runtimeError("%s", AS_CSTRING(args[-1]));
                    return false;
                }
                vm.stackTop -= argCount;
                return true;
            }
            default:
                break; // Not a function or a class.
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

// Helper function to get the method from the Class and call it. The instance is already on the stack.
static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    return call(AS_CLOSURE(method), argCount);
}

// Helper function to cast the receiver Value to an Instance and invoke the method on it.
static bool invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);

    if (!IS_INSTANCE(receiver)) {
        runtimeError("Only instances have methods.");
        return false;
    }

    ObjInstance* instance = AS_INSTANCE(receiver);

    // First checks if the instance's fields contains a Value of the name. If it does, treated like a normal callable.
    // Regulator: Ordinary closures get their environment from upvalues. Bound methods get it from the receiver they carry.
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

// Helper function to get a method from the class's table, create a bound method with the instance on top of the stack and push the bound method onto the stack.
static bool bindMethod(ObjClass* klass, ObjString* name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

// Traverses through the VM's (sorted) upvalue list, and returns if open upvalue already exists for a captured variable.
// Given a pointer to the captured local variable's slot in it's frame window, creates an upvalue and inserts into sorted list.
static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
    if (upvalue != NULL && upvalue ->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;
    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
    return createdUpvalue;
}

// Hoists open upvalues pointing to the stack into closed upvalues on the heap.
// Takes the value on the stack and stores it in existing upvalue, making the Value on the stack free to be removed.
// location field of the upvalue changes to point to the Value it's storing. Neat!
static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

// Helper function to store the method closure in the class table and pop it.
// No checks because the VM trusts its own compiler.
static void defineMethod(ObjString* name) {
    Value method = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
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
    // Peek so that GC run does not free these objects.
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

// EDIT: Added list runtime functionality.
// Helper function to add to a list.
static void appendToList(ObjList* list, Value value) {
    // Grow the list.
    if (list->capacity < list->count + 1) {
        int oldCapacity = list->capacity;
        list->capacity = GROW_CAPACITY(oldCapacity);
        list->items = GROW_ARRAY(Value, list->items, oldCapacity, list->capacity);
    }
    list->items[list->count] = value;
    list->count++;
}

// Helper function to store a Value to a list given the index.
static void storeToList(ObjList* list, int index, Value value) {
    list->items[index] = value;
}

// Helper function to get a Value from a list given the index.
static Value indexFromList(ObjList* list, int index) {
    return list->items[index];
}

// Helper function to delete from a list and shift all the Values by 1.
static void deleteFromList(ObjList* list, int index) {
    for (int i = index; i < list->count - 1; i++) {
        list->items[i] = list->items[i + 1];
    }
    list->items[list->count - 1] = NIL_VAL;
    list->count--;
}

// Helper function to check if the list index is valid.
// Takes the raw double so it can be checked before converting: casting NaN or an out-of-range double to int is undefined behaviour.
bool isValidListIndex(ObjList* list, double index) {
    if (!(index >= 0 && index < list->count)) return false; // Written this way so NaN fails too.
    return index == (int)index; // Rejects fractional indexes like 1.5. Safe to cast now that it's in range.
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

    // Syncs the cached ip back into the frame before reporting, since runtimeError() reads frame->ip to find the line.
    // Without this, errors raised directly in run() report a stale line (or underflow if no call has happened yet in this frame).
    #define RUNTIME_ERROR(...) \
        do { \
            frame->ip = ip; \
            runtimeError(__VA_ARGS__); \
            return INTERPRET_RUNTIME_ERROR; \
        } while (false)

    // do-while(false) loop is a neat little trick to get around macro's problems. See 15.3.1 for more.
    // Non first-class operator and macro passed as an argument to the macro. The regulator is that macro is just text substitution.
    #define BINARY_OP(valueType, op) \
        do { \
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
                RUNTIME_ERROR("Operands must be numbers."); \
            } \
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(valueType(a op b)); \
        } while(false)
    
    #define BINARY_SPECIAL_OP(valueType, op) \
        do { \
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
                RUNTIME_ERROR("Operands must be numbers."); \
            } \
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(valueType(op(a, b))); \
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
                // Notice the value is still on the stack.
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();

                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
                }
                push(value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();

                // Challenge EDIT: Added support for handling declaration of single assignment global variables.
                if (isFinalGlobal(name)) {
                    RUNTIME_ERROR("Cannot assign to final variable '%s'.", name->chars);
                }

                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
                }
                // Does not pop because assignment is an expression.
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(peek(0))) {
                    RUNTIME_ERROR("Only instances have properties.");
                }
                ObjInstance* instance = AS_INSTANCE(peek(0));
                ObjString* name = READ_STRING();

                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop(); // Pop the instance.
                    push(value);
                    break;
                }

                frame->ip = ip; // bindMethod() may report an error.
                if (!bindMethod(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(1))) {
                    RUNTIME_ERROR("Only instances have fields.");
                }
                ObjInstance* instance = AS_INSTANCE(peek(1));
                // No GC worry here since instance and value is already on the stack.
                tableSet(&instance->fields, READ_STRING(), peek(0));
                Value value = pop();
                pop(); 
                push(value); // Set expression leaves the value on the stack.
                break;
            }
            case OP_GET_SUPER: {
                ObjString* name = READ_STRING();
                ObjClass* superclass = AS_CLASS(pop());

                // Gets the superclass and binds the current instance to the method.
                frame->ip = ip; // bindMethod() may report an error.
                if (!bindMethod(superclass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

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
                    RUNTIME_ERROR("Cannot redefine final variable '%s'.", name->chars);
                }

                tableSet(&vm.globals, name, peek(0));
                pop(); // pop AFTER adding to hash table to allow for garbage collection anomalies while hash table resizing. If not, pointer to the might be present on the C stack but hash table resizing can cause GC trigger, which will free the Value. 
                break;
            }
            // Challenge EDIT: Added support for handling declaration of single assignment global variables.
            case OP_DEFINE_FINAL_GLOBAL: {
                ObjString* name = READ_STRING();

                if (isFinalGlobal(name)) {
                    RUNTIME_ERROR("Cannot redeclare final variable '%s'.", name->chars);
                }

                tableSet(&vm.globals, name, peek(0));
                tableSet(&vm.finalGlobals, name, BOOL_VAL(true));
                pop();
                break;

            }
            case OP_NEGATE: 
                if (!IS_NUMBER(peek(0))) {
                    RUNTIME_ERROR("Operand must be a number.");
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
                    RUNTIME_ERROR("Operands must be two numbers or two strings");
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_MOD: BINARY_SPECIAL_OP(NUMBER_VAL, fmod); break;
            case OP_EXP: BINARY_SPECIAL_OP(NUMBER_VAL, pow); break;
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
            case OP_LIST: {
                // EDIT: Added OpCode for list creation.
                ObjList* list = newList();
                uint8_t itemCount = READ_BYTE();

                push(OBJ_VAL(list)); // Ensures GC trigger from appendToList does not free this.
                for (int i = itemCount; i > 0; i--) {
                    appendToList(list,  peek(i));
                }
                pop();

                while (itemCount-- > 0) {
                    pop();
                }

                push(OBJ_VAL(list));
                break;
            }
            case OP_GET_INDEX: {
                // EDIT: Added OpCode for getting Value based on list index.
                Value index_ = pop();
                Value list_ = pop();

                // Check if the list is valid, if the index is valid, and if the range is valid.
                if (!IS_LIST(list_)) {
                    RUNTIME_ERROR("Only lists can be indexed.");
                }
                ObjList* list = AS_LIST(list_);

                if (!IS_NUMBER(index_)) {
                    RUNTIME_ERROR("List index can only be a number.");
                }
                double index = AS_NUMBER(index_);

                if (!isValidListIndex(list, index)) {
                    RUNTIME_ERROR("List index out of range.");
                }

                Value result = indexFromList(list, (int)index);
                push(result);
                break;
            }
            case OP_SET_INDEX: {
                // EDIT: Added OpCode for setting Value based on list index.
                Value item_ = pop();
                Value index_ = pop();
                Value list_ = pop();

                if (!IS_LIST(list_)) {
                    RUNTIME_ERROR("Cannot store value in a non-list.");
                }
                ObjList* list = AS_LIST(list_);

                if (!IS_NUMBER(index_)) {
                    RUNTIME_ERROR("List index is not a number.");
                }
                double index = AS_NUMBER(index_);

                if (!isValidListIndex(list, index)) {
                    RUNTIME_ERROR("Invalid list index.");
                }

                storeToList(list, (int)index, item_);
                push(item_);
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
            case OP_INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();

                // Challenge EDIT: 24.1
                frame->ip = ip;

                if (!invoke(method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                frame = &vm.frames[vm.frameCount - 1];
                // Challenge EDIT: 24.1
                ip = frame->ip;

                break;
            }
            case OP_SUPER_INVOKE: {
                // Order gets shuffled, since the arguments must already be present on the stack. The superclass is above the arguments.
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                ObjClass* superclass = AS_CLASS(pop());
                
                // Challenge EDIT: 24.1
                frame->ip = ip;

                // Does not invoke fields.
                if (!invokeFromClass(superclass, method, argCount)) {
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
                // Reads the bytecode and creates upvalues.
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            case OP_RETURN: {
                Value result = pop();
                // Closes upvalues at the end of a function. This is because compiler does NOT emit instructions for popping or closing upvalues at the end of a function block.
                closeUpvalues(frame->slots);
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
            case OP_CLASS:  
                push(OBJ_VAL(newClass(READ_STRING())));
                break;
            case OP_INHERIT: {
                // Unlike jlox, subclass does not store a reference to superclass.
                // This means that monkey patching is not allowed in clox. Not allowed in jlox as well, as I found out.
                Value superclass = peek(1);
                if (!IS_CLASS(superclass)) {
                    RUNTIME_ERROR("Superclass must be a class.");
                }

                ObjClass* subclass = AS_CLASS(peek(0));
                tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
                pop(); // Subclass.
                break;
            }
            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
        }
    }
    #undef READ_BYTE
    #undef READ_SHORT
    #undef READ_CONSTANT
    #undef READ_STRING
    #undef BINARY_OP
    #undef BINARY_SPECIAL_OP
    #undef RUNTIME_ERROR
}

// Interpretes the code and returns a result value.
// EDIT: Compiler returns a function which contains a chunk, VM no longer provides a chunk.
InterpretResult interpret(const char* source) {
    // Gets the script function.
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPET_COMPILE_ERROR;

    // Creates a first class Clsoure value for the script function and manually pushes it to the base of the stack.
    // Function is pushed first to handle GC.
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    // Creates the call frame for script function.
    call(closure, 0);

    return run();
}