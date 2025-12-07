/*
 * vm.h - Virtual machine for Lua++ bytecode
 * 
 * Stack-based interpreter with call frames, upvalues, and GC integration.
 */

#ifndef luapp_vm_h
#define luapp_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

/* Call frame - one per function invocation */
typedef struct {
    ObjClosure* closure;
    uint8_t* ip;            // Instruction pointer into closure's chunk
    Value* slots;           // First stack slot for this frame
} CallFrame;

/* VM state - global singleton */
typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;
    
    Value stack[STACK_MAX];
    Value* stackTop;
    
    Table globals;          // Global variables
    Table strings;          // String interning table
    ObjString* initString;  // Cached "init" string for constructors
    
    ObjUpvalue* openUpvalues;  // Linked list of open upvalues
    
    // GC state
    Obj* objects;           // All allocated objects
    int grayCount;
    int grayCapacity;
    Obj** grayStack;        // Worklist for mark phase
    
    size_t bytesAllocated;
    size_t nextGC;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM(void);
void freeVM(void);
InterpretResult interpret(const char* source);
InterpretResult interpretWithFilename(const char* source, const char* filename);

/* Stack operations */
void push(Value value);
Value pop(void);

/* Call a Lua++ closure from C code with arguments.
 * Returns true on success, false on error.
 * Result is stored in *result if not NULL.
 */
bool callClosure(ObjClosure* closure, int argCount, Value* args, Value* result);

#endif
