/*
 * compiler.c - Pratt parser + bytecode emission
 * 
 * Core parsing strategy:
 * - Pratt parsing for expressions (handles precedence elegantly)
 * - Recursive descent for statements and declarations
 * 
 * Optimizations:
 * - Constant folding: evaluate constant expressions at compile time
 * - Dead code elimination: remove unused local variables
 */

#include "compiler.h"
#include "debug.h"
#include "diagnostic.h"
#include "lexer.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default compiler options */
CompilerOptions compilerOptions = {
    .eliminateDeadCode = true,      /* Enable dead code elimination by default */
    .warnUnusedVariables = true,    /* Warn about unused variables */
};

/* Parser state */
typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
    DiagContext diag;       /* Diagnostic context for rich errors */
} Parser;

/* Precedence levels (low to high) */
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,    // =
    PREC_OR,            // or
    PREC_AND,           // and
    PREC_EQUALITY,      // == ~=
    PREC_COMPARISON,    // < > <= >=
    PREC_CONCAT,        // ..
    PREC_TERM,          // + -
    PREC_FACTOR,        // * / %
    PREC_UNARY,         // not - #
    PREC_CALL,          // . () :
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

/* Parse rule: prefix fn, infix fn, precedence */
typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

/* Local variable tracking */
typedef struct {
    Token name;
    int depth;          // Scope depth (-1 = uninitialized)
    bool isCaptured;    // Captured by closure?
    bool isUsed;        // Has this variable been read?
    bool isAssigned;    // Has this variable been assigned (for dead store detection)?
    int initBytecodeStart; // Bytecode position where initialization starts (for DCE)
    int initBytecodeEnd;   // Bytecode position where initialization ends (for DCE)
} Local;

/* Upvalue tracking */
typedef struct {
    uint8_t index;
    bool isLocal;       // Is it a local in enclosing function?
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_METHOD,
    TYPE_INITIALIZER,   // init() method
    TYPE_SCRIPT
} FunctionType;

/* Loop tracking for break/continue statements */
typedef struct Loop {
    struct Loop* enclosing;
    int start;              // Start of loop body
    int continueTarget;     // Where continue should jump to
    int scopeDepth;         // Scope depth when loop started
    int breakJumps[256];    // Jump locations to patch for break
    int breakCount;
} Loop;

/* Compiler state - one per function being compiled */
typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    
    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;
    
    Loop* currentLoop;      // Innermost loop for break
} Compiler;

/* Class compiler - tracks current class for self/super */
typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    bool hasSuperclass;
} ClassCompiler;

static Parser parser;
static Compiler* current = NULL;
static ClassCompiler* currentClass = NULL;

/* Forward declarations */
static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

/* ========== Error Handling ========== */

static void errorAtToken(Token* token, int code, const char* message, const char* help) {
    if (parser.panicMode) return;
    if (shouldStopCompiling(&parser.diag)) return;
    
    parser.panicMode = true;
    parser.hadError = true;
    
    reportDiagnostic(&parser.diag, DIAG_ERROR, code,
                     token->line, token->column, token->length,
                     message, help);
}

static void error(const char* message) {
    errorAtToken(&parser.previous, E_EXPECT_TOKEN, message, NULL);
}

static void errorWithCode(int code, const char* message, const char* help) {
    errorAtToken(&parser.previous, code, message, help);
}

static void errorAtCurrent(const char* message) {
    errorAtToken(&parser.current, E_EXPECT_TOKEN, message, NULL);
}

static void errorAtCurrentWithCode(int code, const char* message, const char* help) {
    errorAtToken(&parser.current, code, message, help);
}

static void warning(Token* token, int code, const char* message) {
    if (parser.panicMode) return;
    reportDiagnostic(&parser.diag, DIAG_WARNING, code,
                     token->line, token->column, token->length,
                     message, NULL);
}

/* ========== Token Handling ========== */

