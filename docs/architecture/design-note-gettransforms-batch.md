# Design Note: Batch Transform Query (`ffe.getTransforms`)

**Author:** architect
**Date:** 2026-03-06
**Status:** DRAFT — recommendation: defer
**Addresses:** Issue M-1 getTransform GC pressure (Session 10 open issues)

---

## 1. Problem Statement

`ffe.getTransform(entityId)` creates a new Lua table on every call:

```lua
-- Current: one table allocation per entity per frame
local tf = ffe.getTransform(entityId)
-- tf is {x=..., y=..., z=..., scaleX=..., scaleY=..., rotation=...}
```

At 60 fps with N entities, this produces 60N table allocations per second. For large entity counts this generates GC pressure. The LuaJIT garbage collector is incremental, so this manifests as occasional micro-stutters rather than hard freezes, but it is measurable.

---

## 2. Proposed: `ffe.getTransforms({id1, id2, ...})`

Return a table-of-tables for a batch of entity IDs:

```lua
local transforms = ffe.getTransforms({player, follower, enemy1, enemy2})
-- transforms[1] = {x=..., y=..., ...} for player
-- transforms[2] = {x=..., y=..., ...} for follower
-- etc.
```

### Implementation

```cpp
// ffe_getTransforms: takes a Lua table of entity IDs, returns a table of transform tables.
static int ffe_getTransforms(lua_State* L) {
    // arg 1: table of entity IDs
    // result: table of transform tables (or false for invalid IDs)
    luaL_checktype(L, 1, LUA_TTABLE);
    const int n = static_cast<int>(lua_objlen(L, 1));
    lua_newtable(L); // result table
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, 1, i);
        // ... validate ID, lookup transform, push table or false ...
        lua_rawseti(L, -2, i);
    }
    return 1;
}
```

### Allocation count

- Old: N tables per call (one per entity)
- New: 1 outer table + N inner tables = N+1 per call

The inner tables are the same as before. **This approach does NOT reduce allocations** — it only changes the call pattern from N separate function calls to 1 call returning N tables.

---

## 3. Alternative: Pre-allocated Reused Table

Lua allows reusing a table across frames by passing it in as an out-parameter:

```lua
-- Game allocates result table once
local tfCache = {}

-- Per frame: fill the same table (no allocation)
ffe.fillTransform(entityId, tfCache)
-- tfCache.x, tfCache.y, etc. are updated in-place
```

C++ side:

```cpp
static int ffe_fillTransform(lua_State* L) {
    // arg 1: entity ID
    // arg 2: table to fill (pre-allocated by caller)
    luaL_checktype(L, 2, LUA_TTABLE);
    // ... look up transform, set table fields directly ...
    lua_pushvalue(L, 2);
    return 1; // returns the same table for convenience
}
```

### Allocation count

- 0 allocations per call once the table is pre-allocated by the script

This is the only approach that actually eliminates the GC pressure entirely.

---

## 4. GC Impact Analysis: When Does It Become Visible?

LuaJIT's GC is generational (in theory) but in practice behaves like a stop-the-world mark-and-sweep at collection boundaries. The minor collection threshold depends on the heap size. With default settings:

- Each Lua table allocation is ~40–80 bytes (header + hash part)
- LuaJIT triggers GC roughly when allocated bytes exceed the threshold (default: 200 KB after last collection)
- At 60 fps with N=50 entities: 50 × 70 bytes × 60 = ~210 KB/s of short-lived tables

**With N=50 entities:** GC triggers approximately once per second. Each collection at this allocation rate takes < 0.5 ms on LEGACY-tier hardware. This is below the 1 ms budget threshold and is not user-visible.

**With N=500 entities:** ~2.1 MB/s allocation → GC triggers ~10× per second → occasional 0.5–1 ms pauses → marginally visible at 60 fps, invisible at 30 fps. At this scale, the `getTransform`-per-entity pattern in Lua is questionable for other reasons too (cache misses from ECS lookups).

**With N=2000 entities:** ~8.4 MB/s → GC triggers ~40× per second → stutters become visible even at 60 fps. However, iterating 2000 entities individually in Lua at 60 fps is already impractical for other reasons (Lua function call overhead alone: ~2000 × 0.1 µs = 0.2 ms just for calls).

