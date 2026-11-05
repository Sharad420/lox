#ifndef clox_value_h
#define clox_value_h

#include "common.h"
#include <stdint.h>
#include <string.h>

// Forward declared alias given to struct to handle self references.
typedef struct Obj Obj;
typedef struct ObjString ObjString;


// OPTIMIZATION: Representing different Values by making use of IEEE 754 NaN representation's unused bits.
#ifdef NAN_BOXING

#define SIGN_BIT ((uint64_t)0x8000000000000000)             // Bit mask to differentiate a Lox Object.
                                                            // Bit mask to differentiate between Lox numbers and values
#define QNAN     ((uint64_t)0x7ffc000000000000)

#define TAG_NIL   1 // 01
#define TAG_FALSE 2 // 10
#define TAG_TRUE  3 // 11

                                                            // uint64_t instead of double because bitwise operations are easier.
typedef uint64_t Value;

                                                            // Macros to check value's type.
                                                            // If the value has it's exponent, quiet NaN, and Intel NaN bits set, it is NOT a number.
#define IS_BOOL(value)      (((value) | 1) == TRUE_VAL)                                                      
#define IS_NIL(value)       ((value) == NIL_VAL)                                        
#define IS_NUMBER(value)    (((value) & QNAN) != QNAN)
#define IS_OBJ(value)       (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))


// Macros to get C value from correct clox value. 
#define AS_BOOL(value)      ((value) == TRUE_VAL)
#define AS_NUMBER(value)    valueToNum(value)
#define AS_OBJ(value) \
    ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))       // Mask everything but the last 48 bits, and cast to Obj*.

                                                            // memcpy optimized away.
static inline double valueToNum(Value value) {
    double num;
    memcpy(&num, &value, sizeof(Value));
    return num;
}

// Macros to convert native C to clox values.
#define BOOL_VAL(b)        ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL          ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL           ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL            ((Value)(uint64_t)(QNAN | TAG_NIL))      // Explicit conversions.
#define NUMBER_VAL(num)    numToValue(num)
#define OBJ_VAL(obj) \
    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))           // In most architectures, everything above the 48th bit is 0 for a pointer, so this is REASONABLY safe.
                                                                    // Sign bit OR'ed QNAN OR'ed the pointer representation gives the Lox Value. (The last 3 bits are free because Obj* only points to 8-byte aligned objects.)

                                                                    // Just copies the bits of the double to the value, since the representation is 1:1.
                                                                    // This is a known pattern for type punning, hence compilers have optimized heavily.
static inline Value numToValue(double num) {
    Value value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

#else

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

#endif

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