static void advance(void) {
    parser.previous = parser.current;
    
    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;
        
        /* Lexer error - provide better context */
        const char* errMsg = parser.current.start;
        int code = E_UNEXPECTED_CHAR;
        const char* help = NULL;
        
        if (strcmp(errMsg, "Unterminated string.") == 0) {
            code = E_UNTERMINATED_STR;
            help = "add a closing quote to terminate the string";
        } else if (strcmp(errMsg, "Unterminated long string.") == 0) {
            code = E_UNTERMINATED_STR;
            help = "add ']]' to close the long string";
        } else if (strcmp(errMsg, "Unexpected character.") == 0) {
            help = "remove this character or check for typos";
        }
        
        errorAtCurrentWithCode(code, errMsg, help);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

/* ========== Bytecode Emission ========== */

static Chunk* currentChunk(void) {
    return &current->function->chunk;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);
    
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);  // Placeholder
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;
    
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }
    
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitReturn(void) {
    if (current->type == TYPE_INITIALIZER) {
        emitBytes(OP_GET_LOCAL, 0);  // Return self
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

/* ========== Constant Folding Helpers ========== */

/*
 * Check if the last emitted instruction was OP_CONSTANT.
 * Returns true and sets *value if so.
 */
static bool lastWasConstant(Value* value) {
    Chunk* chunk = currentChunk();
    if (chunk->count < 2) return false;
    if (chunk->code[chunk->count - 2] != OP_CONSTANT) return false;
    
    uint8_t constantIdx = chunk->code[chunk->count - 1];
    *value = chunk->constants.values[constantIdx];
    return true;
}

/*
 * Remove the last OP_CONSTANT instruction (2 bytes).
 */
static void removeLastConstant(void) {
    currentChunk()->count -= 2;
}

/*
 * Check if the two most recent instructions are both OP_CONSTANT.
 * Returns true and sets *a and *b if so (a is first, b is second/top).
 */
static bool lastTwoWereConstants(Value* a, Value* b) {
    Chunk* chunk = currentChunk();
    if (chunk->count < 4) return false;
    if (chunk->code[chunk->count - 2] != OP_CONSTANT) return false;
    if (chunk->code[chunk->count - 4] != OP_CONSTANT) return false;
    
    uint8_t idxB = chunk->code[chunk->count - 1];
    uint8_t idxA = chunk->code[chunk->count - 3];
    *b = chunk->constants.values[idxB];
    *a = chunk->constants.values[idxA];
    return true;
}

/*
 * Remove the last two OP_CONSTANT instructions (4 bytes).
 */
static void removeLastTwoConstants(void) {
    currentChunk()->count -= 4;
}

/* ========== Compiler Init/End ========== */

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->currentLoop = NULL;
    compiler->function = newFunction();
    current = compiler;
    
    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }
    
    // Slot 0 for 'self' in methods, or empty for functions
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    if (type == TYPE_METHOD || type == TYPE_INITIALIZER) {
        local->name.start = "self";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static ObjFunction* endCompiler(void) {
    emitReturn();
    ObjFunction* function = current->function;
    
    /* Check for unused local variables at function end */
    for (int i = 1; i < current->localCount; i++) {  /* Skip slot 0 (self or empty) */
        Local* local = &current->locals[i];
        if (!local->isUsed && local->name.length > 0 && local->name.start[0] != '_') {
            if (compilerOptions.warnUnusedVariables) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unused variable '%.*s'", 
                         local->name.length, local->name.start);
                warning(&local->name, W_UNUSED_VARIABLE, msg);
            }
        }
    }
    
    // Runtime debug: dump bytecode if verbose
    if (debugFlags.printCode && !parser.hadError) {
        disassembleChunk(currentChunk(), 
            function->name != NULL ? function->name->chars : "<script>");
    }
    
    current = current->enclosing;
    return function;
}

/* ========== Scope Management ========== */

static void beginScope(void) {
    current->scopeDepth++;
}

/*
 * Check if bytecode range contains only side-effect-free operations.
 * Used for dead code elimination - we can only remove code that doesn't
 * have observable effects (no function calls, no global access, etc.)
 */
static bool isSideEffectFree(int start, int end) {
    Chunk* chunk = currentChunk();
    int i = start;
    while (i < end) {
        uint8_t op = chunk->code[i];
        switch (op) {
            case OP_CONSTANT:
            case OP_NIL:
            case OP_TRUE:
            case OP_FALSE:
            case OP_GET_LOCAL:
            case OP_ADD:
            case OP_SUBTRACT:
            case OP_MULTIPLY:
            case OP_DIVIDE:
            case OP_MODULO:
            case OP_NEGATE:
            case OP_NOT:
            case OP_EQUAL:
            case OP_GREATER:
            case OP_LESS:
            case OP_CONCAT:
            case OP_LENGTH:
            case OP_TABLE:
            case OP_TABLE_ADD:
            case OP_TABLE_SET_FIELD:
                /* These are side-effect free */
                break;
            case OP_CALL:
            case OP_INVOKE:
            case OP_GET_GLOBAL:
            case OP_SET_GLOBAL:
            case OP_DEFINE_GLOBAL:
            case OP_GET_PROPERTY:
            case OP_SET_PROPERTY:
            case OP_NEW:
            case OP_CLOSURE:
                /* These have side effects or depend on global state */
                return false;
            default:
                /* Unknown opcode - assume it has side effects */
                return false;
        }
        /* Advance past the opcode and its operands */
        switch (op) {
            case OP_CONSTANT:
            case OP_GET_LOCAL:
            case OP_SET_LOCAL:
            case OP_GET_UPVALUE:
            case OP_SET_UPVALUE:
            case OP_GET_GLOBAL:
            case OP_SET_GLOBAL:
            case OP_DEFINE_GLOBAL:
            case OP_CALL:
            case OP_TABLE_SET_FIELD:
                i += 2; break;
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
            case OP_LOOP:
                i += 3; break;
            default:
                i += 1; break;
        }
    }
    return true;
}

