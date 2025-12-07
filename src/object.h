/*
 * object.h - Heap-allocated object types for Lua++
 * 
 * All objects share a common Obj header for GC tracking.
 * Types: strings, functions, closures, upvalues, classes, instances.
 */

#ifndef luapp_object_h
#define luapp_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

/* Object type tags */
typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_TABLE,
    OBJ_TRAIT
} ObjType;

/*
 * Base object header - every heap object starts with this.
 * 'next' forms an intrusive linked list for GC traversal.
 */
struct Obj {
    ObjType type;
    bool isMarked;      // GC mark bit
    struct Obj* next;   // Next object in allocation list
};

/* ObjString - interned immutable string */
struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;      // Cached hash for fast table lookups
};

/* ObjFunction - compiled function (bytecode chunk + metadata) */
typedef struct {
    Obj obj;
    int arity;          // Parameter count
    int upvalueCount;
    Chunk chunk;        // Bytecode
    ObjString* name;    // Function name (NULL for scripts)
} ObjFunction;

/* Native C function signature */
typedef Value (*NativeFn)(int argCount, Value* args);

/* ObjNative - built-in C function (print, read, etc) */
typedef struct {
    Obj obj;
    NativeFn function;
    ObjString* name;
} ObjNative;

/*
 * ObjUpvalue - captures a local variable for closures.
 * 'location' points to stack slot while open, 'closed' holds value when closed.
 */
typedef struct ObjUpvalue {
    Obj obj;
    Value* location;        // Points to stack slot (open) or &closed (closed)
    Value closed;           // Holds value after variable goes out of scope
    struct ObjUpvalue* next; // Linked list of open upvalues
} ObjUpvalue;

/* ObjClosure - function + captured upvalues */
typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

/*
 * ObjClass - class definition with methods and inheritance.
 * 'privates' tracks which fields/methods are private.
 */
typedef struct ObjClass {
    Obj obj;
    ObjString* name;
    struct ObjClass* superclass;
    Table methods;          // Method name -> ObjClosure
    Table privates;         // Names marked private (value is just true)
} ObjClass;

/* ObjInstance - instantiated object with fields */
typedef struct {
    Obj obj;
    ObjClass* klass;
    Table fields;           // Field name -> Value
} ObjInstance;

/* ObjBoundMethod - method bound to a specific instance (for self) */
typedef struct {
    Obj obj;
    Value receiver;         // The instance 'self' refers to
    ObjClosure* method;
} ObjBoundMethod;

/*
 * ObjTable - Lua table (associative array + array part)
 * The core data structure of Lua - can be used as array, dict, or both.
 */
typedef struct {
    Obj obj;
    Table entries;          // Key-value pairs (hash part)
    ValueArray array;       // Array part (integer keys 1..n)
} ObjTable;

/*
 * ObjTrait - Reusable behavior that can be mixed into classes
 * Similar to interfaces but with default implementations.
 */
typedef struct {
    Obj obj;
    ObjString* name;
    Table methods;          // Method implementations
} ObjTrait;

/* Type checking macros */
#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

#define IS_STRING(value)    isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)    isObjType(value, OBJ_NATIVE)
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE)
#define IS_CLASS(value)     isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value)  isObjType(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_TABLE(value)     isObjType(value, OBJ_TABLE)
#define IS_TRAIT(value)     isObjType(value, OBJ_TRAIT)

/* Object unpacking macros */
#define AS_STRING(value)    ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)  ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)    (((ObjNative*)AS_OBJ(value))->function)
#define AS_CLOSURE(value)   ((ObjClosure*)AS_OBJ(value))
#define AS_CLASS(value)     ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)  ((ObjInstance*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_TABLE(value)     ((ObjTable*)AS_OBJ(value))
#define AS_TRAIT(value)     ((ObjTrait*)AS_OBJ(value))

/* Object constructors */
ObjString* copyString(const char* chars, int length);
ObjString* takeString(char* chars, int length);
ObjFunction* newFunction(void);
ObjNative* newNative(NativeFn function, ObjString* name);
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);
ObjClass* newClass(ObjString* name);
ObjInstance* newInstance(ObjClass* klass);
ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);
ObjTable* newTable(void);
ObjTrait* newTrait(ObjString* name);

/* GC helpers */
void markObject(Obj* object);
void markValue(Value value);
void blackenObject(Obj* object);
void freeObject(Obj* object);

/* Utility */
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
