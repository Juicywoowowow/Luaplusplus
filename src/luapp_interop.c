/*
 * luapp_interop.c - Lua ↔ Lua++ interoperability layer
 * 
 * This module allows standard Lua to load and execute Lua++ code,
 * marshalling values between the two runtimes.
 * 
 * Usage from Lua:
 *   local luapp = require("luapp")
 *   local result = luapp.eval("return 1 + 2")
 *   local mod = luapp.load("mymodule.luapp")
 *   
 *   -- Instantiate Lua++ classes
 *   local obj = luapp.new(mod.MyClass, arg1, arg2)
 *   
 *   -- Pass Lua functions to Lua++
 *   mod.callback = function(x) return x * 2 end
 */

#include "luapp_interop.h"
#include "vm.h"
#include "compiler.h"
#include "memory.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Define debugFlags since main.c is not included in the shared library */
DebugFlags debugFlags = {false, false, false};

/* Track if Lua++ VM is initialized */
static bool vmInitialized = false;

/* Global Lua state for reverse calls (Lua++ calling Lua) */
static lua_State* globalLuaState = NULL;

/* Forward declarations */
static void tableToLua(lua_State* L, ObjTable* table);
static void classToLua(lua_State* L, ObjClass* klass);
static void instanceToLua(lua_State* L, ObjInstance* instance);
static void closureToLua(lua_State* L, ObjClosure* closure);
static Value tableFromLua(lua_State* L, int idx);
static Value functionFromLua(lua_State* L, int idx);

/* ========== Lua++ Value → Lua ========== */

void luappToLua(lua_State* L, Value val) {
    if (IS_NIL(val)) {
        lua_pushnil(L);
    }
    else if (IS_BOOL(val)) {
        lua_pushboolean(L, AS_BOOL(val));
    }
    else if (IS_NUMBER(val)) {
        lua_pushnumber(L, AS_NUMBER(val));
    }
    else if (IS_STRING(val)) {
        ObjString* str = AS_STRING(val);
        lua_pushlstring(L, str->chars, str->length);
    }
    else if (IS_TABLE(val)) {
        tableToLua(L, AS_TABLE(val));
    }
    else if (IS_CLASS(val)) {
        classToLua(L, AS_CLASS(val));
    }
    else if (IS_INSTANCE(val)) {
        instanceToLua(L, AS_INSTANCE(val));
    }
    else if (IS_CLOSURE(val)) {
        closureToLua(L, AS_CLOSURE(val));
    }
    else if (IS_FUNCTION(val)) {
        /* Raw functions shouldn't appear at runtime, but handle anyway */
        lua_pushnil(L);
    }
    else if (IS_NATIVE(val)) {
        /* Native functions - push as nil for now */
        lua_pushnil(L);
    }
    else {
        lua_pushnil(L);
    }
}

/*
 * Convert Lua++ table to Lua table.
 * Handles both array and hash parts.
 */
static void tableToLua(lua_State* L, ObjTable* table) {
    lua_newtable(L);
    
    /* Array part (1-indexed) */
    for (int i = 0; i < table->array.count; i++) {
        lua_pushinteger(L, i + 1);
        luappToLua(L, table->array.values[i]);
        lua_settable(L, -3);
    }
    
    /* Hash part */
    for (int i = 0; i < table->entries.capacity; i++) {
        Entry* entry = &table->entries.entries[i];
        if (entry->key != NULL) {
            lua_pushlstring(L, entry->key->chars, entry->key->length);
            luappToLua(L, entry->value);
            lua_settable(L, -3);
        }
    }
}

/*
 * Convert Lua++ class to Lua table with constructor.
 * 
 * Lua++ class becomes:
 * {
 *   __luapp_class = <userdata>,
 *   __name = "ClassName",
 *   new = function(...) ... end,
 *   <methods...>
 * }
 */
static void classToLua(lua_State* L, ObjClass* klass) {
    lua_newtable(L);
    
    /* Store class name */
    lua_pushstring(L, "__name");
    lua_pushlstring(L, klass->name->chars, klass->name->length);
    lua_settable(L, -3);
    
    /* Store reference to original class as light userdata */
    lua_pushstring(L, "__luapp_class");
    lua_pushlightuserdata(L, klass);
    lua_settable(L, -3);
    
    /* Copy methods as Lua functions */
    for (int i = 0; i < klass->methods.capacity; i++) {
        Entry* entry = &klass->methods.entries[i];
        if (entry->key != NULL) {
            lua_pushlstring(L, entry->key->chars, entry->key->length);
            luappToLua(L, entry->value);
            lua_settable(L, -3);
        }
    }
}

