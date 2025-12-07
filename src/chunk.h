/*
 * chunk.h - Bytecode container
 * 
 * A Chunk holds compiled bytecode: opcodes, constants, and line info.
 * Each function compiles to its own Chunk.
 */

#ifndef luapp_chunk_h
#define luapp_chunk_h

#include "common.h"
#include "value.h"

/* Bytecode opcodes */
typedef enum {
    // Constants & literals
    OP_CONSTANT,        // Push constant from pool
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    
    // Stack manipulation
    OP_POP,
    OP_POPN,            // Pop N values
    
    // Variables
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_CLOSE_UPVALUE,
    
    // Arithmetic
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,
    OP_CONCAT,          // String concatenation (..)
    OP_LENGTH,          // # operator (table/string length)
    
    // Comparison & logic
    OP_NOT,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    
    // Control flow
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,            // Jump backwards
    
    // Functions
    OP_CALL,
    OP_CLOSURE,
    OP_RETURN,
    
    // OOP
    OP_CLASS,           // Define a class
    OP_INHERIT,         // Set superclass
    OP_METHOD,          // Add method to class
    OP_GET_PROPERTY,    // obj.field
    OP_SET_PROPERTY,    // obj.field = x
    OP_GET_SUPER,       // super.method
    OP_INVOKE,          // Optimized method call
    OP_SUPER_INVOKE,    // Optimized super call
    OP_NEW,             // Instantiate class
    
    // Tables
    OP_TABLE,           // Create empty table
    OP_TABLE_GET,       // table[key]
    OP_TABLE_SET,       // table[key] = value
    OP_TABLE_ADD,       // Add value to array part during literal construction
    OP_TABLE_SET_FIELD, // Set named field during literal construction
    
    // Traits
    OP_TRAIT,           // Define a trait
    OP_IMPLEMENT,       // Class implements trait
} OpCode;

/*
 * Chunk - bytecode storage for a single function/script.
 */
typedef struct {
    int count;
    int capacity;
    uint8_t* code;      // Bytecode array
    int* lines;         // Line numbers for error reporting
    ValueArray constants; // Constant pool
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

/* GC helper - mark constants in chunk */
void markArray(ValueArray* array);

#endif
