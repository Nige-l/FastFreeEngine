# Design Note: Scene Management (Phase 1)

**Status:** Proposed
**Author:** architect
**Date:** 2026-03-06
**Related:** ADR-004 (Lua scripting), design-note-physics-2d.md (collision callbacks)
**Tier support:** ALL (RETRO, LEGACY, STANDARD, MODERN) — no GPU involvement

---

## 1. Scope

Phase 1 scene management provides the minimum viable API for Lua games to transition between scenes (title screen, gameplay, game over, etc.) without restarting the engine. There is **no scene file format**, no scene graph, no serialization, and no editor integration. Those are Phase 3+ concerns.

Phase 1 delivers four primitives:

| API | Layer | Purpose |
|-----|-------|---------|
| `ffe.destroyAllEntities()` | Lua | Nuclear reset: destroy all entities, free heap-owning components, cancel timers, clear collision callback |
| `ffe.cancelAllTimers()` | Lua | Cancel all timers without touching entities |
| `ffe.loadScene(scriptPath)` | Lua | Validate path, execute a Lua script file (scene script) |
| `World::clearAllEntities()` | C++ | Wrapper around `m_registry.clear()` |

All four are **cold-path operations**. None run per-frame. None touch the GPU.

### What Phase 1 does NOT cover

- Scene file format (`.scene`, `.json`, `.yaml`, etc.)
- Scene graph / hierarchy
- Serialization / deserialization
- Asset bundling or asset lifecycle (textures and sounds are not unloaded on scene change)
- Editor scene view
- Async scene loading / loading screens

---

## 2. Design Rationale

### Why not a scene object?

A `Scene` class that owns entities, systems, and state would be the traditional engine approach. We avoid this in Phase 1 because:

1. **FFE has one World.** The ECS is a single `World` instance. A separate `Scene` abstraction adds complexity with no benefit at this stage.
2. **Lua scripts already define scenes implicitly.** A Lua file that creates entities, sets up callbacks, and defines `update()` is a scene.
3. **Simplicity for learners.** `ffe.destroyAllEntities()` then `ffe.loadScene("level2.lua")` should just work.

### The state-machine pattern remains valid

Many FFE games use a single Lua file with a `gameState` variable and branch in `update()`. This pattern is not deprecated. `loadScene` is an **alternative** for larger games that benefit from splitting logic across multiple files.

### Why not unload textures/sounds on scene change?

Asset lifecycle is orthogonal to scene management. Games that need to reclaim memory can call `ffe.unloadTexture()` and `ffe.unloadSound()` explicitly. Automatic asset lifecycle is a future feature.

---

## 3. API Detail

### 3.1 `World::clearAllEntities()` (C++)

```cpp
inline void World::clearAllEntities() {
    m_registry.clear();
}
```

Delegates to `entt::registry::clear()`, which destroys all entities and all components. Does **not** clear ECS context values (ClearColor, CameraShake, etc.). Does **not** free heap memory owned by components — that is the caller's responsibility.

### 3.2 `ffe.destroyAllEntities()` (Lua)

The "nuclear reset" for scene transitions. Performs these steps in order:

1. **Free Tilemap heap data.** Iterate `Tilemap` view, call `destroyTilemap()` on each.
2. **Clear the collision callback.** `luaL_unref` the callback stored at `s_collisionCallbackKey`, set to `LUA_NOREF`.
3. **Cancel all active timers.** `luaL_unref` each callback ref, zero the timer array, reset `m_timerCount`.
4. **Clear all entities.** Call `World::clearAllEntities()`.

#### Component cleanup contract

> **Rule:** Any component whose destructor must free heap memory must have a cleanup step added to `destroyAllEntities` **before** the `clearAllEntities()` call.

Current heap-owning components:
- `Tilemap` — owns `u16* tiles` (freed via `destroyTilemap()`)

### 3.3 `ffe.cancelAllTimers()` (Lua)

Cancels all timers without touching entities. Useful for game phase transitions.

### 3.4 `ffe.loadScene(scriptPath)` (Lua)

Loads and executes a Lua script file, enabling multi-file game structures.

**Behavior:**
1. Validate argument is a string.
2. Apply `isPathSafe()` checks (no `../`, no absolute paths).
3. Retrieve stored asset root from `ScriptEngine`.
4. Call `doFile()` internally.
5. Return nothing. Failures are logged.

**Asset root storage:** Add `m_assetRoot[512]` field and `setScriptRoot()` method to `ScriptEngine`. Write-once to match renderer's security posture.

**Re-entrancy:** Not guarded. The Lua call stack depth (~200 frames) and instruction budget (1M) provide natural limits.

---

## 4. What destroyAllEntities Resets

| State | Reset? | Notes |
|-------|--------|-------|
| All entities and components | YES | `m_registry.clear()` |
| Tilemap heap data | YES | `destroyTilemap()` per entity |
| Active timers | YES | `luaL_unref` each callback |
| Collision callback | YES | Must unref and nil |
| Lua global variables | NO | Persist across `loadScene` |
| Lua `update()` function | REPLACED | New scene overwrites global |
| Loaded textures | NO | Until `unloadTexture()` |
| Loaded sounds | NO | Until `unloadSound()` |
| Background color | NO | ECS context survives clear |
| Camera shake | NO | ECS context survives clear |
| Music playback | NO | Continues across transitions |

---

## 5. Thread Safety

All scene management calls are main-thread-only. Enforced by the constraint that all Lua execution happens on the main thread.

---

## 6. Security Considerations

- **Path traversal:** `loadScene` reuses `doFile()`'s path validation. No new attack surface.
- **Instruction budget:** Scene loading runs within the existing 1M instruction budget.
- **Lua global pollution:** Existing risk, not introduced by `loadScene`.

---

## 7. Future Extensions (not Phase 1)

- Scene stack (push/pop for pause menus)
- Async loading with loading screens
- Scene file format (declarative)
- Lua environment isolation per scene
- Asset lifecycle (auto-unload unreferenced assets)
- Editor integration
