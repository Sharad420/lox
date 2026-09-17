#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

// Macro to downcast the void*(Obj* in this case) to the specfic Obj type, using type punning.
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

// Utility function to allocate an Obj of size of the specific object type and intialize it's fields.
static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;

    // Adds object to head of linked list.
    object->next = vm.objects;
    vm.objects = object;

    return object;
}

// Function to allocate an ObjClosure.
ObjClosure* newClosure(ObjFunction* function) {
    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    return closure;
}

// Utility function to allocate a string object and initializes it's fields, like a constructor.
static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);

    string->length = length;
    string->chars = chars;
    string->hash = hash;

    // String interning.
    tableSet(&vm.strings, string, NIL_VAL);
    return string;
}

// Function to allocate a new LoxFunction and return its pointer.
ObjFunction* newFunction() {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

// Function to allocate a new ObjNative.
ObjNative* newNative(NativeFn function, int arity) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->arity = arity;
    native->function = function;
    return native;
}


// Utility function to calculate hash function(FNV-1a).
// "Mixing" in each bit to an initial hash value and "scrambling" it a bit.
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u; // u stands for unsinged, to fit the number in 32 bits.
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

// Function to allocate a string while taking ownership of that string, unlike copyString.
ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // Checks if string is already interned and frees up the string currently owned by this function.
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocateString(chars, length, hash);
}

// Copies a string literal to heap memory and calculates it's hash.
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

// Creates a new Upvalue Object given a pointer to the slot where the closed-over variable lives.
ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    return upvalue;
}

// Helper function to print a Lox function object, top-level or user defined.
static void printFunction(ObjFunction* function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

// Helper function to handle printing an object.
void printObject(Value value) {
    switch(OBJ_TYPE(value)) {
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_STRING:    
            printf("%s", AS_CSTRING(value));
            break;
        // Upvalues are not first class, and will not be accessed by the users. The value inside the Upvalue object will, but not the Upvalue itself.
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
    }
}