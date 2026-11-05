// // #include <cstdarg>
// #include <stdarg.h>
// #include <stdbool.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <string.h>
// #include <time.h>

// #include "chunk.h"
// #include "common.h"
// #include "compiler.h"
// #include "debug.h"
// #include "line.h"
// #include "memory.h"
// #include "object.h"
// #include "table.h"
// #include "value.h"
// #include "vm.h"

// // Note that we are using a static variable. This is a pedagogical choice for the sake of simplicity. Ideally a VM pointer is needed in case multiple VMs are required.
// VM vm;

// // Resets the inline array by just pointing to the first element.
// static void resetStack() {
//     // In most expressions, an array "automatically" decays into a pointer to its first element.
//     vm.stackTop = vm.stack;
//     vm.frameCount = 0;
//     vm.openUpvalues = NULL;
// }

// // Alerts the user about a runtime error and provides the line number. This is a variadic function, like printf. Read more if interested.
// // Challenge EDIT: Gets the line number from the line run array.
// // EDIT: Now gets the topmost call frame and reads its chunk and ip.
// static void runtimeError(const char* format, ...) {
//     va_list args;
//     va_start(args, format);
//     vfprintf(stderr, format, args);
//     va_end(args);
//     fputs("\n", stderr);

//     // Prints the stack trace. 
//     for (int i = vm.frameCount - 1; i >= 0; i--) {
//         CallFrame* frame = &vm.frames[i];
//         ObjFunction* function = frame->closure->function;
//         size_t instruction = frame->ip - function->chunk.code - 1;
//         fprintf(stderr, "[line %d] in ", getLine(&function->chunk.lines, instruction));
//         if (function->name == NULL) {
//             fprintf(stderr, "script\n");
//         } else {
//             fprintf(stderr, "%s()\n", function->name->chars);
//         }
//     }
//     resetStack();
// }

// // NATIVE FUNCTIONS:

// // Native function to return the current clock time.
// static Value clockNative(int argCount, Value* args) {
//     return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
// }

// // Native function to return type based on value. 
// static Value typeNative(int argCount, Value* args) {
//     Value value = args[0];

//     if (IS_NIL(value)) return OBJ_VAL(copyString("nil", 3));
//     if (IS_BOOL(value)) return OBJ_VAL(copyString("bool", 4));
//     if (IS_NUMBER(value)) return OBJ_VAL(copyString("number", 6));

//     ObjType type = OBJ_TYPE(value);

//     switch (type) {
//         case OBJ_STRING:
//             return OBJ_VAL(copyString("string", 6));
//         case OBJ_LIST:
//             return OBJ_VAL(copyString("list", 4));
//         case OBJ_FUNCTION:
//         case OBJ_CLOSURE:
//         case OBJ_NATIVE:
//             return OBJ_VAL(copyString("function", 8));
//         case OBJ_CLASS:
//             return OBJ_VAL(copyString("class", 5));
//         case OBJ_INSTANCE:
//             return OBJ_VAL(copyString("instance", 8));
//         default:
//             return OBJ_VAL(copyString("unknown type", 12));
//     }
// }

// // TODO: Add input scanning.
// // Native function to add file reading.
// static Value readFileNative(int argCount, Value* args) {
//     if (argCount != 1 || !IS_STRING(args[0])) {
//         runtimeError("readFile() expects exactly one string argument.");
//         return NIL_VAL;
//     }

//     ObjString* path = AS_STRING(args[0]);

//     FILE* file = fopen(path->chars, "rb");

//     if (file == NULL) {
//         runtimeError("Could not open file '%s'.", path->chars);
//         return NIL_VAL;
//     }

//     fseek(file, 0L, SEEK_END);
//     long fileSize = ftell(file);
//     rewind(file);

//     if (fileSize < 0) {
//         fclose(file);
//         runtimeError("Could not determine file size.");
//         return NIL_VAL;
//     }

//     char* buffer = ALLOCATE(char, fileSize + 1);

//     size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
//     fclose(file);

//     buffer[bytesRead] = '\0';

//     ObjString* result = copyString(buffer, (int)bytesRead);

//     FREE_ARRAY(char, buffer, fileSize + 1);
//     return OBJ_VAL(result);
// }