/*
 * Convert Lua++ instance to Lua table.
 * 
 * Instance becomes:
 * {
 *   __luapp_instance = <userdata>,
 *   __class = <class table>,
 *   <fields...>
 * }
 * With metatable pointing to class methods.
 */
static void instanceToLua(lua_State* L, ObjInstance* instance) {
    lua_newtable(L);
    
    /* Store reference to original instance */
    lua_pushstring(L, "__luapp_instance");
    lua_pushlightuserdata(L, instance);
    lua_settable(L, -3);
    
    /* Copy fields */
    for (int i = 0; i < instance->fields.capacity; i++) {
        Entry* entry = &instance->fields.entries[i];
        if (entry->key != NULL) {
            lua_pushlstring(L, entry->key->chars, entry->key->length);
            luappToLua(L, entry->value);
            lua_settable(L, -3);
        }
    }
    
    /* Create metatable with __index pointing to class methods */
    lua_newtable(L);  /* metatable */
    lua_pushstring(L, "__index");
    classToLua(L, instance->klass);
    lua_settable(L, -3);
    lua_setmetatable(L, -2);
}


/*
 * Wrapper for calling Lua++ closures from Lua.
 * The closure is stored as upvalue[1].
 */
static int luappClosureWrapper(lua_State* L) {
    /* Get the Lua++ closure from upvalue */
    ObjClosure* closure = (ObjClosure*)lua_touserdata(L, lua_upvalueindex(1));
    if (closure == NULL) {
        return luaL_error(L, "Invalid Lua++ closure");
    }
    
    int argCount = lua_gettop(L);
    int expectedArgs = closure->function->arity;
    
    /* Check arity */
    if (argCount != expectedArgs) {
        return luaL_error(L, "Expected %d arguments but got %d",
                          expectedArgs, argCount);
    }
    
    /* Convert Lua arguments to Lua++ values */
    Value* args = NULL;
    if (argCount > 0) {
        args = (Value*)malloc(sizeof(Value) * argCount);
        if (args == NULL) {
            return luaL_error(L, "Out of memory");
        }
        for (int i = 0; i < argCount; i++) {
            args[i] = luaToLuapp(L, i + 1);  /* Lua indices are 1-based */
        }
    }
    
    /* Call the Lua++ function */
    Value result = NIL_VAL;
    bool success = callClosure(closure, argCount, args, &result);
    
    /* Free argument array */
    if (args != NULL) {
        free(args);
    }
    
    if (!success) {
        return luaL_error(L, "Lua++ function call failed");
    }
    
    /* Convert result back to Lua */
    luappToLua(L, result);
    return 1;
}

/*
 * Convert Lua++ closure to Lua function.
 */
static void closureToLua(lua_State* L, ObjClosure* closure) {
    /* Store closure pointer as light userdata upvalue */
    lua_pushlightuserdata(L, closure);
    lua_pushcclosure(L, luappClosureWrapper, 1);
}

/* ========== Lua → Lua++ Value ========== */

Value luaToLuapp(lua_State* L, int idx) {
    /* Handle negative indices */
    if (idx < 0 && idx > LUA_REGISTRYINDEX) {
        idx = lua_gettop(L) + idx + 1;
    }
    
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
            return NIL_VAL;
            
        case LUA_TBOOLEAN:
            return BOOL_VAL(lua_toboolean(L, idx));
            
        case LUA_TNUMBER:
            return NUMBER_VAL(lua_tonumber(L, idx));
            
        case LUA_TSTRING: {
            size_t len;
            const char* s = lua_tolstring(L, idx, &len);
            return OBJ_VAL(copyString(s, (int)len));
        }
        
        case LUA_TTABLE:
            return tableFromLua(L, idx);
            
        case LUA_TFUNCTION:
            return functionFromLua(L, idx);
            
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            /* Check if it's a wrapped Lua++ object */
            return NIL_VAL;
            
        default:
            return NIL_VAL;
    }
}

/* ========== Lua Function Wrapper (Reverse Direction) ========== */

/*
 * Structure to hold Lua function reference for calling from Lua++.
 * We store the Lua registry reference so the function isn't garbage collected.
 */
