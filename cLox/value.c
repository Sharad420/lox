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
void printValue(Value value) {
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

}

// Checks equality of any 2 Values, even of different types.
bool valuesEqual(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:    return true;
        case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
        // Interning of objects allows for pointer comparision.
        case VAL_OBJ:    return AS_OBJ(a) == AS_OBJ(b);
        default:         return false; // Unreachable
    }
}