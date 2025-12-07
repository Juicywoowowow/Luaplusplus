#include "memory.h"
#include "vm.h"
#include "compiler.h"
#include <stdio.h>

#define GC_HEAP_GROW_FACTOR 2

// Forward declarations for GC
static void markRoots(void);
static void traceReferences(void);
static void sweep(void);

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;
    
    if (newSize > oldSize) {
#if DEBUG_STRESS_GC
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
    if (result == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    return result;
}

void collectGarbage(void) {
    size_t before = vm.bytesAllocated;
    
    if (debugFlags.logGC) {
        printf("-- gc begin (allocated: %zu bytes)\n", before);
    }

    markRoots();
    traceReferences();
    sweep();
    
    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

    if (debugFlags.logGC) {
        printf("-- gc end: collected %zu bytes (from %zu to %zu), next at %zu\n",
               before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
    }
}

static void markRoots(void) {
    // Mark stack values
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }
    
    // Mark call frames' closures
    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }
    
    // Mark open upvalues
    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }
    
    // Mark globals
    markTable(&vm.globals);
    
    // Mark compiler roots (if compiling)
    markCompilerRoots();
    
    // Mark init string
    markObject((Obj*)vm.initString);
}

static void traceReferences(void) {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep(void) {
    Obj* previous = NULL;
    Obj* object = vm.objects;
    
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }
            freeObject(unreached);
        }
    }
}

void freeObjects(void) {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    free(vm.grayStack);
}

size_t getBytesAllocated(void) {
    return vm.bytesAllocated;
}