typedef struct {
    lua_State* L;       /* Lua state */
    int ref;            /* Registry reference to the Lua function */
} LuaFuncRef;

/* Array to track Lua function references */
#define MAX_LUA_FUNC_REFS 256
static LuaFuncRef luaFuncRefs[MAX_LUA_FUNC_REFS];
static int luaFuncRefCount = 0;

/*
 * Native function that calls a Lua function from Lua++.
 * The function reference index is encoded in the native's name.
 */
static Value luaFunctionNative(int argCount, Value* args) {
    if (globalLuaState == NULL) {
        return NIL_VAL;
    }
    
    lua_State* L = globalLuaState;
    
    /* Get the function reference from the native's context */
    /* We use a simple approach: store ref index in a global */
    /* This is set before calling the native */
    extern int currentLuaFuncRef;
    
    if (currentLuaFuncRef < 0 || currentLuaFuncRef >= luaFuncRefCount) {
        return NIL_VAL;
    }
    
    LuaFuncRef* funcRef = &luaFuncRefs[currentLuaFuncRef];
    
    /* Push the Lua function from registry */
    lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef->ref);
    
    /* Push arguments */
    for (int i = 0; i < argCount; i++) {
        luappToLua(L, args[i]);
    }
    
    /* Call the Lua function */
    if (lua_pcall(L, argCount, 1, 0) != LUA_OK) {
        /* Error occurred */
        lua_pop(L, 1);  /* Pop error message */
        return NIL_VAL;
    }
    
    /* Convert result back to Lua++ */
    Value result = luaToLuapp(L, -1);
    lua_pop(L, 1);
    
    return result;
}

/* Current Lua function reference being called (for native dispatch) */
int currentLuaFuncRef = -1;

/*
 * Wrapper native that dispatches to the correct Lua function.
 * Each wrapped Lua function gets a unique native with its ref index.
 */
static Value luaFuncWrapper0(int argCount, Value* args) { currentLuaFuncRef = 0; return luaFunctionNative(argCount, args); }
static Value luaFuncWrapper1(int argCount, Value* args) { currentLuaFuncRef = 1; return luaFunctionNative(argCount, args); }
static Value luaFuncWrapper2(int argCount, Value* args) { currentLuaFuncRef = 2; return luaFunctionNative(argCount, args); }
static Value luaFuncWrapper3(int argCount, Value* args) { currentLuaFuncRef = 3; return luaFunctionNative(argCount, args); }
static Value luaFuncWrapper4(int argCount, Value* args) { currentLuaFuncRef = 4; return luaFunctionNative(argCount, args); }
static Value luaFuncWrapper5(int argCount, Value* args) { currentLuaFuncRef = 5; return luaFunctionNative(argCount, args); }
static Value luaFuncWrapper6(int argCount, Value* args) { currentLuaFuncRef = 6; return luaFunctionNative(argCount, args); }
static Value luaFuncWrapper7(int argCount, Value* args) { currentLuaFuncRef = 7; return luaFunctionNative(argCount, args); }

static NativeFn luaFuncWrappers[] = {
    luaFuncWrapper0, luaFuncWrapper1, luaFuncWrapper2, luaFuncWrapper3,
    luaFuncWrapper4, luaFuncWrapper5, luaFuncWrapper6, luaFuncWrapper7,
};
#define NUM_LUA_FUNC_WRAPPERS (sizeof(luaFuncWrappers) / sizeof(luaFuncWrappers[0]))

/*
 * Convert a Lua function to a Lua++ native function.
 * This allows Lua++ code to call Lua functions.
 */
static Value functionFromLua(lua_State* L, int idx) {
    if (luaFuncRefCount >= MAX_LUA_FUNC_REFS || 
        luaFuncRefCount >= (int)NUM_LUA_FUNC_WRAPPERS) {
        /* Too many Lua functions wrapped */
        return NIL_VAL;
    }
    
    /* Store the Lua state for reverse calls */
    globalLuaState = L;
    
    /* Create a reference to the Lua function in the registry */
    lua_pushvalue(L, idx);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    
    /* Store the reference */
    int refIndex = luaFuncRefCount++;
    luaFuncRefs[refIndex].L = L;
    luaFuncRefs[refIndex].ref = ref;
    
    /* Create a Lua++ native function that wraps this Lua function */
    ObjString* name = copyString("<lua function>", 14);
    ObjNative* native = newNative(luaFuncWrappers[refIndex], name);
    
    return OBJ_VAL(native);
}

