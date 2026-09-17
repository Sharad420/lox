#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

// Macro to access the type of the object
#define OBJ_TYPE(value)      (AS_OBJ(value)->type)

// Macro to check if the Object is a certain Obj. Function used because the argument to the macro is used twice, but we don't want it to be evaluated twice.
// Function just passes the evaluated argument expression to the body. Think IS_STRING(pop())
#define IS_CLOSURE(value)    isObjectType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)   isObjectType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)     isObjectType(value, OBJ_NATIVE)
#define IS_STRING(value)     isObjectType(value, OBJ_STRING)

// Macros to return an Obj* downcasted pointer and the character array.
#define AS_CLOSURE(value)    ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)   ((ObjFunction*)AS_OBJ(value))
// Returns the NativeFn directly.
#define AS_NATIVE(value)     ((ObjNative*)AS_OBJ(value))
#define AS_STRING(value)     ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)    (((ObjString*)AS_OBJ(value))->chars)

// Enum to represent the type of object.
typedef enum {
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE
} ObjType;

// Struct to represent the heap allocated Object, and a pointer to the next Obj.
struct Obj {
    ObjType type;
    struct Obj* next;
};

// Struct to represent first class Lox functions as an Object. Holds a chunk for bytecode.
typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

// Typedef of a pointer to a C function that returns a Value.
// Takes the arg count and the pointer to the first argument on the stack.
typedef Value (*NativeFn)(int argCount, Value* args);

// Struct to represent native functions. They do not contain a chunk like ObjFunctions, because they do not have bytecode. They reference native C code.
// Challenge EDIT: 24.2, added arity for dynamic checking.
typedef struct {
    Obj obj;
    int arity;
    // Pointer to the C function that implements it.
    NativeFn function;
} ObjNative;

// String stuct containing Obj state as first field for type punning. In the sense of OOP, ObjString "is" an Obj
struct ObjString {
    Obj obj;
    int length;
    char* chars;
    // Taking advantage of the string immutability of Lox to cache hash.
    uint32_t hash;
};

// Runtime representation for upvalues.
// Managing closed over variables that no longer live on the stack implies some kind of dynamic allocation.
typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
} ObjUpvalue;

// Struct which wraps around the static ObjFunction containing the runtime state of the variables it closes over.
typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjNative* newNative(NativeFn function, int arity);
ObjString* takeString(char* chars, int length);
// Function to allocate an array on the heap, create a string, and return a pointer.
ObjString* copyString(const char* chars, int length);
ObjUpvalue* newUpvalue(Value* slot);

// Helper function to print Value of type object.
void printObject(Value value);

// Function to determining Object type.
// Static to avoid linker issues and inline to optimize compiler.
static inline bool isObjectType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif