/*
 * object.c - Object allocation, GC marking, and printing
 */

#include "object.h"
#include "memory.h"
#include "table.h"
#include "vm.h"
#include <stdio.h>
#include <string.h>

/* Allocate an object of given type and size, link into GC list */
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;
    
    // Link into VM's object list for GC
    object->next = vm.objects;
    vm.objects = object;
    
#if DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif
    
    return object;
}

/* String interning - hash and check if string already exists */
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;  // FNV-1a
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    
    // Intern the string
    tableSet(&vm.strings, string, NIL_VAL);
    return string;
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    
    // Check if already interned
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;
    
    // Allocate new string
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    
    return allocateString(chars, length, hash);
}

ObjFunction* newFunction(void) {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjNative* newNative(NativeFn function, ObjString* name) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    native->name = name;
    return native;
}

ObjClosure* newClosure(ObjFunction* function) {
    // Allocate upvalue array
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }
    
    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

ObjClass* newClass(ObjString* name) {
    ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    klass->superclass = NULL;
    initTable(&klass->methods);
    initTable(&klass->privates);
    return klass;
}

ObjInstance* newInstance(ObjClass* klass) {
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method) {
    ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjTable* newTable(void) {
    ObjTable* table = ALLOCATE_OBJ(ObjTable, OBJ_TABLE);
    initTable(&table->entries);
    initValueArray(&table->array);
    return table;
}

ObjTrait* newTrait(ObjString* name) {
    ObjTrait* trait = ALLOCATE_OBJ(ObjTrait, OBJ_TRAIT);
    trait->name = name;
    initTable(&trait->methods);
    return trait;
}

/* GC: Mark a single object as reachable */
void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return;
    
#if DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
    
    object->isMarked = true;
    
    // Add to gray stack for tracing
    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
        if (vm.grayStack == NULL) exit(1);
    }
    vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

/* GC: Trace all references from a marked object */
void blackenObject(Obj* object) {
#if DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
    
    switch (object->type) {
        case OBJ_STRING:
        case OBJ_NATIVE:
            // No outgoing references
            break;
            
        case OBJ_UPVALUE:
            markValue(((ObjUpvalue*)object)->closed);
            break;
            
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            markObject((Obj*)function->name);
            markArray(&function->chunk.constants);
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
        
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            markObject((Obj*)klass->name);
            markObject((Obj*)klass->superclass);
            markTable(&klass->methods);
            break;
        }
        
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            markObject((Obj*)instance->klass);
            markTable(&instance->fields);
            break;
        }
        
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* bound = (ObjBoundMethod*)object;
            markValue(bound->receiver);
            markObject((Obj*)bound->method);
            break;
        }
        
        case OBJ_TABLE: {
            ObjTable* table = (ObjTable*)object;
            markTable(&table->entries);
            markArray(&table->array);
            break;
        }
        
        case OBJ_TRAIT: {
            ObjTrait* trait = (ObjTrait*)object;
            markObject((Obj*)trait->name);
            markTable(&trait->methods);
            break;
        }
    }
}

/* Free a single object's memory */
void freeObject(Obj* object) {
#if DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif
    
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            break;
        }
        
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
            
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, object);
            break;
            
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            freeTable(&klass->methods);
            freeTable(&klass->privates);
            FREE(ObjClass, object);
            break;
        }
        
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            freeTable(&instance->fields);
            FREE(ObjInstance, object);
            break;
        }
        
        case OBJ_BOUND_METHOD:
            FREE(ObjBoundMethod, object);
            break;
            
        case OBJ_TABLE: {
            ObjTable* table = (ObjTable*)object;
            freeTable(&table->entries);
            freeValueArray(&table->array);
            FREE(ObjTable, object);
            break;
        }
        
        case OBJ_TRAIT: {
            ObjTrait* trait = (ObjTrait*)object;
            freeTable(&trait->methods);
            FREE(ObjTrait, object);
            break;
        }
    }
}

/* Print an object value */
void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION: {
            ObjFunction* fn = AS_FUNCTION(value);
            if (fn->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", fn->name->chars);
            }
            break;
        }
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_CLOSURE:
            printObject(OBJ_VAL(AS_CLOSURE(value)->function));
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
        case OBJ_CLASS:
            printf("<class %s>", AS_CLASS(value)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("<instance %s>", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_BOUND_METHOD:
            printObject(OBJ_VAL(AS_BOUND_METHOD(value)->method->function));
            break;
        case OBJ_TABLE: {
            ObjTable* table = AS_TABLE(value);
            printf("{");
            bool first = true;
            // Print array part
            for (int i = 0; i < table->array.count; i++) {
                if (!first) printf(", ");
                first = false;
                printValue(table->array.values[i]);
            }
            // Print hash part (simplified - just show count)
            if (table->entries.count > 0) {
                if (!first) printf(", ");
                printf("... %d more", table->entries.count);
            }
            printf("}");
            break;
        }
        case OBJ_TRAIT:
            printf("<trait %s>", AS_TRAIT(value)->name->chars);
            break;
    }
}