// // Helper function to check if values are required type and to assign pointers to those values if they are.
// static bool getInstanceAndName(ObjInstance** instance, ObjString** name, Value* args) {
//     if (!IS_INSTANCE(args[0])) {
//         runtimeError("First argument must be an instance.");
//         return false;
//     }
//     *instance = AS_INSTANCE(args[0]);

//     if (!IS_STRING(args[1])) {
//         runtimeError("Second argument must be a string.");
//         return false;
//     }
//     *name = AS_STRING(args[1]);

//     return true;
// }

// // EDIT: Changed location to after runtimeError is declared to handle errors.

// // Challenge EDIT 27.1: Adding a native field lookup function, crude version of Python's getattr. User can check if a field exists.
// static Value hasFieldNative(int argCount, Value* args) {
//     ObjInstance* instance;
//     ObjString* name;
//     if(!getInstanceAndName(&instance, &name, args)) {
//         return NIL_VAL;
//     }

//     Value value;
//     return BOOL_VAL(tableGet(&instance->fields, name, &value));
// }

// // Challenge EDIT 27.2: Implementing dynamic property access by following Python's strategy of having native functions. 
// // Could implement PHP's -> operator for dynamic property access, decided against it because it was disregulating.
// static Value setFieldNative(int argCount, Value* args) {
//     ObjInstance* instance;
//     ObjString* name;
//     if(!getInstanceAndName(&instance, &name, args)) {
//         return NIL_VAL;
//     }

//     tableSet(&instance->fields, name, args[2]);
//     return args[2];
// }

// // Native function to get a field(dynamic lookup allowed) of an instance.
// static Value getFieldNative(int argCount, Value* args) {
//     ObjInstance* instance;
//     ObjString* name;
//     if(!getInstanceAndName(&instance, &name, args)) {
//         return NIL_VAL;
//     }

//     Value value;
//     if (!tableGet(&instance->fields, name, &value)) {
//         runtimeError("Undefined property %s", name->chars);
//         return NIL_VAL; 
//     }
//     return value;
// }

// // Challenge EDIT 27.3: Implementation to delete a field(mainly for dynamic lookup) in an instance, following Python's strategy of native functions.
// static Value deleteFieldNative(int argCount, Value* args) {
//     ObjInstance* instance;
//     ObjString* name;
//     if(!getInstanceAndName(&instance, &name, args)) {
//         return NIL_VAL;
//     }

//     return BOOL_VAL(tableDelete(&instance->fields, name));
// }

// // EDIT: Added native append and delete for lists. Couple of forward declarations.
// static void appendToList(ObjList* list, Value value);
// static void deleteFromList(ObjList* list, int index);
// static bool isValidListIndex(ObjList* list, int index);

// // Native function to append to a list given a value.
// static Value appendToListNative(int argCount, Value* args) {
//     if (!IS_LIST(args[0])) {
//         runtimeError("First argument must be a list.");
//         return NIL_VAL;
//     }

//     ObjList* list = AS_LIST(args[0]);
//     Value item = args[1];

//     appendToList(list, item);
//     return NIL_VAL;
// }

// // Native function to delete from a list given an index.
// static Value deleteFromListNative(int argCount, Value* args) {
//     if (!IS_LIST(args[0])) {
//         runtimeError("First argument must be a list.");
//         return NIL_VAL;
//     }

//     if (!IS_NUMBER(args[1])) {
//         runtimeError("Second argument must be a number.");
//         return NIL_VAL;
//     }

//     ObjList* list = AS_LIST(args[0]);
//     int index = AS_NUMBER(args[1]);

//     if (!isValidListIndex(list, index)) {
//         runtimeError("List index out of bounds.");
//         return NIL_VAL;
//     }

//     deleteFromList(list, index);
//     return NIL_VAL;
// }

// // Foreign function interface to allow users to define native functions.
// // Given the name and the function pointer, Makes an ObjString and a Value and defines them in globals.
// static void defineNative(const char* name, NativeFn function, int arity) {
//     // Push and pop to avoid GC trigger.
//     push(OBJ_VAL(copyString(name, (int)strlen(name))));
//     push(OBJ_VAL(newNative(function, arity)));
//     tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
//     pop();
//     pop();
// }

// // Initializes the VM and any native functions.
// // Working gray list as well. Self adjusting GC threshold as well.
// void initVM() {
//     resetStack();
//     vm.objects = NULL;
//     vm.bytesAllocated = 0;
//     vm.nextGC = 1024 * 1024;