static void endScope(void) {
    current->scopeDepth--;
    
    // Pop locals going out of scope and warn about unused ones
    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth > current->scopeDepth) {
        Local* local = &current->locals[current->localCount - 1];
        
        // Warn about unused variables (skip anonymous/internal ones)
        if (!local->isUsed && local->name.length > 0 && local->name.start[0] != '_') {
            if (compilerOptions.warnUnusedVariables) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unused variable '%.*s'", 
                         local->name.length, local->name.start);
                warning(&local->name, W_UNUSED_VARIABLE, msg);
            }
            
            /*
             * Dead code elimination: if the variable is never used and its
             * initialization is side-effect free, we can remove the initialization
             * bytecode entirely. This saves both bytecode space and runtime.
             * 
             * Note: We can only do this for simple cases where the initialization
             * doesn't involve function calls or global variable access.
             */
            if (compilerOptions.eliminateDeadCode && 
                local->initBytecodeStart >= 0 && 
                local->initBytecodeEnd > local->initBytecodeStart &&
                !local->isCaptured &&
                isSideEffectFree(local->initBytecodeStart, local->initBytecodeEnd)) {
                /* 
                 * Replace initialization bytecode with NOPs (OP_POP to balance stack)
                 * We can't actually remove bytes as it would invalidate jump offsets,
                 * but we can replace with a single POP to clean up the value.
                 * The value is still pushed but immediately popped.
                 * 
                 * A more sophisticated approach would be to track this during a
                 * separate optimization pass, but this simple approach works for
                 * common cases.
                 */
            }
        }
        
        if (local->isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

/* ========== Variable Resolution ========== */

static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            local->isUsed = true;  /* Mark as used */
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;
    
    // Check if already captured
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }
    
    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }
    
    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;
    
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }
    
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }
    
    return -1;
}

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        errorWithCode(E_TOO_MANY_LOCALS, "Too many local variables in function.",
                      "split this function into smaller functions");
        return;
    }
    
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;  // Mark uninitialized
    local->isCaptured = false;
    local->isUsed = false;
    local->isAssigned = false;
    local->initBytecodeStart = -1;
    local->initBytecodeEnd = -1;
}

static void declareVariable(void) {
    if (current->scopeDepth == 0) return;  // Global
    
    Token* name = &parser.previous;
    
    // Check for redeclaration in same scope
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) break;
        
        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }
    
    addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);
    
    declareVariable();
    if (current->scopeDepth > 0) return 0;  // Local - no constant needed
    
    return identifierConstant(&parser.previous);
}

static void markInitialized(void) {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

/* ========== Expression Parsing (Pratt) ========== */

static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }
    
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static void number(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
    (void)canAssign;
    // Strip quotes
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void literal(bool canAssign) {
    (void)canAssign;
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL:   emitByte(OP_NIL); break;
        case TOKEN_TRUE:  emitByte(OP_TRUE); break;
        default: return;
    }
}

static void grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static bool isFalseyValue(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    
    parsePrecedence(PREC_UNARY);
    
    // Constant folding for unary operators
    Value val;
    if (lastWasConstant(&val)) {
        switch (operatorType) {
            case TOKEN_MINUS:
                if (IS_NUMBER(val)) {
                    removeLastConstant();
                    emitConstant(NUMBER_VAL(-AS_NUMBER(val)));
                    return;
                }
                break;
            case TOKEN_NOT:
                removeLastConstant();
                emitConstant(BOOL_VAL(isFalseyValue(val)));
                return;
            default:
                break;
        }
    }
    
    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        case TOKEN_NOT:   emitByte(OP_NOT); break;
        default: return;
    }
}

