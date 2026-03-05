# ADR-004: Lua Scripting Layer

**Revision 1** — addresses security-auditor findings from shift-left review (2 CRITICAL, 3 HIGH, 5 MEDIUM resolved).

**Status:** PROPOSED
**Author:** architect
**Date:** 2026-03-05
**Tiers:** ALL (RETRO, LEGACY, STANDARD, MODERN)
**Security Review Required:** YES — this ADR touches the Lua scripting sandbox (CLAUDE.md Section 5)

This is the most important ADR for FFE's mission. The scripting layer is how a sixteen-year-old makes their first game. It must be safe, simple, and powerful enough to build real games. Every design decision here is evaluated against one question: would a kid who just learned Lua be able to use this correctly and not hurt themselves or anyone playing their game?

ADR-001 defined the core skeleton, ECS, and the system priority convention (100-199 for gameplay/scripting). ADR-002 defined the renderer. This ADR defines everything that sits in `engine/scripting/` and the contract between C++ engine code and Lua game code.

---

## 1. Design Philosophy

### 1.1 Lua Is the Game Language, C++ Is the Engine Language

This is a hard boundary. Game developers write Lua. Engine developers write C++. The scripting API is the interface between these two worlds. It is not "C++ exposed to Lua" — it is a purpose-built game development API that happens to be implemented in C++ underneath.

When a game developer writes `entity:getTransform().position.x`, they should not need to know what EnTT is, what a registry is, or what template instantiation means. They should think in terms of entities, components, positions, and sprites. The API hides every C++ implementation detail behind names and patterns that make sense to someone who learned programming last month.

### 1.2 sol2 Is the Binding Layer

sol2 (v3.x) is a header-only, type-safe C++/Lua binding library. It is already declared in `vcpkg.json`. It provides:

- Automatic type conversion between C++ and Lua types
- Usertype registration with member functions and properties
- Protected call wrappers (`sol::protected_function`)
- Table manipulation for sandbox construction
- Compile-time type safety — binding errors are caught at compile time, not at runtime in a player's game

sol2 is used internally by the engine. Game developers never see it. They see Lua tables, functions, and the FFE game API.

### 1.3 LuaJIT Is the Runtime

LuaJIT 2.1 is the Lua runtime. It is already installed as a system package. LuaJIT is two to ten times faster than PUC Lua 5.4 for the kind of work game scripts do: tight loops, floating-point math, table manipulation. On LEGACY hardware, this performance difference is the difference between a game running at 60 fps and a game running at 40 fps.

LuaJIT implements Lua 5.1 with select 5.2 extensions. This is a deliberate constraint. Lua 5.1 is simpler than 5.4, has better JIT coverage, and is the version most game scripting tutorials and books target. A student searching "Lua game programming" will find material that works with FFE.

**Compatibility note:** sol2 supports LuaJIT natively. No compatibility shims are needed.

### 1.4 The API Should Feel Like It Was Designed for Games

Bad:
```lua
local reg = engine.getWorld():getRegistry()
local view = reg:view(engine.components.Transform, engine.components.Sprite)
view:each(function(entity, transform, sprite) ... end)
```

Good:
```lua
for id, transform, sprite in world:each("Transform", "Sprite") do
    transform.position.x = transform.position.x + speed * dt
end
```

The good version reads like pseudocode. The bad version reads like a C++ programmer forgot they were writing a Lua API. Every binding decision in this ADR optimises for the good version.

---

## 2. Sandbox Design

This section is the most security-sensitive part of FastFreeEngine. Games built on FFE may be played by children. Game scripts from community sources may be downloaded and run by players who do not read the source code. The sandbox must be escape-proof.

### 2.1 Threat Model

The primary threat is a malicious or carelessly-written Lua script that:

1. Reads or writes files on the player's filesystem
2. Executes arbitrary OS commands
3. Exfiltrates data via network or environment variables
4. Causes denial of service through infinite loops or memory exhaustion
5. Crashes the engine through unchecked operations
6. Escapes the sandbox via the debug library or bytecode manipulation

Every item in the following whitelist and blacklist exists to counter one or more of these threats.

### 2.2 Whitelist — What Lua Scripts CAN Access

The sandbox is constructed by building a **new, empty environment table** and adding only what is explicitly listed here. This is a whitelist, not a blacklist. If something is not listed below, it does not exist in the script environment.

#### ECS Operations
| Function | Description |
|---|---|
| `world:spawn()` | Create a new entity. Returns an entity handle. |
| `world:destroy(id)` | Destroy an entity by handle. |
| `world:isValid(id)` | Check if an entity handle is still valid. |
| `world:add(id, "Component", {...})` | Add a component to an entity with initial values. |
| `world:get(id, "Component")` | Get a component from an entity. Returns a reference. |
| `world:has(id, "Component")` | Check if an entity has a component. |
| `world:remove(id, "Component")` | Remove a component from an entity. |
| `world:each("Comp1", "Comp2", ...)` | Iterate all entities with the given components. |
| `world:count("Component")` | Count entities with a given component. |

Components are identified by string name at the Lua boundary. The C++ side maps these strings to component types via a compile-time registry (see Section 4). This prevents Lua from requesting arbitrary C++ types.

#### Components (Read/Write Access)

**Transform:**
```lua
local t = world:get(id, "Transform")
t.position.x = 10.0    -- glm::vec3, exposed as {x, y, z}
t.position.y = 20.0
t.scale.x = 2.0
t.rotation = 3.14      -- radians, z-axis
```

**Sprite:**
```lua
local s = world:get(id, "Sprite")
s.size.x = 64.0        -- glm::vec2
s.size.y = 64.0
s.color.r = 1.0        -- glm::vec4 as {r, g, b, a}
s.color.a = 0.5
s.layer = 1
s.sortOrder = 10
-- s.texture is read-only from Lua (set via asset loading API)
-- s.uvMin, s.uvMax are read/write for spritesheet animation
```

Future components (Rigidbody, Collider, AudioSource, etc.) follow the same pattern: registered by name, fields exposed as properties.

#### Input Queries
| Function | Description |
|---|---|
| `input.isKeyPressed(key)` | True on the frame the key was first pressed. |
| `input.isKeyHeld(key)` | True every frame the key is held down. |
| `input.isKeyReleased(key)` | True on the frame the key was released. |
| `input.mousePosition()` | Returns `{x, y}` in screen coordinates. |
| `input.isMousePressed(button)` | True on frame mouse button was pressed. |
| `input.isMouseHeld(button)` | True every frame mouse button is held. |

