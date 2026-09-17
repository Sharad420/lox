#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"

// Macro to allocate memory. NULL tells realloc to assign a new block of memory.
#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

// Macro to free any allocated memory.
#define FREE(type, pointer) reallocate((pointer), sizeof(type), 0)

// Macro to double capacity.
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

// Text wrapper around reallocate to grow.
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate((pointer), sizeof(type) * (oldCount), \
    sizeof(type) * (newCount))

// Text wrapper around reallocate to free an array.
#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate((pointer), sizeof(type) * (oldCount), 0)

// void* signifies it takes in a pointer of unspecified type. This means that pointer arithmatic cannot be performed because reallocate is unaware of what type it is.
// This is why GROW_ARRAY immediately typecasts is back.
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

void freeObjects();

#endif