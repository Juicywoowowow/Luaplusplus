/*
 * value.h - Tagged union representation for Lua++ values
 * 
 * Every value in Lua++ is either a primitive (nil, bool, number) or
 * a heap-allocated object (string, function, class, instance, etc).
 * We use NaN-boxing for compact 64-bit representation on supported platforms,
 * falling back to a tagged union struct otherwise.
 */

#ifndef luapp_value_h
#define luapp_value_h

#include "common.h"

// Forward declare Obj - actual definition in object.h
typedef struct Obj Obj;
typedef struct ObjString ObjString;

/*
 * Value types - what kind of data a Value holds.
 * Objects are heap-allocated and tracked by GC.
 */
typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_OBJ  // Pointer to heap object (string, closure, class, etc)
} ValueType;

/*
 * The Value struct - core data type of the VM.
 * Uses a tagged union: 'type' says which field of 'as' is valid.
 */
typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;

/* Type checking macros */
#define IS_NIL(value)    ((value).type == VAL_NIL)
#define IS_BOOL(value)   ((value).type == VAL_BOOL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value)    ((value).type == VAL_OBJ)

/* Value unpacking - extract the C value from a Value */
#define AS_BOOL(value)   ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value)    ((value).as.obj)

/* Value constructors - wrap C values into Value */
#define NIL_VAL          ((Value){VAL_NIL, {.number = 0}})
#define BOOL_VAL(value)  ((Value){VAL_BOOL, {.boolean = value}})
#define NUMBER_VAL(value)((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object)  ((Value){VAL_OBJ, {.obj = (Obj*)object}})

/*
 * ValueArray - dynamic array of Values.
 * Used for constant pools in chunks and other collections.
 */
typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

/* ValueArray operations */
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);

/* Utility functions */
void printValue(Value value);
bool valuesEqual(Value a, Value b);

#endif
