#include <stdint.h>
#include <stdlib.h>

#include "chunk.h"
#include "line.h"
#include "memory.h"
#include "value.h"

// Array is empty at initialization.
void initChunk(Chunk *chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    // chunk->lines = NULL;
    initValueArray(&chunk->constants);
    initLineRunArray(&chunk->lines);
}

// Deallocates memory and leaves chunk in a clean state.
void freeChunk(Chunk *chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    // FREE_ARRAY(int,chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
    freeLineRunArray(&chunk->lines);
    initChunk(chunk);
}

// Checks if array has enough capacity and then adds bytecode. Adds line number too.
// Bytecode is allocated on the heap.
void writeChunk(Chunk *chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        // chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    // chunk->lines[chunk->count] = line;
    writeLineRunArray(&chunk->lines, line);
    chunk->count++;
}

// Writes a constant to the chunk's constant pool. Returns the index where the constant has been appended.
int addConstant(Chunk *chunk, Value value) {
    // Challenge EDIT: Search existing constants.
    for (int i = 0; i < chunk->constants.count; i++) {
        if (valuesEqual(value, chunk->constants.values[i])) {
            return i;
        }
    }
    
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count - 1;
}