/*
 * Public API: Wrap a Lua function for use in Lua++.
 */
Value wrapLuaFunction(lua_State* L, int idx) {
    return functionFromLua(L, idx);
}

/*
 * Convert Lua table to Lua++ table.
 */
static Value tableFromLua(lua_State* L, int idx) {
    ObjTable* table = newTable();
    push(OBJ_VAL(table));  /* GC protection */
    
    lua_pushnil(L);  /* First key */
    while (lua_next(L, idx) != 0) {
        /* Key at -2, value at -1 */
        
        if (lua_isinteger(L, -2)) {
            /* Integer key → array part */
            int key = (int)lua_tointeger(L, -2);
            if (key >= 1) {
                Value val = luaToLuapp(L, -1);
                /* Grow array if needed */
                while (table->array.count < key) {
                    writeValueArray(&table->array, NIL_VAL);
                }
                table->array.values[key - 1] = val;
            }
        }
        else if (lua_isstring(L, -2)) {
            /* String key → hash part */
            size_t len;
            const char* key = lua_tolstring(L, -2, &len);
            ObjString* keyStr = copyString(key, (int)len);
            Value val = luaToLuapp(L, -1);
            tableSet(&table->entries, keyStr, val);
        }
        
        lua_pop(L, 1);  /* Pop value, keep key for next iteration */
    }
    
    pop();  /* Remove GC protection */
    return OBJ_VAL(table);
}

/* ========== Lua API Functions ========== */

/*
 * luapp.eval(code) - Evaluate Lua++ code string
 * Returns the result or nil on error.
 */
static int l_eval(lua_State* L) {
    const char* code = luaL_checkstring(L, 1);
    
    if (!vmInitialized) {
        initVM();
        vmInitialized = true;
    }
    
    InterpretResult result = interpret(code);
    
    if (result != INTERPRET_OK) {
        lua_pushnil(L);
        lua_pushstring(L, result == INTERPRET_COMPILE_ERROR ? 
                       "Compile error" : "Runtime error");
        return 2;
    }
    
    /* Get result from Lua++ stack if any */
    /* Note: Current VM doesn't expose results easily */
    lua_pushnil(L);
    return 1;
}

/*
 * luapp.load(filename) - Load and execute a Lua++ file
 * Returns a table with exported globals.
 */
static int l_load(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);
    
    /* Read file */
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        return luaL_error(L, "Cannot open file: %s", filename);
    }
    
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    
    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        return luaL_error(L, "Not enough memory to read file");
    }
    
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    
    /* Initialize VM if needed */
    if (!vmInitialized) {
        initVM();
        vmInitialized = true;
    }
    
    /* Compile and run */
    InterpretResult result = interpretWithFilename(buffer, filename);
    free(buffer);
    
    if (result != INTERPRET_OK) {
        return luaL_error(L, "Failed to load %s: %s", filename,
                          result == INTERPRET_COMPILE_ERROR ? 
                          "compile error" : "runtime error");
    }
    
    /* Export globals as Lua table */
    lua_newtable(L);
    
    /* Iterate through Lua++ globals and convert to Lua */
    for (int i = 0; i < vm.globals.capacity; i++) {
        Entry* entry = &vm.globals.entries[i];
        if (entry->key != NULL) {
            /* Skip built-in functions */
            const char* name = entry->key->chars;
            if (strcmp(name, "print") == 0 ||
                strcmp(name, "read") == 0 ||
                strcmp(name, "type") == 0 ||
                strcmp(name, "tonumber") == 0 ||
                strcmp(name, "tostring") == 0) {
                continue;
            }
            
            lua_pushlstring(L, entry->key->chars, entry->key->length);
            luappToLua(L, entry->value);
            lua_settable(L, -3);
        }
    }
    
    return 1;
}

/*
 * luapp.version() - Get Lua++ version string
 */
static int l_version(lua_State* L) {
    lua_pushstring(L, "Lua++ 0.1.0");
    return 1;
}

/*
 * luapp.reset() - Reset the Lua++ VM state
 */
