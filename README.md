# Lua++

A Lua superset with modern features: OOP, traits, enhanced diagnostics, and compiler optimizations — implemented in pure C.

## Features

- **Full Lua compatibility** - vanilla Lua code runs unmodified
- **Classes & Inheritance** - `class`, `extends`, `self`, `super`, `new`
- **Traits** - reusable behavior mixins with `trait` and `implements`
- **Tables** - Lua's core data structure with key-value syntax `{name = "foo", 1, 2, 3}`
- **Control flow** - `break` and `continue` statements
- **Length operator** - `#table` and `#string`
- **Private members** - `private` keyword for encapsulation
- **Rich diagnostics** - colored errors/warnings with source context and suggestions
- **Constant folding** - compile-time evaluation of constant expressions
- **Mark-sweep GC** - automatic memory management
- **Bytecode VM** - stack-based interpreter with custom opcodes

## Building

```bash
make
```

## Usage

```bash
# Run a file
./luap examples/demo.luapp

# Interactive REPL
./luap

# Show version
./luap -V

# Verbose mode (bytecode + execution trace + GC logs)
./luap -v examples/demo.luapp

# Just dump bytecode
./luap --dump-bytecode examples/demo.luapp

# Just trace execution
./luap --trace examples/demo.luapp

# Log GC events
./luap --log-gc examples/demo.luapp
```

## Classes & Inheritance

```lua
class Animal
    function init(name)
        self.name = name
    end

    function speak()
        print(self.name .. " makes a sound")
    end
end

class Dog extends Animal
    function speak()
        print(self.name .. " barks!")
    end
end

local d = new Dog("Rex")
d:speak()  -- Rex barks!
```

## Traits

Traits provide reusable behavior that can be mixed into classes:

```lua
trait Printable
    function describe()
        return "I am " .. self.name
    end
end

trait Movable
    function move(x, y)
        self.x = self.x + x
        self.y = self.y + y
    end
end

class Player implements Printable, Movable
    function init(name)
        self.name = name
        self.x = 0
        self.y = 0
    end
end

local p = new Player("Hero")
print(p:describe())  -- I am Hero
p:move(5, 3)
```

## Tables

```lua
-- Array-style
local numbers = {1, 2, 3, 4, 5}
print(numbers[1])  -- 1 (1-indexed)

-- Key-value syntax
local person = {name = "Alice", age = 30}
print(person["name"])  -- Alice

-- Mixed
local mixed = {1, 2, key = "value", 3}

-- Length operator
print(#numbers)  -- 5
```

## Control Flow

```lua
-- Break out of loops
for i = 1, 100 do
    if i > 5 then break end
    print(i)
end

-- Continue to next iteration
for i = 1, 10 do
    if i % 2 == 0 then continue end
    print(i)  -- prints odd numbers only
end
```

## Rich Diagnostics

Lua++ provides helpful error messages with source context:

```
error[E003]: cannot use 'self' outside of a class
  --> examples/error_test.luapp:5:7
     |
   5 | print(self.name)
     |       ^^^^
     |
   = help: 'self' refers to the current instance and is only valid inside class methods
```

Warnings for potential issues:

```
warning[W001]: unused variable 'temp'
  --> script.luapp:10:11
     |
  10 |     local temp = calculate()
     |           ^^^^
```

## Constant Folding

The compiler evaluates constant expressions at compile time:

```lua
local x = 2 + 3 * 4           -- Compiled as 14
local s = "Hello, " .. "World!"  -- Compiled as "Hello, World!"
local b = 10 > 5              -- Compiled as true
```

## Private Members

```lua
class Counter
    private count

    function init()
        self.count = 0
    end

    function increment()
        self.count = self.count + 1
    end

    function get()
        return self.count
    end
end
```

## Examples

See the `examples/` directory:
- `demo.luapp` - Basic language features
- `oop_test.luapp` - Classes and inheritance
- `new_features.luapp` - Tables, break, continue, traits
- `constant_folding.luapp` - Compiler optimizations
- `error_showcase.luapp` - Diagnostic system demo

## Architecture

```
src/
├── main.c        - CLI entry point
├── lexer.c       - Tokenizer
├── compiler.c    - Pratt parser + bytecode emission + constant folding
├── vm.c          - Bytecode interpreter
├── object.c      - Heap objects (strings, functions, classes, tables, traits)
├── memory.c      - Allocator + mark-sweep GC
├── table.c       - Hash table implementation
├── chunk.c       - Bytecode container
├── value.c       - Tagged union values
├── debug.c       - Bytecode disassembler
└── diagnostic.c  - Error/warning reporting with source context
```

## License

MIT
# Lua-