Key constants are plain strings: `"W"`, `"A"`, `"Space"`, `"Escape"`, `"Left"`, `"Right"`, etc. Not scancodes. Not integer constants. Strings that a beginner can read and type correctly.

#### Math Types
| Type | Description |
|---|---|
| `vec2(x, y)` | 2D vector with arithmetic operators (+, -, *, /) and `.x`, `.y` |
| `vec3(x, y, z)` | 3D vector with arithmetic operators and `.x`, `.y`, `.z` |
| `math.lerp(a, b, t)` | Linear interpolation |
| `math.clamp(val, min, max)` | Clamp a value |
| `math.distance(a, b)` | Distance between two vec2 or vec3 |
| `math.normalize(v)` | Return a normalized copy of a vector |
| `math.dot(a, b)` | Dot product |
| `math.random(min, max)` | Random float in range (engine-seeded RNG, not os-dependent) |
| `math.randomInt(min, max)` | Random integer in range |
| `math.pi` | 3.14159... |

Standard Lua `math` library functions (`math.sin`, `math.cos`, `math.floor`, `math.ceil`, `math.abs`, `math.sqrt`, `math.min`, `math.max`, `math.atan`) are also available. These are safe — they are pure functions with no side effects.

**Explicitly removed from the math table:** `math.randomseed`. The engine controls RNG seeding to ensure determinism and prevent scripts from influencing the random sequence in security-relevant ways. Scripts use `math.random(min, max)` and `math.randomInt(min, max)` which are backed by the engine-seeded RNG.

#### Logging
| Function | Description |
|---|---|
| `log.info(msg)` | Log an informational message. |
| `log.warn(msg)` | Log a warning. |
| `log.error(msg)` | Log an error. |

All log functions prepend the script name and are rate-limited to prevent log flooding (max 100 messages per script per frame). Log output goes to the engine's logging system (ADR-001 Section 6).

#### Time
| Value | Description |
|---|---|
| `dt` | Delta time passed as a parameter to `update(dt)`. Not a global. |

There is no global clock access. Scripts receive `dt` and that is all they need. This prevents timing-based side channels and simplifies replay/determinism.

#### Engine Control
| Function | Description |
|---|---|
| `engine.requestShutdown()` | Request clean engine shutdown at end of frame. |
| `engine.tier()` | Returns the current hardware tier as a string ("RETRO", "LEGACY", etc.) |

#### Safe Standard Library Subset
| Module / Function | Why It Is Safe |
|---|---|
| `type()` | Type checking — pure function |
| `tostring()` | String conversion — pure function |
| `tonumber()` | Number conversion — pure function |
| `pairs()`, `ipairs()` | Table iteration — pure functions |
| `next()` | Table iteration primitive — pure function |
| `select()` | Vararg selection — pure function |
| `unpack()` / `table.unpack()` | Table unpacking — pure function |
| `pcall()`, `xpcall()` | Error handling — scripts should be able to handle their own errors. **Audit trail:** errors caught by pcall/xpcall are still logged at `DEBUG` level by the engine so that swallowed errors are visible in diagnostic logs. This prevents scripts from silently hiding failures. |
| `error()` | Raise an error — safe, caught by engine's pcall |
| `assert()` | Assertions — safe |
| `string.*` (except `string.dump`) | String manipulation — pure functions |
| `table.*` | Table manipulation — pure functions |
| `print()` | Redirected to `log.info()` — safe |

### 2.3 Blacklist — What Is Removed and Why

These items are **never present** in the script environment. They are not "disabled" — they are never added to the sandbox table in the first place.

| Removed | Threat It Prevents |
|---|---|
| `os` library | Filesystem access, command execution, environment variable exfiltration |
| `io` library | Arbitrary file read/write on the player's machine |
| `debug` library | Can inspect and modify any Lua state, read upvalues, set metatables on protected objects — the universal sandbox escape tool |
| `package` / `require` (filesystem version) | Loading arbitrary `.lua` or `.so`/`.dll` files from disk |
| `loadfile()` | Execute arbitrary Lua files |
| `dofile()` | Execute arbitrary Lua files |
| `load()` | Execute arbitrary Lua code from strings — code injection vector. **Removed unconditionally.** Even with a sandboxed environment, `load()` enables eval-style code generation that complicates security analysis and can be combined with other primitives in unexpected ways. If a legitimate use case arises (e.g., data-driven game patterns), it will be wrapped in a purpose-built C++ function that forces the sandbox environment and applies additional validation — never exposed as raw `load()`. |
| `string.dump()` | Serialises a function to bytecode. Crafted bytecode can crash LuaJIT or escape the sandbox. Removed unconditionally. |
| `collectgarbage()` | Memory management is engine-controlled. Scripts must not trigger GC pauses. |
| `rawget()`, `rawset()` | Bypass metatables — could bypass read-only protections on engine objects |
| `setfenv()`, `getfenv()` | Environment manipulation — could escape the sandbox by changing a function's environment to the global table |
| `setmetatable()` | **Removed entirely** — not just on engine types. Lua game scripts do not need custom metatables; the engine provides all types (vec2, vec3, entity handles, components) with their metatables already set. Allowing `setmetatable()` on any table risks `__gc` metamethod abuse (arbitrary code execution during garbage collection), proxy table confusion, and metatable poisoning attacks. If a future feature requires metatable manipulation, it will be provided through a safe engine API. |
| `newproxy()` | Creates userdata — could be used to confuse type checks |
| `load()` | Code injection vector — removed unconditionally (see above) |
| `setmetatable()` | Removed entirely — `__gc` metamethod abuse, metatable poisoning (see above) |
| `coroutine` library | Complicates execution model, makes instruction hook harder to reason about, can be used to evade time budgets. Not exposed. Timer API (`engine.after()`) covers the common use cases. |
| `ffi` library (LuaJIT) | **Explicitly NOT opened.** The FFI library provides raw memory access and C function calls — a complete sandbox escape. Must never be loaded via `sol::lib` selectors. |
| `jit` library (LuaJIT) | **Explicitly NOT opened.** Provides control over JIT compilation that could interfere with the instruction hook safety mechanism. JIT mode is controlled exclusively by the engine based on `ScriptTrustLevel`. |

### 2.4 `require` Replacement — Engine Module System

