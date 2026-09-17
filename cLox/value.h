#ifndef clox_value_h
#define clox_value_h

#include "common.h"

// Forward declared alias given to struct to handle self references.
typedef struct Obj Obj;
typedef struct ObjString ObjString;


// "Tag" to define the type of values the VM supports.
typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ
} ValueType;

// Tagged union to represent a Lox value.
// Union is quintessential to C for memory management, but is very dangerous.
typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;

// Macros to check value's type.
#define IS_BOOL(value)      ((value).type == VAL_BOOL)
#define IS_NIL(value)       ((value).type == VAL_NIL)
#define IS_NUMBER(value)    ((value).type == VAL_NUMBER)
#define IS_OBJ(value)       ((value).type == VAL_OBJ)

// Macros to get C value from correct clox value.
#define AS_OBJ(value)       ((value).as.obj)
#define AS_BOOL(value)      ((value).as.boolean)
#define AS_NUMBER(value)    ((value).as.number)

// Macros to convert native C to clox value.
#define BOOL_VAL(value)     ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL             ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value)   ((Value){VAL_NUMBER, {.number = value}})
// Using type punning, given an ObjX*, it is casted to Obj*.
#define OBJ_VAL(object)      ((Value){VAL_OBJ, {.obj = (Obj*)object}})


// Constant pool which stores constants and is accessed by address.
typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif