#!/usr/bin/env lua
--[[
    use_from_lua.lua
    Example: Using Lua++ modules from standard Lua
    
    Run from lua-plus-plus directory:
        lua examples/interop/use_from_lua.lua
--]]

-- Add current directory to cpath for the luapp.so library
package.cpath = "./?.so;" .. package.cpath

-- Load the Lua++ interop library
local luapp = require("luapp")

print("========================================")
print("  Lua++ Interop Demo")
print("  " .. luapp.version())
print("========================================")
print()

-- Load a simple Lua++ module
print("Loading simple_module.luapp...")
local mod = luapp.load("examples/interop/simple_module.luapp")
print()

-- List what was exported
print("Exported items:")
for k, v in pairs(mod) do
    local vtype = type(v)
    if vtype == "table" then
        print(string.format("  %s = <class>", k))
    elseif vtype == "function" then
        print(string.format("  %s = <function>", k))
    else
        print(string.format("  %s = %s", k, tostring(v)))
    end
end
print()

-- Test exported constant getters
print("Constants (via getter functions):")
print("  getPI:", type(mod.getPI))
print("  getVersion:", type(mod.getVersion))
print()

-- Test functions (note: calling Lua++ functions from Lua is limited)
print("Functions exported:")
print("  greet:", type(mod.greet))
print("  add:", type(mod.add))
print("  factorial:", type(mod.factorial))
print()

-- Show class structure
print("Counter class structure:")
local Counter = mod.Counter
if Counter then
    for k, v in pairs(Counter) do
        if k:sub(1,2) ~= "__" then  -- Skip internal fields
            print(string.format("  .%s = %s", k, type(v)))
        end
    end
end
print()

print("========================================")
print("  Interop test complete!")
print("========================================")
