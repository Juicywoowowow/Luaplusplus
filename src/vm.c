/*
 * vm.c - Bytecode interpreter
 * 
 * Main execution loop: fetch-decode-execute cycle.
 * Handles function calls, closures, OOP dispatch, and GC triggers.
 */

#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

VM vm;

/* Forward declarations */
static InterpretResult run(void);
static void resetStack(void);

/* ========== Native Functions ========== */

static Value printNative(int argCount, Value* args) {
    for (int i = 0; i < argCount; i++) {
        printValue(args[i]);
        if (i < argCount - 1) printf("\t");
    }
    printf("\n");
    return NIL_VAL;
}

static Value readNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        // Remove trailing newline
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
            len--;
        }
        return OBJ_VAL(copyString(buffer, (int)len));
    }
    return NIL_VAL;
}

static Value typeNative(int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    
    Value value = args[0];
    const char* type;
    
    if (IS_NIL(value)) type = "nil";
    else if (IS_BOOL(value)) type = "boolean";
    else if (IS_NUMBER(value)) type = "number";
    else if (IS_STRING(value)) type = "string";
    else if (IS_FUNCTION(value) || IS_CLOSURE(value) || IS_NATIVE(value)) type = "function";
    else if (IS_CLASS(value)) type = "class";
    else if (IS_INSTANCE(value)) type = "instance";
    else type = "unknown";
    
    return OBJ_VAL(copyString(type, (int)strlen(type)));
}

static Value tonumberNative(int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    if (IS_NUMBER(args[0])) return args[0];
    if (IS_STRING(args[0])) {
        char* end;
        double num = strtod(AS_CSTRING(args[0]), &end);
        if (*end == '\0') return NUMBER_VAL(num);
    }
    return NIL_VAL;
}

static Value tostringNative(int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    
    char buffer[64];
    Value value = args[0];
    
    if (IS_STRING(value)) return value;
    if (IS_NUMBER(value)) {
        int len = snprintf(buffer, sizeof(buffer), "%g", AS_NUMBER(value));
        return OBJ_VAL(copyString(buffer, len));
    }
    if (IS_BOOL(value)) {
        return OBJ_VAL(copyString(AS_BOOL(value) ? "true" : "false", AS_BOOL(value) ? 4 : 5));
    }
    if (IS_NIL(value)) {
        return OBJ_VAL(copyString("nil", 3));
    }
    return OBJ_VAL(copyString("<object>", 8));
}

/* ========== Module System ========== */

/* Track loaded modules to avoid re-loading */
static Table loadedModules;
static bool modulesInitialized = false;

/* Read file contents */
static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) return NULL;
    
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    
    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

/*
 * require(moduleName) - Load and execute a Lua++ module
 * 
 * Searches for moduleName.luapp in:
 * 1. Current directory
 * 2. ./lib/ directory
 * 3. Standard library path
 * 
 * Returns the module's exports table.
 */
static Value requireNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        return NIL_VAL;
    }
    
    ObjString* moduleName = AS_STRING(args[0]);
    
    /* Initialize module cache if needed */
    if (!modulesInitialized) {
        initTable(&loadedModules);
        modulesInitialized = true;
    }
    
    /* Check if already loaded */
    Value cached;
    if (tableGet(&loadedModules, moduleName, &cached)) {
        return cached;
    }
    
    /* Try to find the module file */
    char path[512];
    char* source = NULL;
    
    /* Try: moduleName.luapp */
    snprintf(path, sizeof(path), "%s.luapp", moduleName->chars);
    source = readFile(path);
    
    /* Try: lib/moduleName.luapp */
    if (source == NULL) {
        snprintf(path, sizeof(path), "lib/%s.luapp", moduleName->chars);
        source = readFile(path);
    }
    
    /* Try: stdlib/moduleName.luapp */
    if (source == NULL) {
        snprintf(path, sizeof(path), "stdlib/%s.luapp", moduleName->chars);
        source = readFile(path);
    }
    
    if (source == NULL) {
        fprintf(stderr, "Module not found: %s\n", moduleName->chars);
        return NIL_VAL;
    }
    
    /* Create a table to hold module exports */
    ObjTable* exports = newTable();
    push(OBJ_VAL(exports));  /* GC protection */
    
    /* Store in cache before loading (handles circular deps) */
    tableSet(&loadedModules, moduleName, OBJ_VAL(exports));
    
    /* Save current globals state */
    /* The module will define its exports as globals, we'll capture them */
    
    /* Compile and run the module */
    ObjFunction* function = compileWithFilename(source, path);
    free(source);
    
    if (function == NULL) {
        pop();  /* Remove exports from stack */
        tableDelete(&loadedModules, moduleName);
        return NIL_VAL;
    }
    
    /* Run the module */
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    
    /* Module will run via run() which handles frame management */
    
    if (vm.frameCount == FRAMES_MAX) {
        pop();
        pop();
        return NIL_VAL;
    }
    
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - 1;
    
    /* Run until module completes */
    InterpretResult result = run();
    
    if (result != INTERPRET_OK) {
        pop();  /* Remove exports */
        return NIL_VAL;
    }
    
    /* Module executed - exports table is ready */
    pop();  /* Remove closure */
    Value exportsVal = pop();  /* Get exports */
    
    return exportsVal;
}

/* ========== Iterator Functions for for-in loops ========== */

/*
 * pairs(table) - Returns iterator function, table, nil
 * Used in: for k, v in pairs(t) do ... end
 */
static Value pairsNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_TABLE(args[0])) {
        return NIL_VAL;
    }
    /* Return the table itself - the VM handles iteration */
    return args[0];
}

/*
 * ipairs(table) - Returns iterator for array part
 * Used in: for i, v in ipairs(t) do ... end
 */
static Value ipairsNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_TABLE(args[0])) {
        return NIL_VAL;
    }
    return args[0];
}

/*
 * next(table, key) - Returns next key-value pair
 * If key is nil, returns first pair.
 */
static Value nextNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_TABLE(args[0])) {
        return NIL_VAL;
    }
    
    ObjTable* table = AS_TABLE(args[0]);
    Value key = (argCount > 1) ? args[1] : NIL_VAL;
    
    /* If key is nil, return first element */
    if (IS_NIL(key)) {
        /* Check array part first */
        if (table->array.count > 0) {
            /* Return key=1, value=array[0] as a table {1, value} */
            ObjTable* result = newTable();
            writeValueArray(&result->array, NUMBER_VAL(1));
            writeValueArray(&result->array, table->array.values[0]);
            return OBJ_VAL(result);
        }
        /* Check hash part */
        for (int i = 0; i < table->entries.capacity; i++) {
            Entry* entry = &table->entries.entries[i];
            if (entry->key != NULL) {
                ObjTable* result = newTable();
                writeValueArray(&result->array, OBJ_VAL(entry->key));
                writeValueArray(&result->array, entry->value);
                return OBJ_VAL(result);
            }
        }
        return NIL_VAL;
    }
    
    /* Find next after given key */
    if (IS_NUMBER(key)) {
        int idx = (int)AS_NUMBER(key);
        if (idx >= 1 && idx < table->array.count) {
            ObjTable* result = newTable();
            writeValueArray(&result->array, NUMBER_VAL(idx + 1));
            writeValueArray(&result->array, table->array.values[idx]);
            return OBJ_VAL(result);
        }
        /* Fall through to hash part */
    }
    
    /* Search hash part */
    bool found = false;
    for (int i = 0; i < table->entries.capacity; i++) {
        Entry* entry = &table->entries.entries[i];
        if (entry->key != NULL) {
            if (found) {
                ObjTable* result = newTable();
                writeValueArray(&result->array, OBJ_VAL(entry->key));
                writeValueArray(&result->array, entry->value);
                return OBJ_VAL(result);
            }
            if (IS_STRING(key) && entry->key == AS_STRING(key)) {
                found = true;
            }
        }
    }
    
    return NIL_VAL;
}

/*
 * error(message) - Raise a runtime error
 */
static Value errorNative(int argCount, Value* args) {
    if (argCount >= 1 && IS_STRING(args[0])) {
        fprintf(stderr, "error: %s\n", AS_CSTRING(args[0]));
    } else {
        fprintf(stderr, "error\n");
    }
    return NIL_VAL;
}

/*
 * assert(condition, message) - Assert condition is true
 */
static Value assertNative(int argCount, Value* args) {
    if (argCount < 1) return NIL_VAL;
    
    bool condition = !IS_NIL(args[0]) && !(IS_BOOL(args[0]) && !AS_BOOL(args[0]));
    
    if (!condition) {
        if (argCount >= 2 && IS_STRING(args[1])) {
            fprintf(stderr, "assertion failed: %s\n", AS_CSTRING(args[1]));
        } else {
            fprintf(stderr, "assertion failed\n");
        }
    }
    
    return args[0];
}

/*
 * rawget(table, key) - Get without metamethods
 */
static Value rawgetNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_TABLE(args[0])) return NIL_VAL;
    
    ObjTable* table = AS_TABLE(args[0]);
    Value key = args[1];
    
    if (IS_NUMBER(key)) {
        int idx = (int)AS_NUMBER(key);
        if (idx >= 1 && idx <= table->array.count) {
            return table->array.values[idx - 1];
        }
    }
    
    if (IS_STRING(key)) {
        Value value;
        if (tableGet(&table->entries, AS_STRING(key), &value)) {
            return value;
        }
    }
    
    return NIL_VAL;
}

/*
 * rawset(table, key, value) - Set without metamethods
 */
static Value rawsetNative(int argCount, Value* args) {
    if (argCount != 3 || !IS_TABLE(args[0])) return NIL_VAL;
    
    ObjTable* table = AS_TABLE(args[0]);
    Value key = args[1];
    Value value = args[2];
    
    if (IS_NUMBER(key)) {
        int idx = (int)AS_NUMBER(key);
        if (idx >= 1) {
            while (table->array.count < idx) {
                writeValueArray(&table->array, NIL_VAL);
            }
            table->array.values[idx - 1] = value;
            return args[0];
        }
    }
    
    if (IS_STRING(key)) {
        tableSet(&table->entries, AS_STRING(key), value);
    }
    
    return args[0];
}

static void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function, AS_STRING(vm.stack[0]))));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

/* ========== VM Initialization ========== */

static void resetStack(void) {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

void initVM(void) {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;  // First GC at 1MB
    
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    
    initTable(&vm.globals);
    initTable(&vm.strings);
    
    vm.initString = NULL;
    vm.initString = copyString("init", 4);
    
    // Register native functions
    defineNative("print", printNative);
    defineNative("read", readNative);
    defineNative("type", typeNative);
    defineNative("tonumber", tonumberNative);
    defineNative("tostring", tostringNative);
    
    // Module system
    defineNative("require", requireNative);
    
    // Iterators
    defineNative("pairs", pairsNative);
    defineNative("ipairs", ipairsNative);
    defineNative("next", nextNative);
    
    // Error handling
    defineNative("error", errorNative);
    defineNative("assert", assertNative);
    
    // Raw table access
    defineNative("rawget", rawgetNative);
    defineNative("rawset", rawsetNative);
}

void freeVM(void) {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    vm.initString = NULL;
    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop(void) {
    vm.stackTop--;
    return *vm.stackTop;
}

static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    
    // Stack trace
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    
    resetStack();
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(void) {
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));
    
    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';
    
    ObjString* result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

/* ========== Function Calls ========== */

static bool call(ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }
    
    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }
    
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
                
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm.stackTop[-argCount - 1] = bound->receiver;
                return call(bound->method, argCount);
            }
            
            default:
                break;
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined method '%s'.", name->chars);
        return false;
    }
    return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);
    
    if (!IS_INSTANCE(receiver)) {
        runtimeError("Only instances have methods.");
        return false;
    }
    
    ObjInstance* instance = AS_INSTANCE(receiver);
    
    // Check for field first (might be a function stored in field)
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }
    
    return invokeFromClass(instance->klass, name, argCount);
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
    
    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
    
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }
    
    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;
    
    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
    
    return createdUpvalue;
}