Scripts cannot use filesystem `require`. Instead, FFE provides a module system:

```lua
-- Scripts can require other game scripts by name (not path)
local utils = require("utils")        -- loads scripts/utils.lua from the game directory
local config = require("config")      -- loads scripts/config.lua

-- Engine-provided modules are available by convention
local colors = require("ffe.colors")  -- engine-provided colour constants
```

The engine's `require` replacement:

1. Accepts a module name (no path separators, no file extensions)
2. Looks up the name in a table of already-loaded modules (cache)
3. If not cached, searches ONLY the game's `scripts/` directory for `<name>.lua`
4. Loads the file into the sandbox environment (same sandbox, not a new one)
5. Caches the result
6. Path traversal is prevented by strict validation:
   - Module names are validated against `[a-zA-Z0-9_.]` (reject all other characters)
   - **Reject:** leading dots, trailing dots, consecutive dots (`..`), and names exceeding 128 characters
   - Dots map to subdirectories with each path component validated independently
   - After constructing the full filesystem path, verify via `realpath()` (or equivalent) that the resolved path is within the `scripts/` directory — this is the final defense against symlink attacks or validation bypasses

Engine-provided modules (prefixed with `ffe.`) are preloaded at startup. Game scripts cannot shadow them.

### 2.5 Resource Limits

#### Memory Limit

LuaJIT supports custom allocators. The engine provides a tracking allocator that:

1. Wraps the default allocator
2. Tracks total bytes allocated by Lua
3. Returns `NULL` when the configured limit is reached (LuaJIT handles this as an out-of-memory error)

| Tier | Default Lua Memory Limit |
|---|---|
| RETRO | 32 MB |
| LEGACY | 64 MB |
| STANDARD | 128 MB |
| MODERN | 256 MB |

These defaults are configurable in the application config. The limits are generous for game scripting — a complex Lua game with hundreds of entities and tables should use under 10 MB. The limits exist to catch runaway allocations (e.g., a script that builds an infinite table), not to restrict normal usage.

**Per-allocation size cap:** In addition to the total memory limit, the tracking allocator enforces a per-allocation size cap of **16 MB**. Any single allocation request exceeding this cap returns `NULL` immediately, triggering a Lua out-of-memory error. This catches attacks like `string.rep("x", 2^30)` that attempt to allocate a massive contiguous block in a single operation. The per-allocation cap must be tested explicitly: verify that `string.rep` with values exceeding the cap triggers a graceful Lua error (caught by pcall), not an engine crash.

#### CPU / Instruction Limit — Infinite Loop Protection

**This is a safety-critical feature. FFE is an engine children will use. Infinite loop protection MUST be active in ALL builds where Lua scripts are present.** The performance cost is the price of safety.

LuaJIT does not support `lua_sethook` on JIT-compiled code (the hook only fires in the interpreter). Therefore, for untrusted code (which is the default — see below), the JIT compiler is disabled via `luajit.setmode(0, LUAJIT_MODE_OFF)`. All script execution runs in the interpreter where the instruction hook is reliable. This is an acceptable trade-off: interpreter-mode LuaJIT is still significantly faster than PUC Lua 5.4, and safety is non-negotiable.

**Instruction hook with wall-clock time check:**

The engine sets `lua_sethook` with an instruction count callback (e.g., every 10,000 instructions). Inside the hook callback:

1. Check a wall-clock timer (`clock_gettime(CLOCK_MONOTONIC)` or equivalent) against the per-call time budget
2. If the wall-clock budget is exceeded, call `luaL_error(L, "script execution time limit exceeded")` to interrupt the script immediately
3. The error is caught by the engine's `sol::protected_function` pcall wrapper
4. The offending script is disabled (same as any other script error — see Section 5.1)

The instruction count alone is insufficient because different instructions have different costs. The wall-clock check catches both tight computational loops and expensive operations that consume time without many instructions.

**Trust levels and the default:**

The `ScriptEngine` constructor accepts a `ScriptTrustLevel` enum. **The default is `UNTRUSTED`**:

```cpp
enum class ScriptTrustLevel : u8 {
    TRUSTED,      // JIT enabled, hook check interval increased (less overhead)
    UNTRUSTED     // JIT disabled (LUAJIT_MODE_OFF), instruction hook with wall-clock check active
};
```

For `TRUSTED` scripts (opted-in explicitly by the developer), the JIT is enabled and the instruction hook fires at a higher interval (e.g., every 1,000,000 instructions) with the same wall-clock check. This gives better performance while still providing a safety net. **`TRUSTED` is never the default.** A developer must consciously opt in.

| Budget | Value |
|---|---|
| Per-script `update()` warning threshold | 2 ms (LEGACY) |
| Total Lua budget per frame | 4 ms (LEGACY, ~25% of 16.67 ms frame) |
| Instruction hook interval (UNTRUSTED) | Every 10,000 instructions |
| Instruction hook interval (TRUSTED) | Every 1,000,000 instructions |
| Per-call wall-clock hard limit | 8 ms (kills the script, not just a warning) |

#### Stack Depth Limit

LuaJIT's default stack limit (`LUAI_MAXCSTACK`, typically 8000) is kept as-is. This is sufficient for any reasonable game script and prevents stack overflow from infinite recursion.

---

## 3. Script Lifecycle

### 3.1 Script File Convention

Game scripts live in a `scripts/` directory in the game project:

```
my_game/
├── scripts/
│   ├── main.lua          -- entry point, registers all systems
│   ├── player.lua        -- player movement and input
│   ├── enemies.lua       -- enemy AI and spawning
│   ├── hud.lua           -- HUD overlay logic
│   └── utils.lua         -- shared utility functions
└── assets/
    └── ...
```

### 3.2 Script Structure

Each script that acts as a system is a Lua table with lifecycle functions:

```lua
-- scripts/player.lua
local Player = {}

function Player.init(world)
    -- Called once when the script is loaded.
    -- Create entities, set initial state.
end

function Player.update(world, dt)
    -- Called every tick at the script's registered priority.
    -- Game logic goes here.
end

function Player.shutdown(world)
    -- Called when the engine shuts down or the script is unloaded.
    -- Clean up if needed (entity destruction is automatic).
end

return Player
```

All three functions are optional. A script with only `init` is valid (e.g., a level setup script). A script with only `update` is valid (e.g., a passive system). The `world` parameter is the same `World` from ADR-001, wrapped for Lua access.

### 3.3 Registration — `main.lua`

