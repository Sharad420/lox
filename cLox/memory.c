#include <stddef.h>
#include <stdlib.h>

#include "memory.h"
#include "chunk.h"
#include "compiler.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

// Single function for all dynamic memory management.
// Everything runs through reallocate because it becomes easier to keep track of the number of bytes of allocated memory.
// size_t is the type for number/count of bytes.
// Runs GC everytime the threshold is crossed.
// Regulator: Think about what values are on the C stack and what values are reachable everytime reaallocate is called.
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize) {
        // Flag to garbage collector as stress test everytime memory is allocated.
#ifdef DEBUG_STRESS_GC
            collectGarbage();
#endif
        if (vm.bytesAllocated > vm.nextGC) {
            collectGarbage();
        }
    }
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);

    // If we're not able to realloc to a bigger memory, throw error.
    if (result == NULL) exit(1);
    return result;
}

// Marks a reachable object during the marking phase.
// Adds the marked object to the working list while traversing.
void markObject(Obj *object) {
    if (object == NULL) return;
    // Avoids cycles.
    if (object->isMarked) return;

    object->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        // Manually allocating using system's realloc function, to avoid recursive GC calling.
        vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
        // Exit if gray stack allocation fails. Not elegant, but keeps it short. Can also allocate a "rainy day" block.
        if (vm.grayStack == NULL) exit(1);
    }
    vm.grayStack[vm.grayCount++] = object;
#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
}

// If a reachable value is an object, mark it. (Precise GC)
void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

// Traversing a single object's references. Marks GC managed objects, the unmarked objects themselves are freed via freeObject().
void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
    switch(object->type) {
        case OBJ_BOUND_METHOD:{
            ObjBoundMethod* bound = (ObjBoundMethod*)object;
            markValue(bound->receiver);
            markObject((Obj*)bound->method);
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            markObject((Obj*)klass->name);
            markTable(&klass->methods);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            markObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            markObject((Obj*)function->name);
            markArray(&function->chunk.constants);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            markObject((Obj*)instance->klass);
            markTable(&instance->fields);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            for (int i = 0; i < list->count; i++) {
                markValue(list->items[i]);
            }
        }
        case OBJ_UPVALUE:
            // Open upvalues->location need not be marked since they are roots, closed upvalues->closed need to be marked because they no longer exist on the stack.
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

// Helper function to free an object from memory.
// Different implementation for different object types, which may own other allocated memory.
static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch (object->type) {
        case OBJ_BOUND_METHOD: 
            // Frees Value receiver as well.
            FREE(ObjBoundMethod, object);
            break;
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            // Entries in the table are not freed. The GC will handle it.
            freeTable(&klass->methods);
            FREE(ObjClass, object);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            // Frees the closure's upvalue array.
            FREE_ARRAY(ObjUpvalue, closure->upvalues, closure->upvalueCount);
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
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            // Entries in the table are not freed, because there may be other references to those objects. Only frees the table.
            // GC handles freeing those objects.
            freeTable(&instance->fields);
            FREE(ObjInstance, object);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            FREE_ARRAY(Value, list->items, list->count);
            FREE(ObjList, object);
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

// Helper function to traverse and mark reachable roots.
// Basically a DFS for the connected components in the graph, but only roots are considered for starting a component.
// Roots are local variables/temporaries sitting in the VM's stack, and global variables.
// Any other references the VM can directly reach are roots as well - closures on the call frame, the vm's open upvalue list.
// Also includes objects that the compiler may access during compilation.
static void markRoots() {
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    // Marks all closures on the call Frame, even though they may already be marked. Marking all references.
    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }

    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }

    markTable(&vm.globals);
    markTable(&vm.finalGlobals);
    markCompilerRoots();
    markObject((Obj*)vm.initString);
}

// Marks the references that are reached via the roots.
void traceReferences() {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

// Sweeps and frees up all the objects in the VM object list not marked - and primes them for the next GC run.
void sweep() {
    Obj* previous = NULL;
    Obj* object = vm.objects;

    while (object != NULL) {
        if (object->isMarked) {
            // Clear the bit for the next GC run.
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            if (previous != NULL) {
                object = object->next;
                previous->next = object;
            } else {
                vm.objects = object->next;
            }

            freeObject(unreached);
        }
    }
}

// Implements the mark-and-sweep algorithm.
// Adjusts the threshold size for next GC trigger.
void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytesAllocated;
#endif

    markRoots();
    traceReferences();
    tableRemoveWhite(&vm.strings);
    sweep();
    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu), next GC run at %zu\n", before - vm.bytesAllocated, before, 
        vm.bytesAllocated, vm.nextGC);
#endif
}

// Frees all the objects on the linked list.
// Manually frees the working list.
void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    free(vm.grayStack);
}