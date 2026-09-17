#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "common.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include <stdint.h>

// Caps the height of the CallFrame stack i.e the number of ongoing calls. Consequently caps the value stack relative to that.
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

// Struct for a live function invocation.
// Slots points at the first slot in the VMs value stack that the function call can use.
// Caller contains its own ip, acting like a return address.
// Pointer to the function being called.
typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
} CallFrame;

// Definition of stack-based VM struct.
    /* Instruction pointer/program counter always points to the instruction to be executed, not the instruction being executed.
       Note that it is being stored as a field, not a local variable - C handles local variables a lot better due to knowing its lifetime and stack trace, and therefore makes the code faster at the cost of extra plumbing.
       Dereferencing is faster than looking up instruction by index. */
// Challenge EDIT: Added support for single assignment global variables(final globals table).
// Each call frame has its own ip and own ObjFunction pointer, enabling getting to a certain function's chunk.
typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;
    Value stack[STACK_MAX];
    Value* stackTop;
    Table globals;
    Table finalGlobals;
    Table strings;
    Obj* objects;
} VM;

// Enum typedef to indicate the result.
typedef enum {
    INTERPRET_OK,
    INTERPET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

// Basically tells the compiler that a variable of name vm exists somewhere(vm.c here), and will be found later by the linker.
extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif