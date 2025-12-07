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
    
    /* Save current VM state */
    int savedFrameCount = vm.frameCount;
    Value* savedStackTop = vm.stackTop;
    
    /* Push the closure as the callee */
    push(OBJ_VAL(closure));
    
    /* Push arguments */
    for (int i = 0; i < argCount; i++) {
        push(args[i]);
    }
    
    /* Set up call frame */
    if (!call(closure, argCount)) {
        /* Restore state on failure */
        vm.stackTop = savedStackTop;
        vm.frameCount = savedFrameCount;
        if (result) *result = NIL_VAL;
        return false;
    }
    
    /* Run until we return to our frame */
    InterpretResult runResult = run();
    
    if (runResult != INTERPRET_OK) {
        /* Runtime error occurred */
        if (result) *result = NIL_VAL;
        return false;
    }
    
    /* Get the result from the stack */
    if (result) {
        /* After return, result should be on top of stack */
        if (vm.stackTop > savedStackTop) {
            *result = *(vm.stackTop - 1);
        } else {
            *result = NIL_VAL;
        }
    }
    
    return true;
}