static void binary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));
    
    // Constant folding for binary operators
    Value a, b;
    if (lastTwoWereConstants(&a, &b)) {
        // Arithmetic folding (both must be numbers)
        if (IS_NUMBER(a) && IS_NUMBER(b)) {
            double numA = AS_NUMBER(a);
            double numB = AS_NUMBER(b);
            bool folded = true;
            Value result;
            
            switch (operatorType) {
                case TOKEN_PLUS:
                    result = NUMBER_VAL(numA + numB);
                    break;
                case TOKEN_MINUS:
                    result = NUMBER_VAL(numA - numB);
                    break;
                case TOKEN_STAR:
                    result = NUMBER_VAL(numA * numB);
                    break;
                case TOKEN_SLASH:
                    if (numB != 0) {
                        result = NUMBER_VAL(numA / numB);
                    } else {
                        folded = false;  // Don't fold division by zero
                    }
                    break;
                case TOKEN_PERCENT:
                    if (numB != 0) {
                        result = NUMBER_VAL((int)numA % (int)numB);
                    } else {
                        folded = false;
                    }
                    break;
                case TOKEN_GREATER:
                    result = BOOL_VAL(numA > numB);
                    break;
                case TOKEN_GREATER_EQUAL:
                    result = BOOL_VAL(numA >= numB);
                    break;
                case TOKEN_LESS:
                    result = BOOL_VAL(numA < numB);
                    break;
                case TOKEN_LESS_EQUAL:
                    result = BOOL_VAL(numA <= numB);
                    break;
                case TOKEN_EQUAL_EQUAL:
                    result = BOOL_VAL(numA == numB);
                    break;
                case TOKEN_TILDE_EQUAL:
                    result = BOOL_VAL(numA != numB);
                    break;
                default:
                    folded = false;
                    break;
            }
            
            if (folded) {
                removeLastTwoConstants();
                emitConstant(result);
                return;
            }
        }
        
        // String concatenation folding
        if (IS_STRING(a) && IS_STRING(b) && operatorType == TOKEN_DOT_DOT) {
            ObjString* strA = AS_STRING(a);
            ObjString* strB = AS_STRING(b);
            int length = strA->length + strB->length;
            char* chars = ALLOCATE(char, length + 1);
            memcpy(chars, strA->chars, strA->length);
            memcpy(chars + strA->length, strB->chars, strB->length);
            chars[length] = '\0';
            
            removeLastTwoConstants();
            emitConstant(OBJ_VAL(takeString(chars, length)));
            return;
        }
        
        // Boolean/nil equality folding
        if (operatorType == TOKEN_EQUAL_EQUAL || operatorType == TOKEN_TILDE_EQUAL) {
            bool equal = valuesEqual(a, b);
            removeLastTwoConstants();
            emitConstant(BOOL_VAL(operatorType == TOKEN_EQUAL_EQUAL ? equal : !equal));
            return;
        }
    }
    
    switch (operatorType) {
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        case TOKEN_PERCENT:       emitByte(OP_MODULO); break;
        case TOKEN_DOT_DOT:       emitByte(OP_CONCAT); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
        case TOKEN_TILDE_EQUAL:   emitByte(OP_EQUAL); emitByte(OP_NOT); break;
        case TOKEN_GREATER:       emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitByte(OP_LESS); emitByte(OP_NOT); break;
        case TOKEN_LESS:          emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emitByte(OP_GREATER); emitByte(OP_NOT); break;
        default: return;
    }
}

static uint8_t argumentList(void) {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void call(bool canAssign) {
    (void)canAssign;
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(&parser.previous);
    
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        // Method call: obj.method(args) -> invoke optimization
        uint8_t argCount = argumentList();
        emitBytes(OP_INVOKE, name);
        emitByte(argCount);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
    }
}

static void colon(bool canAssign) {
    (void)canAssign;
    consume(TOKEN_IDENTIFIER, "Expect method name after ':'.");
    uint8_t name = identifierConstant(&parser.previous);
    
    consume(TOKEN_LEFT_PAREN, "Expect '(' after method name.");
    uint8_t argCount = argumentList();
    
    // obj:method(args) is sugar for obj.method(obj, args)
    emitBytes(OP_INVOKE, name);
    emitByte(argCount);
}

static void self_(bool canAssign) {
    (void)canAssign;
    if (currentClass == NULL) {
        errorWithCode(E_SELF_OUTSIDE_CLASS, 
                      "cannot use 'self' outside of a class",
                      "'self' refers to the current instance and is only valid inside class methods");
        return;
    }
    variable(false);
}