The game's entry point is `scripts/main.lua`. This script registers game systems:

```lua
-- scripts/main.lua
local player = require("player")
local enemies = require("enemies")
local hud = require("hud")

-- Register systems with priorities (same convention as C++: lower = earlier)
-- 100-199 range is for gameplay/scripting (ADR-001 Section 3.3)
engine.registerSystem("Player Input",   player,  100)
engine.registerSystem("Enemy AI",       enemies, 110)
engine.registerSystem("HUD Update",     hud,     120)
```

`engine.registerSystem(name, scriptTable, priority)` does the following:

1. Validates that `scriptTable` is a table
2. If the table has an `init` function, calls it immediately with `world`
3. If the table has an `update` function, creates a `SystemDescriptor` with a C++ trampoline function that calls the Lua `update` with `world` and `dt`
4. Registers the `SystemDescriptor` with the `World` at the given priority
5. If the table has a `shutdown` function, stores a reference for shutdown sequencing

### 3.4 Loading Sequence

During `Application::startup()`, step 6 ("Initialize the scripting engine") does:

1. Create the `sol::state` (backed by LuaJIT with the tracking allocator)
2. Build the sandbox environment (Section 2.2/2.3)
3. Register all C++ type bindings (Section 4)
4. Load and execute `scripts/main.lua` in the sandbox
5. `main.lua` calls `engine.registerSystem()` to register script systems
6. Control returns to `Application::startup()`, which continues with step 7 (register built-in systems) and step 8 (sort systems by priority)

Script systems and C++ systems coexist in the same priority-sorted list. A Lua gameplay system at priority 110 runs after a C++ input system at priority 50 and before a C++ physics system at priority 200. They are peers.

### 3.5 Shutdown Sequence

During `Application::shutdown()`, step 6 ("Shutdown scripting"):

1. Call `shutdown(world)` on every registered script that has a `shutdown` function, in reverse registration order
2. Clear all Lua references and caches
3. Close the `sol::state` (frees all Lua memory)

---

## 4. Binding Pattern

### 4.1 Type Bindings with sol2

Each exposed C++ type gets a sol2 usertype registration. These are defined in `engine/scripting/lua_bindings.cpp` and called once during scripting engine initialisation.

```cpp
// Binding Transform to Lua
void bindTransform(sol::state& lua) {
    lua.new_usertype<Transform>("Transform",
        "position", &Transform::position,
        "scale",    &Transform::scale,
        "rotation", &Transform::rotation
    );
}

// Binding vec3 to Lua (from glm)
void bindVec3(sol::state& lua) {
    lua.new_usertype<glm::vec3>("vec3",
        sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z,
        sol::meta_function::addition,       [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
        sol::meta_function::subtraction,    [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
        sol::meta_function::multiplication, [](const glm::vec3& a, float s) { return a * s; },
        sol::meta_function::to_string,      [](const glm::vec3& v) {
            char buf[64];
            snprintf(buf, sizeof(buf), "vec3(%.2f, %.2f, %.2f)", v.x, v.y, v.z);
            return std::string(buf);
        }
    );
}
```

### 4.2 Component Access — String-Based

Components are accessed by string name, not by C++ type. This is a deliberate choice:

```lua
-- This is the API:
local t = world:get(id, "Transform")
local s = world:get(id, "Sprite")
world:add(id, "Transform", { position = vec3(10, 20, 0) })
```

Why string-based instead of typed methods like `entity:getTransform()`:

1. **Extensibility.** When we add Rigidbody, Collider, AudioSource, etc., no new Lua methods need to be added. The same `world:get(id, "ComponentName")` works for all of them.
2. **Data-driven.** Game configs and editors can reference components by name in JSON/Lua tables.
3. **Discoverability.** A beginner reads `world:get(id, "Transform")` and understands what it does. `world:getTransform(id)` is fine too, but it does not scale.
4. **One pattern to learn.** Instead of memorising per-component methods, learn one pattern that works everywhere.

The C++ implementation maps component names to type-erased accessor functions at registration time:

```cpp
// Pseudocode — actual implementation uses sol2's type system
componentRegistry["Transform"] = ComponentBinding{
    .add    = [](World& w, EntityId id, sol::table args) { /* ... */ },
    .get    = [](World& w, EntityId id) -> sol::object { /* ... */ },
    .has    = [](World& w, EntityId id) -> bool { /* ... */ },
    .remove = [](World& w, EntityId id) { /* ... */ }
};
```

The string lookup uses a flat `std::array` or `std::unordered_map` with a small fixed set of entries. This is not a hot-path concern — component access from Lua is inherently slower than C++ template access, and the string lookup is dwarfed by the sol2 type conversion overhead.

### 4.3 View / Query Iteration

The `world:each(...)` function is the primary way to iterate entities from Lua:

```lua
for id, transform, sprite in world:each("Transform", "Sprite") do
    transform.position.x = transform.position.x + 1.0 * dt
end
```

Under the hood, this:

1. Looks up the component types from the string names
2. Creates an EnTT view over those component types
3. Returns a Lua iterator that yields `(EntityId, Component1&, Component2&, ...)` per entity

The view is created once per `each()` call. The iterator steps through it.

**Component access returns proxy objects, not raw references.** When Lua calls `world:get(id, "Transform")` or receives components from `world:each()`, it receives a lightweight proxy object that stores the `EntityId` and the component type. Every property access on the proxy (e.g., `t.position.x`) validates that the entity is still alive (`world:isValid(id)`) before accessing the underlying component data. If the entity has been destroyed (e.g., another script destroyed it, or a deferred destroy executed), the proxy returns a safe default value and logs a warning.

This prevents use-after-free vulnerabilities when entities are destroyed while Lua still holds a reference to their components. Without proxy validation, a Lua script holding a stale component reference could read or write freed memory.

The proxy overhead is one `isValid()` check (a generation comparison, effectively free) per property access. This is the correct trade-off for an engine where scripts from untrusted sources may run.

**Maximum components per `each()` call:** 8. This is a compile-time limit based on the number of template instantiations we generate. Eight components in a single query is more than any reasonable game logic needs. If someone hits this limit, their query is doing too much.

### 4.4 Entity Handles

Entities are exposed to Lua as opaque integer handles (the `EntityId` from `types.h`). Lua scripts cannot construct entity IDs from raw numbers — they can only obtain them from `world:spawn()` or from iteration. This prevents stale-handle bugs where a script fabricates an entity ID that refers to a destroyed-and-recycled entity.

```lua
local id = world:spawn()               -- only way to get a valid ID
world:add(id, "Transform", {})
local valid = world:isValid(id)         -- true
world:destroy(id)
local gone = world:isValid(id)          -- false
```

The version/generation bits in `EntityId` (upper 12 bits, per `types.h`) ensure that a destroyed entity's ID will not match a new entity that reuses the same index.

---

## 5. Error Handling

### 5.1 Core Principle: Lua Errors Never Crash the Engine

Every Lua invocation from C++ goes through `sol::protected_function_result` (i.e., pcall). If a Lua script throws an error, raises an exception, or triggers a runtime fault:

1. The error is caught by the protected call
2. The error message (including script name, line number, and stack trace) is logged via `FFE_LOG_ERROR`
3. The failing script's `update` function is **disabled** — it will not be called again until reloaded
4. The engine continues running with all other systems intact
5. If in development mode, the error is also displayed on-screen (future editor overlay)

### 5.2 Error Message Quality

Error messages from the scripting layer must be helpful to beginners. The engine post-processes Lua error messages to add context:

```
[SCRIPT ERROR] player.lua:42: attempt to index a nil value (field 'positon')
  Hint: Did you mean 'position'? (Transform has fields: position, scale, rotation)

  Stack trace:
    player.lua:42 in function 'update'
    [C]: in function 'pcall'
```

The "hint" system is a stretch goal, not a launch requirement. But the script name, line number, and field name must always be present. These three pieces of information are the difference between a student fixing their bug in 30 seconds and giving up.

### 5.3 Type Errors

sol2 provides excellent type checking at the binding boundary. If a script passes a string where a number is expected, sol2 produces a clear error before the C++ code ever sees the wrong type. These errors are caught by the same pcall mechanism.

```
[SCRIPT ERROR] enemies.lua:15: bad argument #2 to 'world:add' (number expected, got string)
```

### 5.4 Nil Component Access

Accessing a component that an entity does not have is a common beginner mistake:

```lua
local t = world:get(id, "Transform")  -- entity has no Transform
t.position.x = 5.0                     -- nil index error
```

The `world:get()` binding checks `hasComponent` before accessing it. If the component is missing, it returns `nil` and logs a warning (once per script per component type, to avoid log spam):

```
[SCRIPT WARN] player.lua:20: entity 42 has no Transform component (world:get returned nil)
```

---

## 6. Hot Reload (Development Feature)

### 6.1 File Watcher

In development builds (`FFE_DEV=1`), the scripting engine watches the `scripts/` directory for file modifications using `inotify` (Linux). When a `.lua` file changes:

1. Log the detected change
2. Wait 100ms for the editor to finish writing (debounce)
3. Reload the modified file

### 6.2 Reload Strategy

Hot reload **does not preserve script state.** When a script is reloaded:

1. Call `shutdown(world)` on the old script table (if it has one)
2. Remove the old script's system from the system list
3. Load and execute the new file in the sandbox
4. Call `init(world)` on the new script table
5. Register the new `update` function at the same priority

**Why no state preservation:** Preserving Lua state across reloads is fragile and error-prone. It requires diffing table shapes, handling type changes, and dealing with stale closures. For a development tool, the simplicity of "reload from scratch" is worth the cost of re-running `init`. If a script needs persistent state across reloads (e.g., entity positions), that state already lives in ECS components, which are not affected by the script reload.

### 6.3 Scope

Hot reload is a development convenience, not a production feature. It is **compiled out of release builds entirely** (guarded by `#ifdef FFE_DEV`). It adds no overhead to shipped games.

**TOCTOU limitation:** There is an inherent time-of-check-time-of-use race between detecting a file change and reading the file. In development builds this is acceptable — the developer is modifying their own files on a local machine. In release builds this code does not exist. This is documented here so that no one attempts to repurpose the hot reload mechanism for production mod loading.

---

## 7. Performance Budget

### 7.1 Frame Time Allocation

On LEGACY hardware targeting 60 fps, the total frame budget is 16.67 ms. The Lua scripting layer is allocated:

| Budget | Time | Percentage |
|---|---|---|
| Total Lua execution per frame | 4.0 ms | ~24% |
| Per-script `update()` warning | 2.0 ms | ~12% |

The per-script warning threshold is a soft budget (produces a Tracy-visible warning and log message). The per-call wall-clock hard limit (8 ms, see Section 2.5) is a hard cap that terminates the script via `luaL_error()`. This is safe because all Lua calls go through `sol::protected_function` (pcall), so the error is caught cleanly and the script is disabled without leaving the game in an inconsistent state.

If the total Lua budget is consistently exceeded, the developer sees:

```
[PERF WARN] Lua scripts used 6.2ms this frame (budget: 4.0ms). Consider optimising or moving hot logic to C++.
```

### 7.2 Tracy Integration

All Lua system invocations are wrapped in Tracy zones:

```cpp
// In the script system trampoline:
void luaSystemTrampoline(World& world, float dt) {
    ZoneScopedN("Lua:PlayerInput");  // uses the script system name
    // ... call lua update ...
}
```

This means every Lua script appears as a named zone in the Tracy profiler, alongside C++ systems. A developer can see exactly how much time each script consumes relative to rendering, physics, and everything else.

### 7.3 Guidance for Developers

The scripting layer documentation (`.context.md`) will include clear guidance:

- **Do** game logic in Lua: input handling, entity spawning, state machines, UI logic
- **Do not** do per-pixel computation, pathfinding over large grids, or heavy math in Lua
- If a Lua script is too slow, the path is: identify the hot loop with Tracy, move that specific function to C++, expose it as a new Lua binding
- The ECS iteration (`world:each`) crosses the Lua/C++ boundary once and then iterates in C++. Prefer it over manual entity-by-entity access.

---

## 8. Example: A Complete Lua Game Script

This is what making a game with FFE looks like. A bouncing ball that the player controls with arrow keys, bouncing off the edges of the screen.

