# Post-Implementation Security Review: Entity Lifecycle Lua Bindings

**Reviewer:** security-auditor
**Date:** 2026-03-06
**Implementation reviewed:** `engine/scripting/script_engine.cpp` — `registerEcsBindings()`, entity lifecycle section (lines 562–825)
**Shift-left design review:** `design-note-entity-from-lua-security-review.md`
**Review type:** Post-implementation verification — confirms conditions from shift-left review were met

---

## Verdict: PASS WITH MINOR ISSUES

All HIGH and MEDIUM conditions from the shift-left review are correctly implemented. Two LOW issues are noted below; neither blocks merging.

---

## Condition Verification

### H-1 — Full two-sided entity ID range check in all new bindings

**Status: PASS**

`MAX_ENTITY_ID` is defined at line 588 as `static_cast<lua_Integer>(UINT32_MAX - 1u)`. All four new bindings apply `rawId < 0 || rawId > MAX_ENTITY_ID` before any cast or ECS access:

| Binding | Line | Check present |
|---------|------|--------------|
| `destroyEntity` | 627 | Yes |
| `addTransform` | 653 | Yes |
| `addSprite` | 708 | Yes |
| `addPreviousTransform` | 791 | Yes |

None of the new bindings copy the weaker single-sided check (`rawId < 0` only) from the pre-existing `getTransform`/`setTransform` bindings.

### H-2 — Overwrite path uses `emplace_or_replace`, not `emplace`

**Status: PASS**

All three component-adding bindings use `world->registry().emplace_or_replace<T>(static_cast<entt::entity>(entityId))` directly via the raw registry escape hatch:

| Binding | Line | Method |
|---------|------|--------|
| `addTransform` | 681 | `emplace_or_replace<ffe::Transform>` |
| `addSprite` | 762 | `emplace_or_replace<ffe::Sprite>` |
| `addPreviousTransform` | 819 | `emplace_or_replace<ffe::PreviousTransform>` |

No binding calls `world->addComponent<T>()` unconditionally.

### M-1 — `addSprite` rejects `rawHandle <= 0`, not `rawHandle < 0`

**Status: PASS**

Line 720: `if (rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(UINT32_MAX))`. The boundary correctly rejects zero (the `TextureHandle{id=0}` null sentinel). Passing `0` as a handle returns `false` before any `TextureHandle` is constructed.

### M-2 — Dangling texture handle documented in `.context.md` "What not to do" section

**Status: PASS**

`.context.md` lines 358–379 contain the entry "Do not hold texture handles across scene transitions where C++ may unload them (dangling handle risk)". The entry includes:
- An explanation of why the scripting layer cannot verify handle liveness
- A concrete bad example (holding a Lua integer handle across a scene boundary after C++ unloads the texture)
- A concrete good example (using handles only within the C++-guaranteed lifetime)
- An explicit teardown sequence (remove Sprite components, then unload, then re-create)

The condition is satisfied.

---

## Additional Checks

### NaN/Inf rejection in `addTransform`

`std::isfinite()` is applied to all five float arguments (x, y, rotation, scaleX, scaleY) at lines 670–675 before any assignment. Returns `false` and logs on rejection. PASS.

### NaN/Inf rejection in `addSprite` — width and height

`std::isfinite(width)` and `std::isfinite(height)` are checked at line 732, combined with a `> 0.0` positivity check. Returns `false` and logs on rejection. PASS.

### Colour clamping in `addSprite`

Lines 744–747 apply `std::max(0.0, std::min(1.0, rawR/G/B/A))` to each channel. The intent is correct. See LOW-1 below for a NaN propagation detail.

### `createEntity` under ECS exhaustion

Line 604 checks `entityId == ffe::NULL_ENTITY` and returns `nil` (not a crash). The World pointer nil-check at line 595 precedes the `createEntity` call. PASS.

### World pointer retrieval pattern

All new bindings follow the same pattern as existing bindings: `lua_pushlightuserdata(state, &s_worldRegistryKey)` → `lua_gettable(state, LUA_REGISTRYINDEX)` → `lua_isnil` check → `lua_touserdata`. No new binding skips the nil check or takes a different retrieval path. PASS.

### New attack surface

No new attack surface beyond what was in scope for the shift-left review. The new bindings introduce entity creation and component mutation, both of which were analysed in the design note. No new Lua globals are exposed outside the `ffe.*` table. No file I/O or network access is added.

---

## LOW Findings (advisory — do not block merge)

**LOW-1: NaN may propagate through colour clamping into `Sprite.color`**

`std::max(0.0, std::min(1.0, NaN))` is unspecified by the C++ standard — comparison operations involving NaN always return false, so `std::min(1.0, NaN)` may return `NaN` on most implementations (NaN is returned as the second argument when comparison is false). If `rawR/G/B/A` is NaN (e.g., passed as `0/0` from Lua's math operations), the clamp may silently store NaN into `Sprite.color.r/g/b/a`. The render system clamps colour at pack time, so the GPU output is not corrupted (the existing render-layer defence noted in L-3 of the shift-left review holds), but the component itself stores NaN for one frame until the render clamp fires.

**Recommended fix (non-blocking):** Apply `std::isfinite()` checks to colour channels before clamping, or use a NaN-safe clamp (`std::isnan(v) ? 0.0f : clamp(v)`) to ensure the stored component value is always well-defined.

**LOW-2: Layer `lua_Integer` to `i16` narrowing cast is C++ UB for out-of-range values**

Line 751 casts `rawLayer` (a `lua_Integer`, i.e., 64-bit signed integer) to `ffe::i32`, then line 767 casts `layerClamped` to `ffe::i16`. If `rawLayer` is outside `[-32768, 32767]`, the `i16` narrowing is undefined behaviour in C++, even though the render system subsequently clamps the value to `[0, 15]` before using it. The warning log at lines 752–756 fires for values outside `[0, 15]`, which is a narrower band than the UB range. A value of `32768` would be logged as a warning but would also trigger signed narrowing UB in the cast.

This was noted as a risk in the shift-left review (L-4) and the blast radius is confirmed limited (render-layer clamping prevents sort key overflow). However, UB in the binding itself is still UB.

**Recommended fix (non-blocking):** Clamp `rawLayer` to `[-32768, 32767]` (or `[0, 15]`) before casting to `i16`, eliminating the UB regardless of render-layer defences.

---

## Summary

| Condition | Status |
|-----------|--------|
| H-1: Full two-sided entity ID check in all four new bindings | PASS |
| H-2: `emplace_or_replace` used in `addTransform`, `addSprite`, `addPreviousTransform` | PASS |
| M-1: Texture handle rejects `<= 0` (not `< 0`) | PASS |
| M-2: Dangling texture handle documented in `.context.md` "What not to do" | PASS |
| NaN/Inf rejection in `addTransform` | PASS |
| NaN/Inf rejection in `addSprite` width/height | PASS |
| Colour NaN propagation | LOW-1 (advisory) |
| Layer `i16` narrowing UB | LOW-2 (advisory) |
| `createEntity` exhaustion safety | PASS |
| World pointer retrieval pattern | PASS |
| New attack surface | None found |