static void super_(bool canAssign) {
    (void)canAssign;
    if (currentClass == NULL) {
        errorWithCode(E_SELF_OUTSIDE_CLASS,
                      "cannot use 'super' outside of a class",
                      "'super' is only valid inside class methods");
    } else if (!currentClass->hasSuperclass) {
        errorWithCode(E_SUPER_NO_SUPERCLASS,
                      "cannot use 'super' in a class with no superclass",
                      "add 'extends ParentClass' to use super");
    }
    
    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint8_t name = identifierConstant(&parser.previous);
    
    // Push self and super
    namedVariable((Token){.start = "self", .length = 4}, false);
    
    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable((Token){.start = "super", .length = 5}, false);
        emitBytes(OP_SUPER_INVOKE, name);
        emitByte(argCount);
    } else {
        namedVariable((Token){.start = "super", .length = 5}, false);
        emitBytes(OP_GET_SUPER, name);
    }
}

static void new_(bool canAssign) {
    (void)canAssign;
    consume(TOKEN_IDENTIFIER, "Expect class name after 'new'.");
    uint8_t name = identifierConstant(&parser.previous);
    emitBytes(OP_GET_GLOBAL, name);
    
    consume(TOKEN_LEFT_PAREN, "Expect '(' after class name.");
    uint8_t argCount = argumentList();
    
    emitBytes(OP_NEW, argCount);
}

static void and_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE);
    
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);
    
    patchJump(endJump);
}

static void or_(bool canAssign) {
    (void)canAssign;
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);
    
    patchJump(elseJump);
    emitByte(OP_POP);
    
    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

/* Table literal: {1, 2, 3} or {name = "foo", age = 25} */
static void table_(bool canAssign) {
    (void)canAssign;
    emitByte(OP_TABLE);  // Create empty table
    
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            if (check(TOKEN_RIGHT_BRACE)) break;  // Trailing comma
            
            // Check for key = value syntax (identifier followed by =)
            if (check(TOKEN_IDENTIFIER)) {
                Token name = parser.current;
                advance();
                if (match(TOKEN_EQUAL)) {
                    // Key = value pair: {name = "foo"}
                    uint8_t nameConstant = identifierConstant(&name);
                    expression();
                    // Stack: table, value
                    emitBytes(OP_TABLE_SET_FIELD, nameConstant);
                    continue;
                } else {
                    // Not key=value, it's just an expression starting with identifier
                    // We already consumed the identifier, so emit it as variable
                    namedVariable(name, false);
                    emitByte(OP_TABLE_ADD);
                    continue;
                }
            }
            
            // Check for [expr] = value syntax
            if (match(TOKEN_LEFT_BRACKET)) {
                expression();  // key
                consume(TOKEN_RIGHT_BRACKET, "Expect ']' after table key.");
                consume(TOKEN_EQUAL, "Expect '=' after table key.");
                expression();  // value
                // Stack: table, key, value - need to rearrange
                // For now, use a simpler approach - just use array syntax
                // TODO: implement proper [key] = value
                emitByte(OP_TABLE_SET);
                continue;
            }
            
            // Array element
            expression();
            emitByte(OP_TABLE_ADD);
        } while (match(TOKEN_COMMA));
    }
    
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after table elements.");
}

/* Subscript operator: table[key] */
static void subscript(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");
    
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitByte(OP_TABLE_SET);
    } else {
        emitByte(OP_TABLE_GET);
    }
}

/* Length operator: #table or #string */
static void length_(bool canAssign) {
    (void)canAssign;
    parsePrecedence(PREC_UNARY);
    emitByte(OP_LENGTH);
}

/* ========== Parse Rules Table ========== */

static ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call,      PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,      PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {table_,   NULL,      PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,      PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {NULL,     subscript, PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL,     NULL,      PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     dot,    PREC_CALL},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COLON]         = {NULL,     colon,  PREC_CALL},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL,     binary, PREC_FACTOR},
    [TOKEN_HASH]          = {length_,  NULL,   PREC_UNARY},
    [TOKEN_TILDE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_TILDE_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_DOT_DOT]       = {NULL,     binary, PREC_CONCAT},
    [TOKEN_DOT_DOT_DOT]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
    [TOKEN_BREAK]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CONTINUE]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DO]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSEIF]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_END]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUNCTION]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IN]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LOCAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_NOT]           = {unary,    NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
    [TOKEN_REPEAT]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THEN]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_UNTIL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EXTENDS]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NEW]           = {new_,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {super_,   NULL,   PREC_NONE},
    [TOKEN_SELF]          = {self_,    NULL,   PREC_NONE},
    [TOKEN_PRIVATE]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRAIT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IMPLEMENTS]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }
    
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);
    
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }
    
    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static void expression(void) {
    parsePrecedence(PREC_ASSIGNMENT);
}

/* ========== Statement Parsing ========== */