```lua
-- scripts/ball.lua
-- A bouncing ball you can steer with arrow keys.

local Ball = {}

-- Configuration
local SPEED = 200.0         -- pixels per second
local BALL_SIZE = 32.0
local SCREEN_W = 1280
local SCREEN_H = 720

-- State
local ballId = nil
local velocity = vec2(150, 100)

function Ball.init(world)
    -- Create the ball entity
    ballId = world:spawn()

    world:add(ballId, "Transform", {
        position = vec3(SCREEN_W / 2, SCREEN_H / 2, 0),
        scale    = vec3(1, 1, 1),
    })

    world:add(ballId, "Sprite", {
        size  = vec2(BALL_SIZE, BALL_SIZE),
        color = { r = 0.2, g = 0.8, b = 1.0, a = 1.0 },
        layer = 0,
    })

    log.info("Ball spawned at center of screen!")
end

function Ball.update(world, dt)
    -- Player input: nudge the ball's velocity
    if input.isKeyHeld("Left") then
        velocity.x = velocity.x - SPEED * dt
    end
    if input.isKeyHeld("Right") then
        velocity.x = velocity.x + SPEED * dt
    end
    if input.isKeyHeld("Up") then
        velocity.y = velocity.y - SPEED * dt
    end
    if input.isKeyHeld("Down") then
        velocity.y = velocity.y + SPEED * dt
    end

    -- Quit on Escape
    if input.isKeyPressed("Escape") then
        engine.requestShutdown()
        return
    end

    -- Move the ball
    local t = world:get(ballId, "Transform")
    t.position.x = t.position.x + velocity.x * dt
    t.position.y = t.position.y + velocity.y * dt

    -- Bounce off screen edges
    if t.position.x < 0 then
        t.position.x = 0
        velocity.x = -velocity.x
    elseif t.position.x > SCREEN_W - BALL_SIZE then
        t.position.x = SCREEN_W - BALL_SIZE
        velocity.x = -velocity.x
    end

    if t.position.y < 0 then
        t.position.y = 0
        velocity.y = -velocity.y
    elseif t.position.y > SCREEN_H - BALL_SIZE then
        t.position.y = SCREEN_H - BALL_SIZE
        velocity.y = -velocity.y
    end
end

function Ball.shutdown(world)
    log.info("Ball game shutting down. Thanks for playing!")
end

return Ball
```

And the entry point that wires it up:

```lua
-- scripts/main.lua
local ball = require("ball")

engine.registerSystem("Ball Physics", ball, 100)
```

That is a complete, runnable game. Thirty-five lines of logic (excluding comments and whitespace). A student reads this and understands: I create entities, I add components, I check input, I move things, I bounce off walls. No boilerplate. No ceremony. No engine internals leaking through.

---

## 9. File Layout

```
engine/scripting/
├── CMakeLists.txt              # Links against sol2, LuaJIT, ffe_core
├── .context.md                 # AI-native documentation for the scripting system
├── script_engine.h             # ScriptEngine class: owns sol::state, sandbox, lifecycle
├── script_engine.cpp           # State creation, sandbox construction, script loading
├── lua_bindings.h              # Declaration of all bindXxx() functions
├── lua_bindings.cpp            # sol2 usertype registrations for all exposed types
├── script_system.h             # scriptSystemUpdate() — the C++ trampoline for Lua update()
├── script_system.cpp           # Trampoline implementation, timing, error handling
├── lua_allocator.h             # Tracking allocator for memory limits
└── lua_allocator.cpp           # Allocator implementation with configurable cap
```

### 9.1 ScriptEngine Class

```cpp
// engine/scripting/script_engine.h
#pragma once

#include "core/types.h"

// Forward declarations — sol2 is an implementation detail
namespace sol { class state; }

namespace ffe {

class World;

enum class ScriptTrustLevel : u8 {
    TRUSTED,      // JIT enabled, hook at higher interval — developer must opt in explicitly
    UNTRUSTED     // JIT disabled, instruction hook with wall-clock check always active
};

struct ScriptEngineConfig {
    const char* scriptDirectory = "scripts";
    const char* entryPoint      = "main.lua";
    size_t memoryLimitBytes     = 64 * 1024 * 1024;  // 64 MB default (LEGACY)
    size_t perAllocationMaxBytes = 16 * 1024 * 1024;  // 16 MB per-allocation cap
    ScriptTrustLevel trustLevel = ScriptTrustLevel::UNTRUSTED;  // Safe by default
};

class ScriptEngine {
public:
    explicit ScriptEngine(const ScriptEngineConfig& config);
    ~ScriptEngine();

    // Non-copyable, non-movable (owns the Lua state)
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    Result init(World& world);
    void shutdown(World& world);

    // Hot reload (dev builds only)
    void checkForChanges(World& world);

    // Memory stats for profiling
    size_t luaMemoryUsed() const;
    size_t luaMemoryLimit() const;

private:
    ScriptEngineConfig m_config;
    // sol::state stored via pimpl to avoid sol2 in the header
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ffe
```

Note the pimpl pattern. sol2 is a heavy header. By keeping it out of `script_engine.h`, we avoid pulling sol2 (and therefore LuaJIT headers) into every translation unit that includes the scripting interface. Only `script_engine.cpp` and `lua_bindings.cpp` pay the sol2 compile-time cost.

---

## 10. Dependencies

### 10.1 Required

| Dependency | Source | Notes |
|---|---|---|
| **sol2** (v3.x) | vcpkg (`sol2` in `vcpkg.json`) | Already declared. Header-only. |
| **LuaJIT** (2.1) | System package (`libluajit-5.1-dev`) | Already installed. sol2 links against it. |
| **glm** | vcpkg (`glm` in `vcpkg.json`) | Already declared. Needed for vec2/vec3 bindings. |

### 10.2 vcpkg.json Changes

**None required.** sol2 is already in `vcpkg.json`. sol2's vcpkg port depends on Lua, but we override it to use system LuaJIT. The CMakeLists.txt for `ffe_scripting` must:

1. `find_package(sol2 CONFIG REQUIRED)`
2. `find_package(PkgConfig REQUIRED)` then `pkg_check_modules(LUAJIT REQUIRED luajit)`
3. Link `ffe_scripting` against both sol2 and the system LuaJIT

If vcpkg's sol2 port insists on pulling its own Lua, we set `sol2[luajit]` as the feature or override with a CMake variable. This is a build system detail for `system-engineer` to resolve.

### 10.3 Compile-Time Cost

sol2 is header-only and template-heavy. Binding files (`lua_bindings.cpp`) will be slow to compile. This is acceptable because:

1. Only two `.cpp` files include sol2 (`script_engine.cpp`, `lua_bindings.cpp`)
2. ccache eliminates recompilation on unchanged files
3. The pimpl pattern in `script_engine.h` prevents sol2 from leaking into other translation units