//     vm.grayCount = 0;
//     vm.grayCapacity = 0;
//     vm.grayStack = NULL;

//     initTable(&vm.globals);
//     initTable(&vm.finalGlobals);
//     initTable(&vm.strings);

//     // Intern the initString for quick access. Defensively assigning NULL to it so GC does not crash.
//     vm.initString = NULL; 
//     vm.initString = copyString("init", 4);

//     // Explicit arity provided by VM implementer.
//     defineNative("clock", clockNative, 0);
//     defineNative("type", typeNative, 1);
//     defineNative("hasField", hasFieldNative, 2);
//     defineNative("setField", setFieldNative, 3);
//     defineNative("getField", getFieldNative, 2);
//     defineNative("deleteField", deleteFieldNative, 2);
//     defineNative("appendToList", appendToListNative, 2);
//     defineNative("deleteFromList", deleteFromListNative, 2);
// }

// // Frees up any remaining objects in memory after program is completed.
// // initString is cleared and freeObjects frees the unreachable ObjString.
// void freeVM() {
//     freeTable(&vm.globals);
//     freeTable(&vm.finalGlobals);
//     freeTable(&vm.strings);
//     vm.initString = NULL;
//     freeObjects();
// }

// // Dereferences the stack top slot and pushes a value.
// void push(Value value) {
//     *vm.stackTop = value;
//     vm.stackTop++;
// }

// // Moves stack pointer back to the most recent used slot and returns the value.
// // This slot is no longer in use according to the logic invariant of the stack.
// Value pop() {
//     vm.stackTop--;
//     return *vm.stackTop;
// }

// // Returns a value in the stack without popping from the top.
// static Value peek(int distance) {
//     return vm.stackTop[-1 - distance];
// }

// // Creates a call frame and initializes it's window on the stack for the verified function.
// // Literally no work needed to calculate the global offset for the local variables of the function!
// static bool call(ObjClosure* closure, int argCount) {
//     // Dynamic checking of arity.
//     if (argCount != closure->function->arity) {
//         runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
//         return false;
//     }

//     if (vm.frameCount == FRAMES_MAX) {
//         runtimeError("Stack overflow.");
//         return false;
//     }

//     CallFrame* frame = &vm.frames[vm.frameCount++];
//     frame->closure = closure;
//     frame->ip = closure->function->chunk.code;
//     frame->slots = vm.stackTop - argCount - 1; // -1 accounts for the function object stored at stack slot 0.
//     return true;
// }

// // Checks if the callee(Value) is a function or a class dynamically, and then creates a CallFrame for the callee.
// static bool callValue(Value callee, int argCount) {
//     if (IS_OBJ(callee)) {
//         switch (OBJ_TYPE(callee)) {
//             case OBJ_BOUND_METHOD: {
//                 ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
//                 // Replaces the closure on stack slot 0 in the frame window with the receiver instance. The pointer to the closure is still passed to call, creating the CallFrame.
//                 vm.stackTop[-argCount - 1] = bound->receiver;
//                 return call(bound->method, argCount);
//             }
//             case OBJ_CLASS: {
//                 ObjClass* klass = AS_CLASS(callee);
//                 // Instance replaces the class object on the stack evaluated before the opening paranthesis. Instance doesn't need to be bound to init.
//                 vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
//                 Value initializer;
//                 if (tableGet(&klass->methods, vm.initString, &initializer)) {
//                     return call(AS_CLOSURE(initializer), argCount);
//                 } else if (argCount != 0) {
//                     runtimeError("Expected 0 arguments but got %d.", argCount);
//                     return false;
//                 }
//                 return true;
//             }
//             case OBJ_CLOSURE:
//                 return call(AS_CLOSURE(callee), argCount);
//             case OBJ_NATIVE: {
//                 // Invokes the C function and runs it right there, clears the stack and pushes the result. No CallFrames.
//                 ObjNative* native = AS_NATIVE(callee);
//                 if (argCount != native->arity) {
//                     runtimeError("Expected %d arguments but got %d.", native->arity, argCount);
//                     return false;
//                 }
//                 NativeFn function = native->function;
//                 Value result = function(argCount, vm.stackTop - argCount);
//                 vm.stackTop -= argCount + 1;
//                 push(result);
//                 return true;
//             }
//             default:
//                 break; // Not a function or a class.
//         }
//     }
//     runtimeError("Can only call functions and classes.");
//     return false;
// }