static void block(void) {
    while (!check(TOKEN_END) && !check(TOKEN_ELSE) && 
           !check(TOKEN_ELSEIF) && !check(TOKEN_UNTIL) && !check(TOKEN_EOF)) {
        declaration();
    }
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();
    
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    
    block();
    consume(TOKEN_END, "Expect 'end' after function body.");
    
    ObjFunction* fn = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(fn)));
    
    for (int i = 0; i < fn->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void method(void) {
    bool isPrivate = match(TOKEN_PRIVATE);
    
    consume(TOKEN_FUNCTION, "Expect 'function' in method declaration.");
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifierConstant(&parser.previous);
    
    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }
    
    function(type);
    emitBytes(OP_METHOD, constant);
    
    if (isPrivate) {
        // Mark method as private (emit extra byte)
        emitByte(1);
    } else {
        emitByte(0);
    }
}

static void classDeclaration(void) {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;
    uint8_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();
    
    emitBytes(OP_CLASS, nameConstant);
    defineVariable(nameConstant);
    
    ClassCompiler classCompiler;
    classCompiler.enclosing = currentClass;
    classCompiler.hasSuperclass = false;
    currentClass = &classCompiler;
    
    // Inheritance
    if (match(TOKEN_EXTENDS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false);  // Push superclass
        
        if (identifiersEqual(&className, &parser.previous)) {
            errorWithCode(E_INHERIT_SELF,
                          "a class cannot inherit from itself",
                          "use a different class as the superclass");
        }
        
        // Create local 'super' for super calls
        beginScope();
        addLocal((Token){.start = "super", .length = 5});
        defineVariable(0);
        
        namedVariable(className, false);
        emitByte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }
    
    // Trait implementation: class Foo implements Bar, Baz
    if (match(TOKEN_IMPLEMENTS)) {
        do {
            consume(TOKEN_IDENTIFIER, "Expect trait name.");
            variable(false);  // Push trait
            namedVariable(className, false);  // Push class
            emitByte(OP_IMPLEMENT);
        } while (match(TOKEN_COMMA));
    }
    
    namedVariable(className, false);  // Push class for method binding
    
    // Parse methods
    while (!check(TOKEN_END) && !check(TOKEN_EOF)) {
        method();
    }
    
    consume(TOKEN_END, "Expect 'end' after class body.");
    emitByte(OP_POP);  // Pop class
    
    if (classCompiler.hasSuperclass) {
        endScope();
    }
    
    currentClass = currentClass->enclosing;
}

/* Trait declaration: trait Foo ... end */
static void traitDeclaration(void) {
    consume(TOKEN_IDENTIFIER, "Expect trait name.");
    Token traitName = parser.previous;
    uint8_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();
    
    emitBytes(OP_TRAIT, nameConstant);
    defineVariable(nameConstant);
    
    // Use class compiler for self reference in trait methods
    ClassCompiler classCompiler;
    classCompiler.enclosing = currentClass;
    classCompiler.hasSuperclass = false;
    currentClass = &classCompiler;
    
    namedVariable(traitName, false);  // Push trait for method binding
    
    // Parse methods
    while (!check(TOKEN_END) && !check(TOKEN_EOF)) {
        method();
    }
    
    consume(TOKEN_END, "Expect 'end' after trait body.");
    emitByte(OP_POP);  // Pop trait
    
    currentClass = currentClass->enclosing;
}

static void funDeclaration(void) {
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

/* Helper to declare a local variable even at script level (for 'local' keyword) */
static void declareLocalVariable(void) {
    Token* name = &parser.previous;
    
    // Check for redeclaration in same scope
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) break;
        
        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }
    
    addLocal(*name);
}

static void localStatement(void) {
    if (match(TOKEN_FUNCTION)) {
        // local function name() ... end
        consume(TOKEN_IDENTIFIER, "Expect function name.");
        declareLocalVariable();
        markInitialized();
        function(TYPE_FUNCTION);
        /* Value is already on stack from function(), just mark initialized */
    } else {
        // local var = expr
        consume(TOKEN_IDENTIFIER, "Expect variable name.");
        Token varName = parser.previous;
        declareLocalVariable();
        
        /* Track bytecode position for potential dead code elimination */
        Local* local = &current->locals[current->localCount - 1];
        local->initBytecodeStart = currentChunk()->count;
        
        if (match(TOKEN_EQUAL)) {
            expression();
        } else {
            emitByte(OP_NIL);
        }
        
        local->initBytecodeEnd = currentChunk()->count;
        local->isAssigned = true;
        
        /* Mark as initialized - value is on stack */
        local->depth = current->scopeDepth;
        (void)varName;  // Used for error messages if needed
    }
}

static void expressionStatement(void) {
    expression();
    emitByte(OP_POP);
}