**Conclusion:** The GC pressure from `getTransform` is not a visible problem until the Lua-script-per-entity pattern itself becomes the bottleneck (~500+ entities). At that point, the right solution is to move entity iteration to C++ (registered ECS systems) rather than doing it from Lua.

---

## 5. Options Summary

| Option | Allocations/Frame (N entities) | Complexity | Notes |
|--------|-------------------------------|------------|-------|
| Current `ffe.getTransform` | N tables | None | Fine up to ~200 entities |
| `ffe.getTransforms(ids)` | N+1 tables | Low | Cosmetically nicer, same GC cost |
| `ffe.fillTransform(id, t)` | 0 (after warmup) | Low | Actually eliminates GC pressure |
| C++ ECS system (iterate in C++) | 0 | Medium | Best for large entity counts |

---

## 6. Recommendation: Add `ffe.fillTransform`, Defer Batch API

### What to implement now

Add `ffe.fillTransform(entityId, table)` as a zero-allocation alternative. This is a 15-line addition to `script_engine.cpp`. Scripts that need per-frame transform reads without GC pressure use it:

```lua
local tfCache = {}  -- allocated once at startup

function update(entityId, dt)
    ffe.fillTransform(entityId, tfCache)
    -- tfCache.x, tfCache.y etc. updated in-place — no GC
end
```

Keep `ffe.getTransform` as-is. It's simpler for one-off reads and for scripts with few entities.

### What to defer

`ffe.getTransforms({ids...})` is a cosmetic improvement only — it does not reduce allocations. **Do not implement the batch version.** The table-of-tables approach produces the same GC pressure as N individual `getTransform` calls and adds C++ complexity for no gain.

The real solution for 500+ entity scenarios is C++ ECS iteration, not Lua batch queries.

---

## 7. Implementation Spec for `ffe.fillTransform`

```cpp
// ffe_fillTransform(entityId: integer, outTable: table) -> table
// Writes transform fields into an existing Lua table.
// No table allocation — outTable is modified in-place.
// Returns outTable for chaining convenience.
// Returns false if entityId is invalid or has no Transform component.
static int ffe_fillTransform(lua_State* L) {
    // Arg validation: same two-sided ID check as ffe.getTransform
    const lua_Integer rawId = luaL_checkinteger(L, 1);
    if (rawId < 0 || rawId > static_cast<lua_Integer>(UINT32_MAX - 1u)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    luaL_checktype(L, 2, LUA_TTABLE);

    // World null-guard
    ffe::World* world = /* ... retrieve from registry ... */;
    if (!world) { lua_pushboolean(L, 0); return 1; }

    const auto entity = static_cast<entt::entity>(static_cast<ffe::u32>(rawId));
    const ffe::Transform* tf = world->registry().try_get<ffe::Transform>(entity);
    if (!tf) { lua_pushboolean(L, 0); return 1; }

    // Fill existing table — no new table allocation
    lua_pushvalue(L, 2);       // push outTable to top
    lua_pushnumber(L, static_cast<lua_Number>(tf->position.x));
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, static_cast<lua_Number>(tf->position.y));
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, static_cast<lua_Number>(tf->position.z));
    lua_setfield(L, -2, "z");
    lua_pushnumber(L, static_cast<lua_Number>(tf->scale.x));
    lua_setfield(L, -2, "scaleX");
    lua_pushnumber(L, static_cast<lua_Number>(tf->scale.y));
    lua_setfield(L, -2, "scaleY");
    lua_pushnumber(L, static_cast<lua_Number>(tf->rotation));
    lua_setfield(L, -2, "rotation");
    return 1;  // return outTable
}
```

**Security:** Same entity ID validation as `ffe.getTransform`. No new attack surface.

---

## 8. Not In Scope

- Batch C++ transform writes from Lua (not needed until 500+ entity scripts)
- LuaJIT FFI-based struct access (changes sandbox model significantly — future ADR needed)
- Transform interpolation queries from Lua (PreviousTransform is internal to the renderer)