// // Helper function to get the method from the Class and call it. The instance is already on the stack.
// static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
//     Value method;
//     if (!tableGet(&klass->methods, name, &method)) {
//         runtimeError("Undefined property '%s'.", name->chars);
//         return false;
//     }

//     return call(AS_CLOSURE(method), argCount);
// }

// // Helper function to cast the receiver Value to an Instance and invoke the method on it.
// static bool invoke(ObjString* name, int argCount) {
//     Value receiver = peek(argCount);

//     if (!IS_INSTANCE(receiver)) {
//         runtimeError("Only instances have methods.");
//         return false;
//     }

//     ObjInstance* instance = AS_INSTANCE(receiver);

//     // First checks if the instance's fields contains a Value of the name. If it does, treated like a normal callable.
//     // Regulator: Ordinary closures get their environment from upvalues. Bound methods get it from the receiver they carry.
//     Value value;
//     if (tableGet(&instance->fields, name, &value)) {
//         vm.stackTop[-argCount - 1] = value;
//         return callValue(value, argCount);
//     }

//     return invokeFromClass(instance->klass, name, argCount);
// }

// // Helper function to get a method from the class's table, create a bound method with the instance on top of the stack and push the bound method onto the stack.
// static bool bindMethod(ObjClass* klass, ObjString* name) {
//     Value method;
//     if (!tableGet(&klass->methods, name, &method)) {
//         runtimeError("Undefined property '%s'.", name->chars);
//         return false;
//     }

//     ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
//     pop();
//     push(OBJ_VAL(bound));
//     return true;
// }

// // Traverses through the VM's (sorted) upvalue list, and returns if open upvalue already exists for a captured variable.
// // Given a pointer to the captured local variable's slot in it's frame window, creates an upvalue and inserts into sorted list.
// static ObjUpvalue* captureUpvalue(Value* local) {
//     ObjUpvalue* prevUpvalue = NULL;
//     ObjUpvalue* upvalue = vm.openUpvalues;
//     while (upvalue != NULL && upvalue->location > local) {
//         prevUpvalue = upvalue;
//         upvalue = upvalue->next;
//     }
//     if (upvalue != NULL && upvalue ->location == local) {
//         return upvalue;
//     }

//     ObjUpvalue* createdUpvalue = newUpvalue(local);
//     createdUpvalue->next = upvalue;
//     if (prevUpvalue == NULL) {
//         vm.openUpvalues = createdUpvalue;
//     } else {
//         prevUpvalue->next = createdUpvalue;
//     }
//     return createdUpvalue;
// }

// // Hoists open upvalues pointing to the stack into closed upvalues on the heap.
// // Takes the value on the stack and stores it in existing upvalue, making the Value on the stack free to be removed.
// // location field of the upvalue changes to point to the Value it's storing. Neat!
// static void closeUpvalues(Value* last) {
//     while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
//         ObjUpvalue* upvalue = vm.openUpvalues;
//         upvalue->closed = *upvalue->location;
//         upvalue->location = &upvalue->closed;
//         vm.openUpvalues = upvalue->next;
//     }
// }

// // Helper function to store the method closure in the class table and pop it.
// // No checks because the VM trusts its own compiler.
// static void defineMethod(ObjString* name) {
//     Value method = peek(0);
//     ObjClass* klass = AS_CLASS(peek(1));
//     tableSet(&klass->methods, name, method);
//     pop();
// }

// // Handles the truthyness/falseyness that Lox provides, negates it and returns a boolean.
// // Only nil and false are falsey, everything else is truthy.
// static bool isFalsey(Value value) {
//     return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
// }

// static bool isFinalGlobal(ObjString* name) {
//     Value ignore;
//     return tableGet(&vm.finalGlobals, name, &ignore);
// }

// // Concatenates two strings into a new string object and pushes it onto the stack.
// static void concatenate() {
//     // Peek so that GC run does not free these objects.
//     ObjString* b = AS_STRING(peek(0));
//     ObjString* a = AS_STRING(peek(1));

