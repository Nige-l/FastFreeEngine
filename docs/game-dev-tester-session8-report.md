# game-dev-tester Usage Report — Session 8 Entity Lifecycle Bindings

**Date:** 2026-03-06
**Tester:** game-dev-tester agent
**Scope:** Entity lifecycle Lua bindings added in Session 8

---

## Summary

All five Session 8 bindings (`ffe.createEntity`, `ffe.destroyEntity`, `ffe.addTransform`, `ffe.addSprite`, `ffe.addPreviousTransform`) are functional and well-behaved from a game developer perspective. The demo was written successfully. No blockers were encountered. Two friction points were identified, one of which is a missing binding that limits what Lua scripts can do independently.

---

## What Was Built

`examples/lua_demo/game.lua` was updated to demonstrate entity lifecycle from Lua:

- **Follower entity:** created on the first `update()` call using `ffe.createEntity()` + `ffe.addTransform()` + `ffe.addSprite()` + `ffe.addPreviousTransform()`. It chases the player using a direction vector each tick.
- **Spawn-on-SPACE:** pressing SPACE spawns a 16x16 cyan static marker at the player's current position, capped at 5. Markers use `ffe.createEntity()` + `ffe.addTransform()` + `ffe.addSprite()` without `addPreviousTransform` (they never move, so no interpolation is needed).
- **Player movement:** WASD as before, screen-clamped. ESC still calls `ffe.requestShutdown()`.

Visual verification was not possible in this session (no display). Structural and syntactic correctness was verified by reading and cross-referencing the implementation.

---

## Binding-by-Binding Report

### `ffe.createEntity()` -> integer or nil

**Status: Works as documented.**

The nil-on-failure return is the right design. Writing the nil check in Lua is natural:

```lua
local id = ffe.createEntity()
if id == nil then
    ffe.log("ERROR: entity cap reached")
    return
end
```

One small friction point: the error message logged by the C++ binding says "ECS out of space", but the .context.md does not mention what the practical entity cap is. A game developer who hits nil without knowing the cap will have trouble diagnosing whether they hit a bug or the limit. Recommend adding the cap number (or a reference to it) to the .context.md entry for `createEntity`.

### `ffe.destroyEntity(entityId)` -> nothing

**Status: Works as documented.**

Correct silent no-op for invalid IDs. No complaints. Not exercised in the demo (no pickup or death mechanic needed), but the test suite confirms it works.

### `ffe.addTransform(entityId, x, y, rotation, scaleX, scaleY)` -> bool

**Status: Works as documented.**

The bool return was useful during authoring — it allowed detecting failure without crashing:

```lua
local ok = ffe.addTransform(id, 200.0, -150.0, 0.0, 1.0, 1.0)
if not ok then
    ffe.log("ERROR: addTransform failed")
end
```

The overwrite-with-warning behaviour (documented in the C++ implementation via `emplace_or_replace`) is acceptable for a demo. For a production game, it would be worth calling `ffe.getTransform` first to detect accidental double-add, but the engine does not crash, which is the important thing.

One documentation gap: the .context.md does not explicitly state the argument order for `addTransform`. The current order is `(entityId, x, y, rotation, scaleX, scaleY)` — this matches `setTransform`, which is consistent. However, `getTransform` returns a table with field names, not positional returns. A developer who copies from `setTransform` will get it right, but it is worth stating the order explicitly in the .context.md entry to avoid confusion.

### `ffe.addSprite(entityId, texHandle, width, height, r, g, b, a, layer)` -> bool

**Status: Functional, but with a significant friction point (see below).**

Nine arguments in a fixed positional call is workable but verbose. Reading this at a glance is harder than, say, `ffe.setTransform`:

```lua
-- Less readable than it could be:
ffe.addSprite(id, MARKER_TEX, 16.0, 16.0, 0.2, 0.9, 0.9, 0.8, 3)
```

A named-table form (e.g. `ffe.addSprite({entity=id, tex=1, w=16, h=16, ...})`) would improve readability, but that is an API design question for api-designer, not a blocker.

The colour clamping behaviour (NaN -> 0.0, out-of-range -> clamped) is safe and the .context.md documents the dangling-handle warning clearly. No complaints on correctness.

**Layer range:** the implementation clamps the layer to [0, 15] with a warning. This is not documented in the .context.md. A developer passing layer 16 or -1 will get a silent correction. The range and the clamping behaviour should be in the .context.md.

### `ffe.addPreviousTransform(entityId)` -> bool

**Status: Works as documented.**

