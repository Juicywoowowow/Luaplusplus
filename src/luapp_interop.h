/*
 * luapp_interop.h - Lua ↔ Lua++ interoperability layer
 * 
 * Allows standard Lua to load and use Lua++ modules.
 * Marshals values between the two runtimes.
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
 * Module entry point for: require("luapp")
 */
int luaopen_luapp(lua_State* L);

#endif