//     int length = a->length + b->length;
//     char* chars = ALLOCATE(char, length + 1);
//     memcpy(chars, a->chars, a->length);
//     memcpy(chars + a->length, b->chars, b->length);
//     chars[length] = '\0';

//     ObjString* result = takeString(chars, length);
//     pop();
//     pop();
//     push(OBJ_VAL(result));
// }

// // EDIT: Added list runtime functionality.
// // Helper function to add to a list.
// static void appendToList(ObjList* list, Value value) {
//     // Grow the list.
//     if (list->capacity < list->count + 1) {
//         int oldCapacity = list->capacity;
//         list->capacity = GROW_CAPACITY(oldCapacity);
//         list->items = GROW_ARRAY(Value, list->items, oldCapacity, list->capacity);
//     }
//     list->items[list->count] = value;
//     list->count++;
// }

// // Helper function to store a Value to a list given the index.
// static void storeToList(ObjList* list, int index, Value value) {
//     list->items[index] = value;
// }

// // Helper function to get a Value from a list given the index.
// static Value indexFromList(ObjList* list, int index) {
//     return list->items[index];
// }

// // Helper function to delete from a list and shift all the Values by 1.
// static void deleteFromList(ObjList* list, int index) {
//     for (int i = index; i < list->count - 1; i++) {
//         list->items[i] = list->items[i + 1];
//     }
//     list->items[list->count - 1] = NIL_VAL;
//     list->count--;
// }

// // Helper function to check if the list index is valid.
// static bool isValidListIndex(ObjList* list, int index) {
//     if (index < 0 || index > list->count - 1) {
//         return false;
//     }
//     return true;
// }

// // The heart of the interpreter. Handles instruction pointed to and increments until the end of the loop.
// // Reads the current CallFrame's chunk and accesses it's variables.
// // EDIT: Changed run() to now work with the current call frame i.e the function being called(including script function).
// // Challenge EDIT 24.1: Making ip a local variable and giving a memory access specifier hint to C for potentially faster dispatch. Reduces previous pointer indirection which may have led to C compiler accessing ip from memory via frame.
// // Changed all frame->ip references to ip, added synchronization at frame beginning & end boundaries.
// static InterpretResult run() {
//     // Storing in local variable for succicntness and to take advantage of C storing local variables in a register.
//     CallFrame* frame = &vm.frames[vm.frameCount - 1];


//     register uint8_t* ip = frame->ip;

//     #define READ_BYTE() (*ip++)

//     // Accesses the current call frame's variables from the constant pool.
//     #define READ_CONSTANT() \
//     (frame->closure->function->chunk.constants.values[READ_BYTE()])

//     // C's comma operator, evaluates the left expression, discards its value and returns the right expression next.
//     #define READ_SHORT() \
//     (ip += 2, (uint16_t)((ip[-2] << 8 | ip[-1])))
    
//     #define READ_STRING() AS_STRING(READ_CONSTANT())

//     // do-while(false) loop is a neat little trick to get around macro's problems. See 15.3.1 for more.
//     // Non first-class operator and macro passed as an argument to the macro. The regulator is that macro is just text substitution.
//     #define BINARY_OP(valueType, op) \
//         do { \
//             if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
//                 runtimeError("Operands must be numbers."); \
//                 return INTERPRET_RUNTIME_ERROR; \
//             } \
//             double b = AS_NUMBER(pop()); \
//             double a = AS_NUMBER(pop()); \
//             push(valueType(a op b)); \
//         } while(false)

//     for (;;) {
//         // If debug flag is enabled, dynamically disassemble instruction.
// #ifdef DEBUG_TRACE_EXECUTION
//         printf("        ");

//         for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
//             printf("[ ");
//             printValue(*slot);
//             printf(" ]");
//         }
//         printf("\n");