static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static void defineMethod(ObjString* name, bool isPrivate) {
    Value method = peek(0);
    Value target = peek(1);
    
    if (IS_CLASS(target)) {
        ObjClass* klass = AS_CLASS(target);
        tableSet(&klass->methods, name, method);
        if (isPrivate) {
            tableSet(&klass->privates, name, BOOL_VAL(true));
        }
    } else if (IS_TRAIT(target)) {
        ObjTrait* trait = AS_TRAIT(target);
        tableSet(&trait->methods, name, method);
    }
    pop();
}

/* ========== Main Execution Loop ========== */

static InterpretResult run(void) {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];
    
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
    } while (false)

    for (;;) {
        // Runtime debug: trace execution
        if (debugFlags.traceExecution) {
            printf("          ");
            for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
            disassembleInstruction(&frame->closure->function->chunk,
                (int)(frame->ip - frame->closure->function->chunk.code));
        }

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            
            case OP_NIL:   push(NIL_VAL); break;
            case OP_TRUE:  push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP:   pop(); break;
            
            case OP_POPN: {
                uint8_t n = READ_BYTE();
                vm.stackTop -= n;
                break;
            }
            
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            
            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(peek(0))) {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjInstance* instance = AS_INSTANCE(peek(0));
                ObjString* name = READ_STRING();
                
                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop();
                    push(value);
                    break;
                }
                
                if (!bindMethod(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(1))) {
                    runtimeError("Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjInstance* instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, READ_STRING(), peek(0));
                Value value = pop();
                pop();
                push(value);
                break;
            }
            
            case OP_GET_SUPER: {
                ObjString* name = READ_STRING();
                ObjClass* superclass = AS_CLASS(pop());
                
                if (!bindMethod(superclass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD:      BINARY_OP(NUMBER_VAL, +); break;
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            
            case OP_MODULO: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                    runtimeError("Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL((int)a % (int)b));
                break;
            }
            
            case OP_CONCAT: {
                if (!IS_STRING(peek(0)) || !IS_STRING(peek(1))) {
                    runtimeError("Operands must be strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                concatenate();
                break;
            }
            
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            
            case OP_LENGTH: {
                Value val = pop();
                if (IS_STRING(val)) {
                    push(NUMBER_VAL(AS_STRING(val)->length));
                } else if (IS_TABLE(val)) {
                    push(NUMBER_VAL(AS_TABLE(val)->array.count));
                } else {
                    runtimeError("Can only get length of string or table.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            
            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            
            case OP_INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                if (!invoke(method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            
            case OP_SUPER_INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                ObjClass* superclass = AS_CLASS(pop());
                if (!invokeFromClass(superclass, method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            
            case OP_RETURN: {
                Value result = pop();
                closeUpvalues(frame->slots);
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }
                
                vm.stackTop = frame->slots;
                push(result);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            
            case OP_CLASS:
                push(OBJ_VAL(newClass(READ_STRING())));
                break;
            
            case OP_INHERIT: {
                Value superclass = peek(1);
                if (!IS_CLASS(superclass)) {
                    runtimeError("Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjClass* subclass = AS_CLASS(peek(0));
                tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
                subclass->superclass = AS_CLASS(superclass);
                pop();
                break;
            }
            
            case OP_METHOD: {
                ObjString* name = READ_STRING();
                bool isPrivate = READ_BYTE();
                defineMethod(name, isPrivate);
                break;
            }
            
            case OP_NEW: {
                int argCount = READ_BYTE();
                Value klass = peek(argCount);
                
                if (!IS_CLASS(klass)) {
                    runtimeError("Can only instantiate classes.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjInstance* instance = newInstance(AS_CLASS(klass));
                vm.stackTop[-argCount - 1] = OBJ_VAL(instance);
                
                // Call init if it exists
                Value initializer;
                if (tableGet(&AS_CLASS(klass)->methods, vm.initString, &initializer)) {
                    if (!call(AS_CLOSURE(initializer), argCount)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    frame = &vm.frames[vm.frameCount - 1];
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_TABLE: {
                push(OBJ_VAL(newTable()));
                break;
            }
            
            case OP_TABLE_GET: {
                Value key = pop();
                Value tableVal = pop();
                
                if (!IS_TABLE(tableVal)) {
                    runtimeError("Can only index tables.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjTable* table = AS_TABLE(tableVal);
                
                // Integer key -> array access
                if (IS_NUMBER(key)) {
                    int index = (int)AS_NUMBER(key);
                    if (index >= 1 && index <= table->array.count) {
                        push(table->array.values[index - 1]);  // Lua is 1-indexed
                        break;
                    }
                }
                
                // String key -> hash access
                if (IS_STRING(key)) {
                    Value value;
                    if (tableGet(&table->entries, AS_STRING(key), &value)) {
                        push(value);
                        break;
                    }
                }
                
                push(NIL_VAL);  // Key not found
                break;
            }
            
            case OP_TABLE_SET: {
                Value value = pop();
                Value key = pop();
                Value tableVal = pop();
                
                if (!IS_TABLE(tableVal)) {
                    runtimeError("Can only index tables.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjTable* table = AS_TABLE(tableVal);
                
                // Integer key -> array access
                if (IS_NUMBER(key)) {
                    int index = (int)AS_NUMBER(key);
                    if (index >= 1) {
                        // Grow array if needed
                        while (table->array.count < index) {
                            writeValueArray(&table->array, NIL_VAL);
                        }
                        table->array.values[index - 1] = value;
                        push(value);
                        break;
                    }
                }
                
                // String key -> hash access
                if (IS_STRING(key)) {
                    tableSet(&table->entries, AS_STRING(key), value);
                    push(value);
                    break;
                }
                
                runtimeError("Table key must be a string or positive integer.");
                return INTERPRET_RUNTIME_ERROR;
            }
            
            case OP_TABLE_ADD: {
                // Add value to array part of table (for literal construction)
                Value value = pop();
                Value tableVal = peek(0);
                
                if (!IS_TABLE(tableVal)) {
                    runtimeError("Expected table.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjTable* table = AS_TABLE(tableVal);
                writeValueArray(&table->array, value);
                break;
            }
            
            case OP_TABLE_SET_FIELD: {
                // Set named field during table literal construction: {name = value}
                ObjString* name = READ_STRING();
                Value value = pop();
                Value tableVal = peek(0);
                
                if (!IS_TABLE(tableVal)) {
                    runtimeError("Expected table.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjTable* table = AS_TABLE(tableVal);
                tableSet(&table->entries, name, value);
                break;
            }
            
            case OP_TRAIT: {
                push(OBJ_VAL(newTrait(READ_STRING())));
                break;
            }
            
            case OP_IMPLEMENT: {
                // Stack: trait, class (class on top)
                Value classVal = pop();
                Value traitVal = pop();
                
                if (!IS_TRAIT(traitVal)) {
                    runtimeError("Can only implement traits.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_CLASS(classVal)) {
                    runtimeError("Only classes can implement traits.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjTrait* trait = AS_TRAIT(traitVal);
                ObjClass* klass = AS_CLASS(classVal);
                
                // Copy all methods from trait to class
                tableAddAll(&trait->methods, &klass->methods);
                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
    return interpretWithFilename(source, NULL);
}

InterpretResult interpretWithFilename(const char* source, const char* filename) {
    ObjFunction* function = compileWithFilename(source, filename);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
    
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);
    
    return run();
}

/*
 * Call a Lua++ closure from C code.
 * This allows external code (like the Lua interop layer) to invoke
 * Lua++ functions and get results back.
 * 
 * Arguments are passed in the args array (argCount elements).
 * The result is stored in *result if not NULL.
 * Returns true on success, false on runtime error.
 */
bool callClosure(ObjClosure* closure, int argCount, Value* args, Value* result) {
    /* Check arity */
    if (argCount != closure->function->arity) {
        if (result) *result = NIL_VAL;
        return false;
    }
    
    /* Save the frame count - we'll run until we return to this level */
    int baseFrameCount = vm.frameCount;
    
    /* Push the closure as the callee */
    push(OBJ_VAL(closure));
    
    /* Push arguments */
    for (int i = 0; i < argCount; i++) {
        push(args[i]);
    }
    
    /* Set up call frame */
    if (!call(closure, argCount)) {
        /* Pop the closure and args on failure */
        vm.stackTop -= argCount + 1;
        if (result) *result = NIL_VAL;
        return false;
    }
    
    /* Run the VM - it will execute until OP_RETURN brings frameCount back */
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

/* Local macros for bytecode reading */
#define CC_READ_BYTE() (*frame->ip++)
#define CC_READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define CC_READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[CC_READ_BYTE()])
#define CC_READ_STRING() AS_STRING(CC_READ_CONSTANT())
#define CC_FAIL() do { if (result) *result = NIL_VAL; return false; } while(0)

    for (;;) {
        uint8_t instruction = CC_READ_BYTE();
        
        switch (instruction) {
            case OP_RETURN: {
                Value returnValue = pop();
                closeUpvalues(frame->slots);
                vm.frameCount--;
                
                if (vm.frameCount <= baseFrameCount) {
                    /* We've returned from the called function */
                    vm.stackTop = frame->slots;
                    if (result) *result = returnValue;
                    return true;
                }
                
                vm.stackTop = frame->slots;
                push(returnValue);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            
            case OP_CONSTANT: {
                Value constant = CC_READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL:   push(NIL_VAL); break;
            case OP_TRUE:  push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP:   pop(); break;
            
            case OP_GET_LOCAL: {
                uint8_t slot = CC_READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = CC_READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = CC_READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    CC_FAIL();
                }
                push(value);
                break;
            }
            
            case OP_ADD: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) CC_FAIL();
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL(a + b));
                break;
            }
            case OP_SUBTRACT: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) CC_FAIL();
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL(a - b));
                break;
            }
            case OP_MULTIPLY: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) CC_FAIL();
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL(a * b));
                break;
            }
            case OP_DIVIDE: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) CC_FAIL();
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL(a / b));
                break;
            }
            case OP_NEGATE: {
                if (!IS_NUMBER(peek(0))) CC_FAIL();
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            }
            case OP_NOT: {
                push(BOOL_VAL(isFalsey(pop())));
                break;
            }
            
            case OP_LESS: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) CC_FAIL();
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(BOOL_VAL(a < b));
                break;
            }
            case OP_GREATER: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) CC_FAIL();
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(BOOL_VAL(a > b));
                break;
            }
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            
            case OP_JUMP: {
                uint16_t offset = CC_READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = CC_READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = CC_READ_SHORT();
                frame->ip -= offset;
                break;
            }
            
            case OP_CALL: {
                int callArgCount = CC_READ_BYTE();
                if (!callValue(peek(callArgCount), callArgCount)) {
                    CC_FAIL();
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            
            case OP_CONCAT: {
                if (!IS_STRING(peek(0)) || !IS_STRING(peek(1))) CC_FAIL();
                ObjString* b = AS_STRING(peek(0));
                ObjString* a = AS_STRING(peek(1));
                int length = a->length + b->length;
                char* chars = ALLOCATE(char, length + 1);
                memcpy(chars, a->chars, a->length);
                memcpy(chars + a->length, b->chars, b->length);
                chars[length] = '\0';
                ObjString* str = takeString(chars, length);
                pop();
                pop();
                push(OBJ_VAL(str));
                break;
            }
            
            case OP_POPN: {
                uint8_t n = CC_READ_BYTE();
                vm.stackTop -= n;
                break;
            }
            
            /* ========== Upvalues and Closures ========== */
            
            case OP_GET_UPVALUE: {
                uint8_t slot = CC_READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            
            case OP_SET_UPVALUE: {
                uint8_t slot = CC_READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            
            case OP_CLOSE_UPVALUE: {
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            }
            
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(CC_READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = CC_READ_BYTE();
                    uint8_t index = CC_READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            
            /* ========== Global Variables ========== */
            
            case OP_SET_GLOBAL: {
                ObjString* name = CC_READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    CC_FAIL();
                }
                break;
            }
            
            case OP_DEFINE_GLOBAL: {
                ObjString* name = CC_READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            
            /* ========== OOP Operations ========== */
            
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(peek(0))) CC_FAIL();
                ObjInstance* instance = AS_INSTANCE(peek(0));
                ObjString* name = CC_READ_STRING();
                
                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop();
                    push(value);
                    break;
                }
                
                if (!bindMethod(instance->klass, name)) {
                    CC_FAIL();
                }
                break;
            }
            
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(1))) CC_FAIL();
                ObjInstance* instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, CC_READ_STRING(), peek(0));
                Value value = pop();
                pop();
                push(value);
                break;
            }
            
            case OP_INVOKE: {
                ObjString* method = CC_READ_STRING();
                int invokeArgCount = CC_READ_BYTE();
                if (!invoke(method, invokeArgCount)) {
                    CC_FAIL();
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            
            case OP_NEW: {
                int newArgCount = CC_READ_BYTE();
                Value klassVal = peek(newArgCount);
                
                if (!IS_CLASS(klassVal)) CC_FAIL();
                
                ObjInstance* instance = newInstance(AS_CLASS(klassVal));
                vm.stackTop[-newArgCount - 1] = OBJ_VAL(instance);
                
                Value initializer;
                if (tableGet(&AS_CLASS(klassVal)->methods, vm.initString, &initializer)) {
                    if (!call(AS_CLOSURE(initializer), newArgCount)) {
                        CC_FAIL();
                    }
                    frame = &vm.frames[vm.frameCount - 1];
                } else if (newArgCount != 0) {
                    CC_FAIL();
                }
                break;
            }
            
            /* ========== Table Operations ========== */
            
            case OP_TABLE: {
                push(OBJ_VAL(newTable()));
                break;
            }
            
            case OP_TABLE_GET: {
                Value key = pop();
                Value tableVal = pop();
                
                if (!IS_TABLE(tableVal)) CC_FAIL();
                
                ObjTable* table = AS_TABLE(tableVal);
                
                if (IS_NUMBER(key)) {
                    int index = (int)AS_NUMBER(key);
                    if (index >= 1 && index <= table->array.count) {
                        push(table->array.values[index - 1]);
                        break;
                    }
                }
                
                if (IS_STRING(key)) {
                    Value value;
                    if (tableGet(&table->entries, AS_STRING(key), &value)) {
                        push(value);
                        break;
                    }
                }
                
                push(NIL_VAL);
                break;
            }
            
            case OP_TABLE_SET: {
                Value value = pop();
                Value key = pop();
                Value tableVal = pop();
                
                if (!IS_TABLE(tableVal)) CC_FAIL();
                
                ObjTable* table = AS_TABLE(tableVal);
                
                if (IS_NUMBER(key)) {
                    int index = (int)AS_NUMBER(key);
                    if (index >= 1) {
                        while (table->array.count < index) {
                            writeValueArray(&table->array, NIL_VAL);
                        }
                        table->array.values[index - 1] = value;
                        push(value);
                        break;
                    }
                }
                
                if (IS_STRING(key)) {
                    tableSet(&table->entries, AS_STRING(key), value);
                    push(value);
                    break;
                }
                
                CC_FAIL();
            }
            
            case OP_TABLE_ADD: {
                Value value = pop();
                Value tableVal = peek(0);
                
                if (!IS_TABLE(tableVal)) CC_FAIL();
                
                ObjTable* table = AS_TABLE(tableVal);
                writeValueArray(&table->array, value);
                break;
            }
            
            case OP_TABLE_SET_FIELD: {
                ObjString* name = CC_READ_STRING();
                Value value = pop();
                Value tableVal = peek(0);
                
                if (!IS_TABLE(tableVal)) CC_FAIL();
                
                ObjTable* table = AS_TABLE(tableVal);
                tableSet(&table->entries, name, value);
                break;
            }
            
            /* ========== Additional Operations ========== */
            
            case OP_MODULO: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) CC_FAIL();
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL((int)a % (int)b));
                break;
            }
            
            case OP_LENGTH: {
                Value val = pop();
                if (IS_STRING(val)) {
                    push(NUMBER_VAL(AS_STRING(val)->length));
                } else if (IS_TABLE(val)) {
                    push(NUMBER_VAL(AS_TABLE(val)->array.count));
                } else {
                    CC_FAIL();
                }
                break;
            }
            
            default:
                /* Unhandled opcode in mini-interpreter */
                CC_FAIL();
        }
    }

#undef CC_READ_BYTE
#undef CC_READ_SHORT
#undef CC_READ_CONSTANT
#undef CC_READ_STRING
#undef CC_FAIL
}