---

## 11. Integration Points

### 11.1 Application Startup / Shutdown

The `Application` class (ADR-001 Section 3.1) owns a `ScriptEngine` instance. During startup step 6:

```cpp
// In Application::startup()
m_scriptEngine = std::make_unique<ScriptEngine>(scriptConfig);
Result r = m_scriptEngine->init(m_world);
if (!r) {
    FFE_LOG_ERROR("Script engine init failed: {}", r.message());
    // Engine continues without scripting — headless/test mode is valid
}
```

During shutdown step 6:

```cpp
m_scriptEngine->shutdown(m_world);
m_scriptEngine.reset();
```

### 11.2 System Priority Integration

Lua script systems are registered at priorities 100-199 (the gameplay/scripting range from ADR-001 Section 3.3). They coexist with C++ systems in the same sorted list:

```
Priority 0-99:    C++ Input System (polls GLFW, updates key states)
Priority 100-199: Lua Script Systems (game logic, AI, HUD updates)
Priority 200-299: C++ Physics System (runs Jolt, resolves collisions)
Priority 300-399: C++ Animation System (future)
Priority 400-499: C++ Audio System (future)
Priority 500+:    C++ Render Preparation (builds draw lists)
```

Input runs before scripts so that `input.isKeyPressed()` returns correct values. Physics runs after scripts so that entity positions set by scripts are picked up by the physics simulation. This ordering is not accidental.

### 11.3 Input System Access

The input API exposed to Lua (Section 2.2) reads from the same input state that the C++ input system writes. The C++ input system (priority 0-99) polls GLFW for key/mouse state and writes it to a struct. The Lua `input.*` functions read from that struct. There is no Lua-side input polling — Lua scripts never talk to GLFW directly.

```cpp
// The input state struct (owned by the input system, read by Lua bindings)
struct InputState {
    // Key states: indexed by key enum, each is a bitfield (pressed, held, released)
    // Mouse position, button states
    // ... (defined in ADR-003)
};
```

The Lua binding reads `InputState` through a const reference. Scripts cannot modify input state.

### 11.4 Hot Reload in the Main Loop

In dev builds, `Application::tick()` calls `m_scriptEngine->checkForChanges(m_world)` once per frame before running systems. This is at the top of `tick()`, before any system executes, ensuring that reloaded scripts see a consistent world state.

---

## 12. Implementation Checklist

Implementation is split into phases. Each phase is independently testable and mergeable.

### Phase 1: Bare Bones (Script Engine Skeleton)
- [ ] `engine/scripting/CMakeLists.txt` — links sol2 and LuaJIT
- [ ] `lua_allocator.h/cpp` — tracking allocator with configurable memory cap
- [ ] `script_engine.h/cpp` — creates `sol::state`, builds empty sandbox environment, loads and executes a `.lua` file
- [ ] Unit test: load a trivial Lua script, verify it runs, verify sandbox blocks `os.execute`
- [ ] Unit test: verify memory limit triggers out-of-memory on runaway allocation

### Phase 2: Type Bindings
- [ ] `lua_bindings.h/cpp` — bind `vec2`, `vec3`, `vec4` with arithmetic operators
- [ ] Bind `Transform` component (position, scale, rotation)
- [ ] Bind `Sprite` component (size, color, layer, sortOrder, uvMin, uvMax)
- [ ] Bind `world:spawn()`, `world:destroy()`, `world:isValid()`
- [ ] Bind `world:add()`, `world:get()`, `world:has()`, `world:remove()` with string-based component lookup
- [ ] Unit test: create entity from Lua, add Transform, modify position, verify in C++

### Phase 3: Iteration and Systems
- [ ] Bind `world:each("Comp1", "Comp2", ...)` — Lua iterator over EnTT views
- [ ] `script_system.h/cpp` — C++ trampoline that calls Lua `update(world, dt)`
- [ ] `engine.registerSystem()` binding — register Lua tables as ECS systems
- [ ] Integration test: register a Lua system, run one tick, verify entity state changed

### Phase 4: Input and Engine API
- [ ] Bind `input.isKeyPressed()`, `input.isKeyHeld()`, `input.isKeyReleased()`
- [ ] Bind `input.mousePosition()`, `input.isMousePressed()`, `input.isMouseHeld()`
- [ ] Bind `log.info()`, `log.warn()`, `log.error()`
- [ ] Bind `engine.requestShutdown()`, `engine.tier()`
- [ ] Bind `math.*` extensions (lerp, clamp, distance, normalize, random)

### Phase 5: Module System and Entry Point
- [ ] Custom `require()` — load from `scripts/` only, path validation, caching
- [ ] `main.lua` loading and execution
- [ ] Integration test: multi-file game with `main.lua` requiring other scripts

### Phase 6: Hot Reload (Dev Only)
- [ ] `inotify` file watcher for `scripts/` directory
- [ ] Reload mechanism: shutdown old script, load new file, re-init
- [ ] Integration test: modify a script file, verify reload occurs

### Phase 7: Hardening
- [ ] Instruction hook with wall-clock time check — active in ALL builds (Section 2.5)
- [ ] `LUAJIT_MODE_OFF` for UNTRUSTED trust level
- [ ] Per-allocation size cap (16 MB) in tracking allocator
- [ ] Test: `string.rep("x", 2^30)` triggers graceful Lua error, not crash
- [ ] Per-script timing with Tracy zones
- [ ] Budget warning system
- [ ] Verify `load()`, `setmetatable()`, `math.randomseed`, `coroutine`, `ffi`, `jit` are absent from sandbox
- [ ] Verify pcall/xpcall errors are logged at DEBUG level
- [ ] Verify component proxy objects validate entity liveness on every access
- [ ] Verify require path validation rejects: leading dots, trailing dots, `..`, names > 128 chars
- [ ] Verify resolved require paths are within scripts/ directory
- [ ] Fuzz test: feed garbage Lua to the sandbox, verify no crashes
- [ ] Security audit by `security-auditor` agent

---

## 13. What This Prevents (and Why That Is OK)

This section lists things the Lua scripting layer intentionally does not support. Every exclusion has a reason.

### No Filesystem Access from Lua

Scripts cannot read or write files. This means no save games written from Lua, no level editors that export to disk from Lua, and no runtime asset loading from Lua.