//         disassembleInstruction(&frame->closure->function->chunk, 
//             (int)(ip - frame->closure->function->chunk.code));
// #endif
//         uint8_t instruction;
//         switch (instruction = READ_BYTE()) {
//             case OP_CONSTANT : {
//                 Value constant = READ_CONSTANT();
//                 push(constant);
//                 break;
//             }
//             case OP_NIL: push(NIL_VAL); break;
//             case OP_TRUE: push(BOOL_VAL(true)); break;
//             case OP_FALSE: push(BOOL_VAL(false)); break;
//             case OP_POP: pop(); break;
//             case OP_POPN: {
//                 // EDIT: instruction to handle multiple pops together.
//                 int numPops = READ_BYTE();
//                 while (numPops-- > 0) {
//                     pop();
//                 }
//                 break;
//             }
//             case OP_GET_LOCAL: {
//                 // Reads the relative offset of a variable.
//                 uint8_t slot = READ_BYTE();
//                 push(frame->slots[slot]);
//                 break;
//             }
//             case OP_SET_LOCAL: {
//                 uint8_t slot = READ_BYTE();
//                 // Notice the value is still on the stack.
//                 frame->slots[slot] = peek(0);
//                 break;
//             }
//             case OP_GET_GLOBAL: {
//                 ObjString* name = READ_STRING();

//                 Value value;
//                 if (!tableGet(&vm.globals, name, &value)) {
//                     runtimeError("Undefined variable '%s'.", name->chars);
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 push(value);
//                 break;
//             }
//             case OP_SET_GLOBAL: {
//                 ObjString* name = READ_STRING();

//                 // Challenge EDIT: Added support for handling declaration of single assignment global variables.
//                 if (isFinalGlobal(name)) {
//                     runtimeError("Cannot assign to final variable '%s'.", name->chars);
//                     return INTERPRET_RUNTIME_ERROR;
//                 }

//                 if (tableSet(&vm.globals, name, peek(0))) {
//                     tableDelete(&vm.globals, name);
//                     runtimeError("Undefined variable '%s'.", name->chars);
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 // Does not pop because assignment is an expression.
//                 break;
//             }
//             case OP_GET_UPVALUE: {
//                 uint8_t slot = READ_BYTE();
//                 push(*frame->closure->upvalues[slot]->location);
//                 break;
//             }
//             case OP_SET_UPVALUE: {
//                 uint8_t slot = READ_BYTE();
//                 *frame->closure->upvalues[slot]->location = peek(0);
//                 break;
//             }
//             case OP_GET_PROPERTY: {
//                 if (!IS_INSTANCE(peek(0))) {
//                     runtimeError("Only instances have properties.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 ObjInstance* instance = AS_INSTANCE(peek(0));
//                 ObjString* name = READ_STRING();

//                 Value value;
//                 if (tableGet(&instance->fields, name, &value)) {
//                     pop(); // Pop the instance.
//                     push(value);
//                     break;
//                 }

//                 if (!bindMethod(instance->klass, name)) {
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 break;
//             }
//             case OP_SET_PROPERTY: {
//                 if (!IS_INSTANCE(peek(1))) {
//                     runtimeError("Only instances have fields.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 ObjInstance* instance = AS_INSTANCE(peek(1));
//                 // No GC worry here since instance and value is already on the stack.
//                 tableSet(&instance->fields, READ_STRING(), peek(0));
//                 Value value = pop();
//                 pop(); 
//                 push(value); // Set expression leaves the value on the stack.
//                 break;
//             }
//             case OP_GET_SUPER: {
//                 ObjString* name = READ_STRING();
//                 ObjClass* superclass = AS_CLASS(pop());

//                 // Gets the superclass and binds the current instance to the method.
//                 if (!bindMethod(superclass, name)) {
//                     return INTERPRET_RUNTIME_ERROR;
//                 }

//                 break;
//             }
//             case OP_EQUAL: {
//                 Value b = pop();
//                 Value a = pop();
//                 push(BOOL_VAL(valuesEqual(a, b)));
//                 break;
//             }
//             case OP_DEFINE_GLOBAL: {
//                 ObjString* name = READ_STRING();

//                 // Challenge EDIT: Added support for single assignment global variables.
//                 if (isFinalGlobal(name)) {
//                     runtimeError("Cannot redefine final variable '%s'.", name->chars);
//                     return INTERPRET_RUNTIME_ERROR;
//                 }

//                 tableSet(&vm.globals, name, peek(0));
//                 pop(); // pop AFTER adding to hash table to allow for garbage collection anomalies while hash table resizing. If not, pointer to the might be present on the C stack but hash table resizing can cause GC trigger, which will free the Value. 
//                 break;
//             }
//             // Challenge EDIT: Added support for handling declaration of single assignment global variables.
//             case OP_DEFINE_FINAL_GLOBAL: {
//                 ObjString* name = READ_STRING();