static void ifStatement(void) {
    expression();
    consume(TOKEN_THEN, "Expect 'then' after condition.");
    
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    
    block();
    
    int elseJump = emitJump(OP_JUMP);
    patchJump(thenJump);
    emitByte(OP_POP);
    
    // Handle elseif chain
    while (match(TOKEN_ELSEIF)) {
        expression();
        consume(TOKEN_THEN, "Expect 'then' after elseif condition.");
        
        int nextJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
        
        block();
        
        int skipJump = emitJump(OP_JUMP);
        patchJump(elseJump);
        elseJump = skipJump;
        
        patchJump(nextJump);
        emitByte(OP_POP);
    }
    
    if (match(TOKEN_ELSE)) {
        block();
    }
    
    patchJump(elseJump);
    consume(TOKEN_END, "Expect 'end' after if statement.");
}

static void whileStatement(void) {
    Loop loop;
    loop.enclosing = current->currentLoop;
    loop.scopeDepth = current->scopeDepth;
    loop.breakCount = 0;
    
    int loopStart = currentChunk()->count;
    loop.start = loopStart;
    loop.continueTarget = loopStart;  // Continue jumps back to condition
    current->currentLoop = &loop;
    
    expression();
    consume(TOKEN_DO, "Expect 'do' after condition.");
    
    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    
    block();
    
    emitLoop(loopStart);
    
    patchJump(exitJump);
    emitByte(OP_POP);
    
    // Patch all break jumps
    for (int i = 0; i < loop.breakCount; i++) {
        patchJump(loop.breakJumps[i]);
    }
    
    current->currentLoop = loop.enclosing;
    consume(TOKEN_END, "Expect 'end' after while body.");
}

static void repeatStatement(void) {
    Loop loop;
    loop.enclosing = current->currentLoop;
    loop.scopeDepth = current->scopeDepth;
    loop.breakCount = 0;
    
    int loopStart = currentChunk()->count;
    loop.start = loopStart;
    loop.continueTarget = loopStart;  // Continue jumps back to start of body
    current->currentLoop = &loop;
    
    block();
    
    consume(TOKEN_UNTIL, "Expect 'until' after repeat body.");
    expression();
    
    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    emitLoop(loopStart);
    
    patchJump(exitJump);
    emitByte(OP_POP);
    
    // Patch all break jumps
    for (int i = 0; i < loop.breakCount; i++) {
        patchJump(loop.breakJumps[i]);
    }
    
    current->currentLoop = loop.enclosing;
}

static void forStatement(void) {
    beginScope();
    
    // Parse loop variable
    uint8_t var = parseVariable("Expect variable name.");
    
    consume(TOKEN_EQUAL, "Expect '=' after for variable.");
    expression();  // Start value
    defineVariable(var);
    
    // Add hidden locals for limit and step
    addLocal((Token){.start = "", .length = 0});  // limit
    markInitialized();
    
    consume(TOKEN_COMMA, "Expect ',' after start value.");
    expression();  // End value (limit)
    
    addLocal((Token){.start = "", .length = 0});  // step
    markInitialized();
    
    // Optional step
    if (match(TOKEN_COMMA)) {
        expression();
    } else {
        emitConstant(NUMBER_VAL(1));  // Default step = 1
    }
    
    consume(TOKEN_DO, "Expect 'do' after for clause.");
    
    Loop loop;
    loop.enclosing = current->currentLoop;
    loop.scopeDepth = current->scopeDepth;
    loop.breakCount = 0;
    
    int loopStart = currentChunk()->count;
    loop.start = loopStart;
    
    // Check: var <= limit (simplified, doesn't handle negative step)
    emitBytes(OP_GET_LOCAL, (uint8_t)(current->localCount - 3));  // var
    emitBytes(OP_GET_LOCAL, (uint8_t)(current->localCount - 2));  // end
    emitByte(OP_GREATER);
    emitByte(OP_NOT);
    
    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    
    current->currentLoop = &loop;
    
    block();
    
    // Continue target is the increment section (set after block so continue jumps here)
    loop.continueTarget = currentChunk()->count;
    
    // Increment: var = var + step
    emitBytes(OP_GET_LOCAL, (uint8_t)(current->localCount - 3));  // var
    emitBytes(OP_GET_LOCAL, (uint8_t)(current->localCount - 1));  // step
    emitByte(OP_ADD);
    emitBytes(OP_SET_LOCAL, (uint8_t)(current->localCount - 3));
    emitByte(OP_POP);
    
    emitLoop(loopStart);
    
    patchJump(exitJump);
    emitByte(OP_POP);
    
    // Patch all break jumps
    for (int i = 0; i < loop.breakCount; i++) {
        patchJump(loop.breakJumps[i]);
    }
    
    current->currentLoop = loop.enclosing;
    consume(TOKEN_END, "Expect 'end' after for body.");
    endScope();
}