static int l_reset(lua_State* L) {
    (void)L;
    if (vmInitialized) {
        freeVM();
        initVM();
    }
    /* Reset Lua function references */
    for (int i = 0; i < luaFuncRefCount; i++) {
        luaL_unref(luaFuncRefs[i].L, LUA_REGISTRYINDEX, luaFuncRefs[i].ref);
    }
    luaFuncRefCount = 0;
    return 0;
}

/*
 * luapp.new(class, ...) - Instantiate a Lua++ class from Lua
 * 
 * Usage:
 *   local mod = luapp.load("mymodule.luapp")
 *   local obj = luapp.new(mod.MyClass, arg1, arg2)
 * 
 * This is equivalent to: new MyClass(arg1, arg2) in Lua++
 */
int luappNewInstance(lua_State* L) {
    /* First argument must be a class (table with __luapp_class) */
    if (!lua_istable(L, 1)) {
        return luaL_error(L, "First argument must be a Lua++ class");
    }
    
    /* Get the __luapp_class field */
    lua_getfield(L, 1, "__luapp_class");
    if (!lua_islightuserdata(L, -1)) {
        lua_pop(L, 1);
        return luaL_error(L, "Invalid Lua++ class (missing __luapp_class)");
    }
    
    ObjClass* klass = (ObjClass*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    
    if (klass == NULL) {
        return luaL_error(L, "Invalid Lua++ class (null pointer)");
    }
    
    /* Create a new instance */
    ObjInstance* instance = newInstance(klass);
    push(OBJ_VAL(instance));  /* GC protection */
    
    /* Get argument count (excluding the class itself) */
    int argCount = lua_gettop(L) - 1;
    
    /* Look for init method */
    Value initializer;
    if (tableGet(&klass->methods, vm.initString, &initializer)) {
        /* Convert Lua arguments to Lua++ values */
        Value* args = NULL;
        if (argCount > 0) {
            args = (Value*)malloc(sizeof(Value) * argCount);
            if (args == NULL) {
                pop();  /* Remove GC protection */
                return luaL_error(L, "Out of memory");
            }
            for (int i = 0; i < argCount; i++) {
                args[i] = luaToLuapp(L, i + 2);  /* Skip class at index 1 */
            }
        }
        
        /* Set up the call: push instance as 'self', then args */
        push(OBJ_VAL(instance));  /* self */
        for (int i = 0; i < argCount; i++) {
            push(args[i]);
        }
        
        /* Call the initializer */
        ObjClosure* initClosure = AS_CLOSURE(initializer);
        Value result;
        bool success = callClosure(initClosure, argCount, args, &result);
        
        if (args != NULL) {
            free(args);
        }
        
        if (!success) {
            pop();  /* Remove GC protection */
            return luaL_error(L, "Lua++ constructor failed");
        }
    } else if (argCount > 0) {
        pop();  /* Remove GC protection */
        return luaL_error(L, "Class has no init method but %d arguments provided", argCount);
    }
    
    /* Convert instance to Lua and return */
    pop();  /* Remove GC protection */
    instanceToLua(L, instance);
    return 1;
}

/*
 * luapp.call(func, ...) - Call a Lua++ function from Lua
 * 
 * This is useful when you have a Lua++ closure and want to call it
 * with explicit control over arguments.
 */
static int l_call(lua_State* L) {
    /* First argument should be a Lua++ closure (wrapped as C closure) */
    if (!lua_isfunction(L, 1)) {
        return luaL_error(L, "First argument must be a function");
    }
    
    /* Get argument count (excluding the function itself) */
    int argCount = lua_gettop(L) - 1;
    
    /* Move function to position 1 and call it with remaining args */
    lua_call(L, argCount, 1);
    return 1;
}

/* ========== Module Registration ========== */

static const luaL_Reg luapp_funcs[] = {
    {"eval",    l_eval},
    {"load",    l_load},
    {"new",     luappNewInstance},
    {"call",    l_call},
    {"version", l_version},
    {"reset",   l_reset},
    {NULL, NULL}
};

int luaopen_luapp(lua_State* L) {
    /* Store global Lua state for reverse calls (Lua++ calling Lua) */
    globalLuaState = L;
    
    /* Initialize Lua++ VM */
    if (!vmInitialized) {
        initVM();
        vmInitialized = true;
    }
    
    /* Create module table */
    luaL_newlib(L, luapp_funcs);
    
    /* Add version info */
    lua_pushstring(L, "0.1.0");
    lua_setfield(L, -2, "_VERSION");
    
    return 1;
}