//                 if (isFinalGlobal(name)) {
//                     runtimeError("Cannot redeclare final variable '%s'.", name->chars);
//                     return INTERPRET_RUNTIME_ERROR;
//                 }

//                 tableSet(&vm.globals, name, peek(0));
//                 tableSet(&vm.finalGlobals, name, BOOL_VAL(true));
//                 pop();
//                 break;

//             }
//             case OP_NEGATE: 
//                 if (!IS_NUMBER(peek(0))) {
//                     runtimeError("Operand must be a number.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 push(NUMBER_VAL(-(AS_NUMBER(pop()))));
//                 break;
//             case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
//             case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
//             case OP_ADD: {
//                 if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
//                     concatenate();
//                 } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
//                     double b = AS_NUMBER(pop());
//                     double a = AS_NUMBER(pop());
//                     push(NUMBER_VAL(a + b));
//                 } else {
//                     runtimeError("Operands must be two numbers or two strings");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 break;
//             }
//             case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
//             case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
//             case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
//             case OP_NOT:
//                 push(BOOL_VAL(isFalsey(pop())));
//                 break;
//             case OP_PRINT: {
//                 printValue(pop());
//                 printf("\n");
//                 break;
//             }
//             case OP_JUMP: {
//                 uint16_t offset = READ_SHORT();
//                 ip += offset;
//                 break;
//             }
//             case OP_JUMP_IF_FALSE: {
//                 uint16_t offset = READ_SHORT();
//                 if (isFalsey(peek(0))) ip += offset;
//                 break;
//             }
//             case OP_LOOP: {
//                 uint16_t offset = READ_SHORT();
//                 ip -= offset;
//                 break;
//             }
//             case OP_LIST: {
//                 // EDIT: Added OpCode for list creation.
//                 ObjList* list = newList();
//                 uint8_t itemCount = READ_BYTE();

//                 push(OBJ_VAL(list)); // Ensures GC trigger from appendToList does not free this.
//                 for (int i = itemCount; i > 0; i--) {
//                     appendToList(list,  peek(i));
//                 }
//                 pop();

//                 while (itemCount-- > 0) {
//                     pop();
//                 }

//                 push(OBJ_VAL(list));
//                 break;
//             }
//             case OP_GET_INDEX: {
//                 // EDIT: Added OpCode for getting Value based on list index.
//                 Value index_ = pop();
//                 Value list_ = pop();

//                 // Check if the list is valid, if the index is valid, and if the range is valid.
//                 if (!IS_LIST(list_)) {
//                     runtimeError("Only lists can be indexed.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 ObjList* list = AS_LIST(list_);

//                 if (!IS_NUMBER(index_)) {
//                     runtimeError("List index can only be a number.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 int index = AS_NUMBER(index_);

//                 if (!isValidListIndex(list, index)) {
//                     runtimeError("List index out of range.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }

//                 Value result = indexFromList(list, index);
//                 push(result);
//                 break;
//             }
//             case OP_SET_INDEX: {
//                 // EDIT: Added OpCode for setting Value based on list index.
//                 Value item_ = pop();
//                 Value index_ = pop();
//                 Value list_ = pop();

//                 if (!IS_LIST(list_)) {
//                     runtimeError("Cannot store value in a non-list.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 ObjList* list = AS_LIST(list_);

//                 if (!IS_NUMBER(index_)) {
//                     runtimeError("List index is not a number.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 int index = AS_NUMBER(index_);

//                 if (!isValidListIndex(list, index)) {
//                     runtimeError("Invalid list index.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }

//                 storeToList(list, index, item_);
//                 push(item_);
//                 break;
//             }
//             case OP_CALL:  {
//                 int argCount = READ_BYTE();

//                 // Challenge EDIT: 24.1
//                 frame->ip = ip; // Save caller's current ip.

//                 // Gets the function object by peeking beyond the arguments.
//                 if (!callValue(peek(argCount), argCount)) {
//                     return INTERPRET_RUNTIME_ERROR;
//                 }
//                 frame = &vm.frames[vm.frameCount - 1];

//                 // Challenge EDIT: 24.1 
//                 ip = frame->ip;

//                 break;
//             }
//             case OP_INVOKE: {
//                 ObjString* method = READ_STRING();
//                 int argCount = READ_BYTE();

