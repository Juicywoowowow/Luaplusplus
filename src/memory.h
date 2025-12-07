#ifndef luapp_memory_h
#define luapp_memory_h

#include "common.h"

// Allocation macros
#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) \
    reallocate(pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

// Core allocation function - all memory goes through here
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

// GC functions
void collectGarbage(void);
void freeObjects(void);

// Memory tracking
size_t getBytesAllocated(void);

#endif
