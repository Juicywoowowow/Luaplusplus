/*
 * luapp_interop.h - Lua ↔ Lua++ interoperability layer
 * 
 * Allows standard Lua to load and use Lua++ modules.
 * Marshals values between the two runtimes.
 * 
 * Features:
 * - Load and execute Lua++ files from Lua
 * - Call Lua++ functions from Lua
 * - Instantiate Lua++ classes from Lua
 * - Call Lua functions from Lua++ (reverse direction)
 */

#ifndef luapp_interop_h
#define luapp_interop_h

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "value.h"
#include "object.h"

/*
 * Convert a Lua value (at stack index) to a Lua++ Value.
 * Tables are deep-copied. Functions become wrapped natives.
 */
Value luaToLuapp(lua_State* L, int idx);

/*
 * Push a Lua++ Value onto the Lua stack.
 * Tables/classes/instances become Lua tables with metatables.
 */
void luappToLua(lua_State* L, Value val);

/*
 * Create a Lua++ native function that wraps a Lua function.
 * This allows Lua++ code to call Lua functions.
 * The Lua function reference is stored in the Lua registry.
 */
Value wrapLuaFunction(lua_State* L, int idx);

/*
 * Instantiate a Lua++ class from Lua.
 * Takes the class and constructor arguments, returns the new instance.
 */
int luappNewInstance(lua_State* L);

/*
 * Module entry point for: require("luapp")
 */
int luaopen_luapp(lua_State* L);

#endif