//                 // Challenge EDIT: 24.1
//                 frame->ip = ip;

//                 if (!invoke(method, argCount)) {
//                     return INTERPRET_RUNTIME_ERROR;
//                 }

//                 frame = &vm.frames[vm.frameCount - 1];
//                 // Challenge EDIT: 24.1
//                 ip = frame->ip;

//                 break;
//             }
//             case OP_SUPER_INVOKE: {
//                 // Order gets shuffled, since the arguments must already be present on the stack. The superclass is above the arguments.
//                 ObjString* method = READ_STRING();
//                 int argCount = READ_BYTE();
//                 ObjClass* superclass = AS_CLASS(pop());
                
//                 // Challenge EDIT: 24.1
//                 frame->ip = ip;

//                 // Does not invoke fields.
//                 if (!invokeFromClass(superclass, method, argCount)) {
//                     return INTERPRET_RUNTIME_ERROR;
//                 }

//                 frame = &vm.frames[vm.frameCount - 1];
//                 // Challenge EDIT: 24.1
//                 ip = frame->ip;

//                 break;
//             }
//             case OP_CLOSURE: {
//                 ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
//                 ObjClosure* closure = newClosure(function);
//                 push(OBJ_VAL(closure));
//                 // Reads the bytecode and creates upvalues.
//                 for (int i = 0; i < closure->upvalueCount; i++) {
//                     uint8_t isLocal = READ_BYTE();
//                     uint8_t index = READ_BYTE();
//                     if (isLocal) {
//                         closure->upvalues[i] = captureUpvalue(frame->slots + index);
//                     } else {
//                         closure->upvalues[i] = frame->closure->upvalues[index];
//                     }
//                 }
//                 break;
//             }
//             case OP_CLOSE_UPVALUE:
//                 closeUpvalues(vm.stackTop - 1);
//                 pop();
//                 break;
//             case OP_RETURN: {
//                 Value result = pop();
//                 // Closes upvalues at the end of a function. This is because compiler does NOT emit instructions for popping or closing upvalues at the end of a function block.
//                 closeUpvalues(frame->slots);
//                 vm.frameCount--;
//                 if (vm.frameCount == 0) {
//                     // Pop the main script function and exit.
//                     pop();
//                     return INTERPRET_OK;
//                 }

//                 // Discard the frame and return to the caller function's call frame.
//                 vm.stackTop = frame->slots;
//                 push(result);
//                 frame = &vm.frames[vm.frameCount - 1];

//                 // Challenge EDIT: 24.1
//                 ip = frame->ip; // Restore the callee's current ip.

//                 break;
//             }
//             case OP_CLASS:  
//                 push(OBJ_VAL(newClass(READ_STRING())));
//                 break;
//             case OP_INHERIT: {
//                 // Unlike jlox, subclass does not store a reference to superclass.
//                 // This means that monkey patching is not allowed in clox. Not allowed in jlox as well, as I found out.
//                 Value superclass = peek(1);
//                 if (!IS_CLASS(superclass)) {
//                     runtimeError("Superclass must be a class.");
//                     return INTERPRET_RUNTIME_ERROR;
//                 }

//                 ObjClass* subclass = AS_CLASS(peek(0));
//                 tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
//                 pop(); // Subclass.
//                 break;
//             }
//             case OP_METHOD:
//                 defineMethod(READ_STRING());
//                 break;
//         }
//     }
//     #undef READ_BYTE
//     #undef READ_SHORT
//     #undef READ_CONSTANT
//     #undef READ_STRING
//     #undef BINARY_OP
// }

// // Interpretes the code and returns a result value.
// // EDIT: Compiler returns a function which contains a chunk, VM no longer provides a chunk.
// InterpretResult interpret(const char* source) {
//     // Gets the script function.
//     ObjFunction* function = compile(source);
//     if (function == NULL) return INTERPET_COMPILE_ERROR;

//     // Creates a first class Clsoure value for the script function and manually pushes it to the base of the stack.
//     // Function is pushed first to handle GC.
//     push(OBJ_VAL(function));
//     ObjClosure* closure = newClosure(function);
//     pop();
//     push(OBJ_VAL(closure));
//     // Creates the call frame for script function.
//     call(closure, 0);

//     return run();
// }