**Why that is OK:** Save games and asset loading are engine-level features exposed through safe, validated APIs (future ADRs). When we add save/load, it will be `engine.save("slot1", data)` and `engine.load("slot1")` — functions that write to a designated save directory with validated paths, not raw `io.open`. The engine provides the safety layer; Lua provides the game logic.

### No Network Access from Lua

Scripts cannot open sockets, make HTTP requests, or communicate with external services.

**Why that is OK:** Networking is an engine feature (future ADR) exposed through a safe API. When multiplayer is added, scripts will call `net.send(peerId, data)` and handle `net.onReceive(callback)`. The engine validates packet sizes, enforces rate limits, and prevents address spoofing. Raw socket access from an untrusted scripting layer would be a catastrophic security hole.

### No Coroutines

Lua coroutines (`coroutine.create`, `coroutine.resume`, `coroutine.yield`) are **explicitly blacklisted** and never present in the sandbox. They add complexity to the execution model, make the instruction count hook harder to reason about, and can be used to evade time budget enforcement.

**Why that is OK:** Coroutines are primarily used for "wait for N seconds" patterns, which are better served by a timer API (`engine.after(seconds, callback)`) that the engine manages. The coroutine library is on the explicit blacklist (Section 2.3) and must not be added without a full security review.

### No Direct C Pointer or Userdata Manipulation

Lua scripts receive engine objects as sol2 usertypes with locked metatables. They cannot forge pointers, reinterpret memory, or access internal engine state that is not explicitly bound.

**Why that is OK:** This is the entire point of the sandbox. Type safety at the Lua/C++ boundary is non-negotiable. sol2 enforces this at compile time.

### No Dynamic Component Definition from Lua

Scripts cannot create new component types at runtime. Components are defined in C++, registered at startup, and accessed by name.

**Why that is OK:** ECS component types must be known at compile time for EnTT to allocate storage efficiently. Allowing runtime component definition would require a property-bag system that defeats the purpose of ECS (data locality, cache efficiency). If a game needs custom data, it uses a "ScriptData" component that holds a Lua table reference — but the component type itself is defined in C++.

### No Multi-threaded Lua Execution

All Lua execution happens on the main thread, during the system update phase. There is no parallel Lua execution.

**Why that is OK:** LuaJIT is not thread-safe. Multiple `lua_State` instances can run on different threads, but they cannot share data without synchronisation. For LEGACY tier (single-core safe), main-thread-only execution is the correct design. On STANDARD/MODERN tiers, the heavy lifting (physics, rendering) is already parallelised in C++. Lua handles game logic, which is inherently sequential (this happens, then that happens). If profiling shows Lua is the bottleneck, the answer is to move the hot code to C++, not to parallelise Lua.

---

## Appendix A: Sandbox Construction Pseudocode

This is the authoritative reference for how the sandbox is built. `security-auditor` should review this against the whitelist/blacklist in Section 2.

```cpp
void ScriptEngine::buildSandbox(sol::state& lua) {
    // 1. Create a new, empty environment table
    sol::environment sandbox(lua, sol::create);

    // 2. Add safe standard library functions (whitelist)
    sandbox["type"]     = lua["type"];
    sandbox["tostring"] = lua["tostring"];
    sandbox["tonumber"] = lua["tonumber"];
    sandbox["pairs"]    = lua["pairs"];
    sandbox["ipairs"]   = lua["ipairs"];
    sandbox["next"]     = lua["next"];
    sandbox["select"]   = lua["select"];
    sandbox["unpack"]   = lua["unpack"];
    sandbox["pcall"]    = lua["pcall"];
    sandbox["xpcall"]   = lua["xpcall"];
    sandbox["error"]    = lua["error"];
    sandbox["assert"]   = lua["assert"];

    // 3. Add safe string library (minus string.dump)
    sol::table safeString(lua, sol::create);
    sol::table origString = lua["string"];
    for (auto& [k, v] : origString) {
        std::string key = k.as<std::string>();
        if (key != "dump") {
            safeString[k] = v;
        }
    }
    sandbox["string"] = safeString;

    // 4. Add safe table library
    sandbox["table"] = lua["table"];

    // 5. Add safe math library + FFE extensions
    sol::table safeMath(lua, sol::create);
    sol::table origMath = lua["math"];
    for (auto& [k, v] : origMath) {
        std::string key = k.as<std::string>();
        if (key != "randomseed") {  // Engine controls RNG seeding
            safeMath[k] = v;
        }
    }
    safeMath["lerp"]      = /* ... */;
    safeMath["clamp"]     = /* ... */;
    safeMath["distance"]  = /* ... */;
    safeMath["normalize"] = /* ... */;
    safeMath["random"]    = /* engine-seeded RNG, replaces os-dependent math.random */;
    safeMath["randomInt"] = /* ... */;
    sandbox["math"] = safeMath;

    // 6. Add engine APIs
    sandbox["world"]  = /* bound World proxy */;
    sandbox["input"]  = /* bound InputState reader */;
    sandbox["log"]    = /* bound logging functions */;
    sandbox["engine"] = /* bound engine control functions */;
    sandbox["vec2"]   = /* vec2 constructor */;
    sandbox["vec3"]   = /* vec3 constructor */;
    sandbox["print"]  = sandbox["log"]["info"];  // redirect print to log

    // 7. Custom require (Section 2.4)
    sandbox["require"] = /* engine module loader */;

    // 8. Self-referential _G — some Lua idioms reference _G explicitly.
    //    Pointing it at the sandbox itself is safe and prevents scripts from
    //    discovering the real global table.
    sandbox["_G"] = sandbox;

    // 9. NOTHING ELSE. The sandbox is complete.
    // Explicitly absent (never loaded, never added):
    //   os, io, debug, package, loadfile, dofile, load, collectgarbage,
    //   rawget, rawset, setfenv, getfenv, setmetatable, newproxy,
    //   coroutine library, ffi library, jit library, math.randomseed
    //
    // IMPORTANT: When opening sol::lib selectors, do NOT open:
    //   sol::lib::ffi, sol::lib::jit, sol::lib::os, sol::lib::io,
    //   sol::lib::debug, sol::lib::package
    // Only open: sol::lib::base, sol::lib::math, sol::lib::string,
    //            sol::lib::table — then selectively copy safe items into sandbox.

    m_sandbox = std::move(sandbox);
}
```

Every script loaded by the engine executes with this sandbox as its environment. There is no global Lua state accessible to game scripts. The sandbox IS their world.