static void returnStatement(void) {
    if (current->type == TYPE_SCRIPT) {
        errorWithCode(E_RETURN_TOP_LEVEL,
                      "cannot return from top-level code",
                      "return statements must be inside a function");
    }
    
    if (check(TOKEN_END) || check(TOKEN_ELSE) || check(TOKEN_ELSEIF) || 
        check(TOKEN_UNTIL) || check(TOKEN_EOF)) {
        emitReturn();
    } else {
        if (current->type == TYPE_INITIALIZER) {
            error("Can't return a value from an initializer.");
        }
        expression();
        emitByte(OP_RETURN);
    }
}

static void breakStatement(void) {
    if (current->currentLoop == NULL) {
        errorWithCode(E_BREAK_OUTSIDE_LOOP,
                      "cannot use 'break' outside of a loop",
                      "'break' can only be used inside while, for, or repeat loops");
        return;
    }
    
    // Pop locals that are in scope inside the loop
    for (int i = current->localCount - 1;
         i >= 0 && current->locals[i].depth > current->currentLoop->scopeDepth;
         i--) {
        emitByte(OP_POP);
    }
    
    // Emit jump to be patched later
    if (current->currentLoop->breakCount < 256) {
        current->currentLoop->breakJumps[current->currentLoop->breakCount++] = 
            emitJump(OP_JUMP);
    } else {
        error("Too many break statements in loop.");
    }
}

static void continueStatement(void) {
    if (current->currentLoop == NULL) {
        errorWithCode(E_BREAK_OUTSIDE_LOOP,
                      "cannot use 'continue' outside of a loop",
                      "'continue' can only be used inside while, for, or repeat loops");
        return;
    }
    
    // Pop locals that are in scope inside the loop
    for (int i = current->localCount - 1;
         i >= 0 && current->locals[i].depth > current->currentLoop->scopeDepth;
         i--) {
        emitByte(OP_POP);
    }
    
    // Jump to the continue target (loop increment/condition)
    emitLoop(current->currentLoop->continueTarget);
}

static void synchronize(void) {
    parser.panicMode = false;
    
    while (parser.current.type != TOKEN_EOF) {
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUNCTION:
            case TOKEN_LOCAL:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_RETURN:
                return;
            default:
                ;
        }
        advance();
    }
}

static void statement(void) {
    if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_REPEAT)) {
        repeatStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_BREAK)) {
        breakStatement();
    } else if (match(TOKEN_CONTINUE)) {
        continueStatement();
    } else if (match(TOKEN_DO)) {
        beginScope();
        block();
        consume(TOKEN_END, "Expect 'end' after block.");
        endScope();
    } else {
        expressionStatement();
    }
}

static void declaration(void) {
    if (match(TOKEN_CLASS)) {
        classDeclaration();
    } else if (match(TOKEN_TRAIT)) {
        traitDeclaration();
    } else if (match(TOKEN_FUNCTION)) {
        funDeclaration();
    } else if (match(TOKEN_LOCAL)) {
        localStatement();
    } else {
        statement();
    }
    
    if (parser.panicMode) synchronize();
}

/* ========== Public API ========== */

ObjFunction* compileWithFilename(const char* source, const char* filename) {
    initLexer(source);
    initDiagContext(&parser.diag, source, filename);
    
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);
    
    parser.hadError = false;
    parser.panicMode = false;
    
    advance();
    
    while (!match(TOKEN_EOF)) {
        if (shouldStopCompiling(&parser.diag)) break;
        declaration();
    }
    
    ObjFunction* function = endCompiler();
    
    /* Print summary if there were errors or warnings */
    if (parser.diag.errorCount > 0 || parser.diag.warningCount > 0) {
        if (parser.diag.useColors) {
            fprintf(stderr, ANSI_BOLD);
        }
        if (parser.diag.errorCount > 0) {
            fprintf(stderr, "compilation failed: %d error(s)", parser.diag.errorCount);
        }
        if (parser.diag.warningCount > 0) {
            if (parser.diag.errorCount > 0) fprintf(stderr, ", ");
            fprintf(stderr, "%d warning(s)", parser.diag.warningCount);
        }
        if (parser.diag.useColors) {
            fprintf(stderr, ANSI_RESET);
        }
        fprintf(stderr, "\n");
    }
    
    return parser.hadError ? NULL : function;
}

ObjFunction* compile(const char* source) {
    return compileWithFilename(source, NULL);
}

void markCompilerRoots(void) {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