The behaviour of automatically copying the existing Transform's position when called is a genuine quality-of-life feature. Without it, a newly spawned entity would flicker on its first interpolated frame. The warning logged when called before `addTransform` is correct and helpful.

The documented call order (addTransform before addPreviousTransform) is logical and consistent with how C++ sets up entities. This is stated as a warning in the .context.md, which is the right place for it.

---

## Friction Points

### 1. No way to pass a texture handle from C++ to Lua (Blocker for real games, not this demo)

**Severity: Minor for demos; significant for real games.**

To use `ffe.addSprite` from Lua, a game developer needs a valid texture handle integer. Currently there is no binding to retrieve a handle by name or path. The only workaround documented in .context.md is to use handle `1` as a headless stub, or to hard-code a handle integer obtained by inspecting C++ code.

For any game that loads more than one texture, this makes `ffe.addSprite` largely unusable from Lua without a further binding such as:

```lua
-- hypothetical binding (does not exist yet):
local tex = ffe.loadTexture("enemy.png")
ffe.addSprite(id, tex, 32, 32, 1, 1, 1, 1, 0)
```

Until such a binding exists, game logic that spawns textured entities from Lua must receive the handle as a parameter passed from C++ (e.g. stored in a Lua global by the host before calling `doFile`). This is not documented as a pattern in .context.md.

**Recommendation:** add a section to the scripting .context.md describing the current limitation and the workaround (C++ stores handle in a Lua global before `doFile`). Track the `ffe.loadTexture` binding as a follow-up item.

### 2. `ffe.addSprite` layer range not documented

**Severity: Minor.**

The layer argument is clamped to [0, 15] silently (with a warning logged, but no error returned). The valid range is not stated in the .context.md API table for `addSprite`. A developer who passes layer 20 expecting it to be a high-priority layer will get layer 15 instead without a visible error.

**Recommendation:** add "layer: integer in [0, 15]; values outside this range are clamped with a warning log" to the .context.md entry.

---

## What Worked Great

- **Nil-on-failure is consistent across all five bindings.** `createEntity` returns nil; the others return false. This matches the documented contract exactly. Writing defensive Lua against these is straightforward.
- **The bool return from addTransform / addSprite / addPreviousTransform** is the right choice. It allows game scripts to log and recover without the engine crashing.
- **addPreviousTransform auto-copying the Transform position** prevents the frame-1 flicker problem silently. This is a detail a game developer would almost certainly get wrong if they had to do it manually.
- **All bindings are safe with invalid entity IDs.** The two-sided range check means game scripts cannot accidentally corrupt the engine by passing -1 or a stale ID.
- **Module-level state works correctly.** Lua upvalues (followerEntityId, markerCount, markers table) survive across `update()` calls. This is the natural way to manage per-entity Lua state and it works as expected with the `callFunction` dispatch pattern.
- **The instruction budget (1,000,000 per call) was not hit** during the demo's update() logic. Simple chase math and a couple of table reads per tick is well within budget.

---

## Blockers

None. The demo was written successfully. All five bindings produced the expected component-creation behaviour based on code reading and cross-referencing with the test suite. The only limitation (no texture handle binding) is a missing feature, not a bug in the delivered bindings.

---

## API Discoverability Score

**Entity lifecycle bindings specifically: 7 / 10**

**Rationale:**

The five new bindings are logically grouped and named consistently (add*, create*, destroy*). The return value contract (integer or nil for create, bool for add*, nothing for destroy*) is coherent and easy to remember. Comparing `addTransform`'s argument order with `setTransform`'s is intuitive — they match.

Points deducted:

- (-1) No documented workaround for the texture handle gap in the scripting .context.md. A developer attempting `addSprite` from scratch will spend time figuring out what to pass for the handle.
- (-1) The layer range [0, 15] for `addSprite` is not in the .context.md. A developer writing `layer=20` will get a silent correction.
- (-1) The entity cap (the number at which `createEntity` returns nil) is not mentioned in the .context.md.

For a game developer pointing an LLM at the .context.md and asking it to write entity-spawning Lua, the LLM would produce mostly correct code but would likely use a placeholder comment for the texture handle and possibly pass an out-of-range layer. That is acceptable for a first draft but requires a fix pass. The binding design itself is solid.

---

## Files Modified

- `/home/nigel/FastFreeEngine/examples/lua_demo/game.lua` — updated to demonstrate entity lifecycle bindings
- `/home/nigel/FastFreeEngine/docs/game-dev-tester-session8-report.md` — this file
