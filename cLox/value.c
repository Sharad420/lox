#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

// Initializes empty valueArray.
void initValueArray(ValueArray *array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void freeValueArray(ValueArray *array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

// Checks if valueArray has enough capacity and adds constant value.
// Allocated on the heap, via GROW_ARRAY macro.
void writeValueArray(ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

// Prints the value found.
// EDIT: Works with NaN boxed values too.
void printValue(Value value) {
#ifdef NAN_BOXING
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_NUMBER(value)) {
        printf("%g", AS_NUMBER(value));
    } else if (IS_OBJ(value)) {
        printObject(value);
    }
#else
    switch (value.type) {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL: printf("nil"); break;
        case VAL_NUMBER:
            // %g is the specifier for doubles and longs.
            printf("%g", AS_NUMBER(value));
            break;
        case VAL_OBJ: printObject(value); break;
    }
#endif
}

// Checks equality of any 2 Values, even of different types.
bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        // To handle IEEE 754's signalling NaN shenanigans. Sacrifices a little bit of performance.
        return AS_NUMBER(a) == AS_NUMBER(b);
    }
    return a == b; // If the bit representations are equal, the values are equal.
#else
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:    return true;
        case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b); // Learning: This works even though IEEE 754 specifies that signalling NaN values are not equal to themselves. THe C compiler handles this because it KNOWS the values are doubles and hence has it's own sidestep to this problem.
        // Interning of objects allows for pointer comparision.
        case VAL_OBJ:    return AS_OBJ(a) == AS_OBJ(b);
        default:         return false; // Unreachable
    }
#endif
}