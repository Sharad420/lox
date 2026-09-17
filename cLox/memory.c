#include <stdlib.h>

#include "memory.h"
#include "chunk.h"
#include "object.h"
#include "value.h"
#include "vm.h"

// Single function for all dynamic memory management.
// Everything runs through reallocate because it becomes easier to keep track of the number of bytes of allocated memory.
// size_t is the type for number/count of bytes.
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);

    // If we're not able to realloc to a bigger memory, throw error.
    if (result == NULL) exit(1);
    return result;
}

// Helper function to free an object from memory.
// Different implementation for different object types, which may own other allocated memory.
static void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            // Frees the closure, NOT the function. Multiple runtime closures can reference the same function.
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            // Function's name is cleared using the garbage collector.
            break;
        }
        case OBJ_NATIVE:
            FREE(ObjNative, object);
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length);
            FREE(ObjString, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, object);
            break;
    }
}

// Frees all the objects on the linked list.
void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}