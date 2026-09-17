#include <stdio.h>

#include "memory.h"
#include "line.h"

// Initializes the line run array.
void initLineRunArray(LineRunArray *array) {
    array->lines = NULL;
    array->count = 0;
    array->capacity = 0;
}

// Frees the lineRunArray and cleans the state.
void freeLineRunArray(LineRunArray* array) {
    FREE_ARRAY(LineRun, array->lines, array->capacity);
    initLineRunArray(array);
}

// Adds a run to an existing line or creates a new run in the dynamic array.
void writeLineRunArray(LineRunArray *array, int line) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->lines = GROW_ARRAY(LineRun, array->lines, oldCapacity, array->capacity);
    }

    // Check if the array is not empty. If the inputted line is equal to the previous LineRun.line, then increment that run, else create a new run.
    if (array->count > 0 && array->lines[array->count - 1].line == line) {
        array->lines[array->count - 1].run++;
    } else {
        LineRun* lineRun = &array->lines[array->count];
        lineRun->line = line;
        lineRun->run = 1;
        array->count++;
    }
}

// Gets the line number based on the offset of the instruction. O(n)-time pass, but this tradeoff is made because we don't expect errors to occur often.
int getLine(const LineRunArray* array, int offset) {
    int currentOffset = 0;
    for (int i = 0; i < array->count; i++) {
        currentOffset += array->lines[i].run;
        if (offset < currentOffset) {
            return array->lines[i].line;
        }
    }
    return -1;
}

