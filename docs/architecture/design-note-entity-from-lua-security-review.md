# Security Review: Entity Lifecycle Lua Bindings (Shift-Left)

**Reviewer:** security-auditor
**Date:** 2026-03-06
**Document reviewed:** design-note-entity-from-lua.md
**Review type:** Shift-left design review — gates implementation start

---

## Verdict: PASS WITH CONDITIONS

**Implementation may begin: YES WITH CONDITIONS**

The design is sound. The architect has done careful work — all major threat vectors are identified in the document's own SEC-E checklist and given correct mitigations. Implementation may begin, subject to two HIGH conditions and two MEDIUM conditions being met before merge. No BLOCK issues were found.

---

## Conditions for Implementation

### HIGH — Hard requirements before merge

**H-1: Entity ID upper bound check must be applied consistently across ALL new bindings**

The existing `getTransform` and `setTransform` bindings in `script_engine.cpp` only check `rawId < 0`, not the upper bound `rawId > UINT32_MAX - 1`. This is a pre-existing gap in the existing bindings (out of scope for this review), but the new bindings must not copy that pattern. Every new binding that receives an entity ID (`destroyEntity`, `addTransform`, `addPreviousTransform`, `addSprite`) must apply the full two-sided check specified in SEC-E-1:

```
rawId < 0 || rawId > static_cast<lua_Integer>(UINT32_MAX - 1)  →  reject
```

`NULL_ENTITY` is defined as `~EntityId{0}` which equals `UINT32_MAX`. The `UINT32_MAX - 1` upper bound correctly excludes it. If the implementation copies from the existing `getTransform` pattern rather than the design note's pseudocode, this guard will be silently missing. The test suite must cover: passing `UINT32_MAX` (should be rejected), passing `-1` (should be rejected).

**H-2: `World::addComponent` uses `emplace` which is undefined behaviour on EnTT if the component already exists — the overwrite path must use `emplace_or_replace` or explicit remove**

`World::addComponent` is implemented as `m_registry.emplace<T>(...)` (confirmed in `engine/core/ecs.h`, line 89). EnTT's `emplace` asserts in debug mode and is undefined behaviour in release mode if called on an entity that already owns the component type.

The design (Section 6, SEC-E-11) correctly identifies this. The implementation must choose one of two safe approaches for all three bindings (`addTransform`, `addSprite`, `addPreviousTransform`):

- Call `world->removeComponent<T>(id)` before `world->addComponent<T>(id, ...)` when `world->hasComponent<T>(id)` is true, OR
- Use `world->registry().emplace_or_replace<T>(entity, ...)` directly via the raw registry escape hatch.

Either approach is acceptable. The `emplace_or_replace` path is atomic and avoids a redundant existence check. The `remove`+`add` path is more explicit and easier to read. The implementation must not call `addComponent` unconditionally without the guard.

---

### MEDIUM — Required before merge

**M-1: The `addSprite` texture handle validation has an off-by-one error in the design pseudocode**

Section 3 validation pseudocode uses `rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(UINT32_MAX)`. This is correct — it rejects zero (the null sentinel) and rejects any value above the `u32` range. However Section 8's `addSprite` sketch uses the same bounds. Confirm the implementation uses strict greater-than for zero rejection (`rawHandle <= 0`, not `rawHandle < 0`), as zero is the invalid handle sentinel defined in `rhi_types.h` (`TextureHandle { u32 id = 0; }`). Passing `0` as the handle must return `false`, not produce a `TextureHandle{0}` in the Sprite component. The `rhi::isValid()` defence-in-depth check immediately after the range check covers this, but the range check should also explicitly reject zero.

**M-2: Document the dangling texture handle risk in the `.context.md` "What not to do" section — not just as an inline comment**

The design explicitly accepts the risk that `addSprite` cannot verify the texture handle refers to a currently live GPU texture. This is a reasonable tradeoff. The condition is that the documented mitigation ("destroy texture handles only after removing all Sprite components that reference them") must appear in the scripting subsystem's `.context.md` "What not to do" section with a concrete bad example and explanation, before `game-dev-tester` writes usage examples. An inline C++ comment in the binding is not sufficient — game developers writing Lua will never read `script_engine.cpp`.

---

## Advisory Findings (LOW — do not gate implementation)

**L-1: Existing `getTransform` and `setTransform` bindings lack the upper-bound entity ID check**

The existing bindings check `rawId < 0` but do not check `rawId > UINT32_MAX - 1`. On a 64-bit LuaJIT platform, a script can compute an integer above `UINT32_MAX` and pass it through without rejection. After the `static_cast<EntityId>(rawId)` truncation, the resulting value would be some valid or invalid `u32`, and `isValid()` would catch most cases. The risk is low because `isValid()` is the final gate. This is a pre-existing gap and is noted here for the tracker rather than as a condition on this design.

**L-2: The entity creation loop / memory exhaustion finding (SEC-E-12) is confirmed safe**

EnTT's `registry.create()` returns `entt::null` when the entity limit is reached (this is defined EnTT behaviour, not implementation-specific). `World::createEntity()` casts that result to `EntityId`, which produces `NULL_ENTITY` (`UINT32_MAX`). The design's `createEntity` binding checks for `NULL_ENTITY` and returns `nil`. No UB, no crash, no undefined state. The 1,000,000-instruction budget does not stop a loop of `ffe.createEntity()` calls because each call is a C binding invocation (budget counts Lua bytecode instructions, not C calls). The real-world risk is bounded by system RAM, not entity count limits. This is correctly accepted in SEC-E-12. No additional binding-layer enforcement is needed.

**L-3: Colour component clamping is correctly safe at the render layer regardless of binding choice**

The open design question in Section 8 (clamp vs reject for `r, g, b, a`) has no security implication either way. `render_system.cpp` already applies `glm::clamp(sprite.color.r, 0.0f, 1.0f)` before multiplying by 255 and packing to `u8` (line 64–67). Out-of-range colour values stored in a Sprite component cannot produce integer overflow in sort key computation or colour packing. The architect's recommendation (clamp with warning) is sound; this is purely a usability decision.

**L-4: The `Sprite.layer` field is `i16`, but `makeSortKey` takes `u8` — no overflow possible**

`render_system.cpp` applies `glm::clamp(static_cast<i32>(sprite.layer), 0, 15)` before casting to `u8` (line 52). An out-of-range `i16` layer value stored in the Sprite component is clamped at render time before the sort key computation. The design's SEC-E-7 requirement to clamp or range-check `layer` at the Lua binding is still correct (a narrowing cast of an out-of-range `lua_Integer` to `i16` is UB in C++), but the render-layer clamping means the only actual risk is the narrowing cast in the binding, not overflow in sort key computation. SEC-E-7 must still be implemented; this finding only clarifies that the blast radius of a missed check is limited to C++ UB in the binding, not to incorrect GPU sorting.

**L-5: Thread safety — no finding**

LEGACY tier is single-threaded. The scripting engine, ECS world, and render system all run on the main thread. The design's assessment is correct. No threading issues in the proposed bindings.

**L-6: `addPreviousTransform` with no Transform — safe, not a crash vector**

If `addPreviousTransform` is called on an entity with no Transform, the design adds a zero-initialised `PreviousTransform`. `PreviousTransform` is a plain struct with value-initialised members (confirmed in `render_system.h` — all fields have default values). This cannot produce a null-dereference or read of uninitialised memory. The resulting entity will render at position (0,0) with scale (1,1) for one frame before the interpolation corrects itself. The warning log is the correct mitigation.

---

## Full Findings Reference

| ID  | Severity | Summary |
|-----|----------|---------|
| H-1 | HIGH | New bindings must apply full two-sided entity ID range check (`< 0` and `> UINT32_MAX-1`); must not copy the existing bindings' weaker pattern |
| H-2 | HIGH | `World::addComponent` via `emplace` is UB if component exists; overwrite path must use `removeComponent`+`addComponent` or `emplace_or_replace` — required by SEC-E-11 |
| M-1 | MEDIUM | Confirm texture handle validation rejects `rawHandle <= 0` (not `< 0`) to match `rhi::TextureHandle{id=0}` null sentinel |
| M-2 | MEDIUM | Dangling texture handle risk must be documented in `.context.md` "What not to do" section before `game-dev-tester` writes examples |
| L-1 | LOW | Pre-existing gap: existing `getTransform`/`setTransform` lack upper-bound entity ID check; tracked for future fix |
| L-2 | LOW | SEC-E-12 confirmed safe: EnTT returns `entt::null` on exhaustion, instruction budget does not cover C bindings, accepted risk |
| L-3 | LOW | Colour clamping (clamp vs reject) has no security implication; render layer already clamps before packing |
| L-4 | LOW | `Sprite.layer` integer narrowing is the risk, not sort key overflow; render layer clamps `layer` to `[0,15]` before `makeSortKey` |
| L-5 | LOW | Thread safety: no finding — LEGACY is single-threaded, all bindings main-thread only |
| L-6 | LOW | `addPreviousTransform` with no Transform: default-initialised struct, no uninitialised read, no crash |

---

## Implementation Checklist for engine-dev

These are the actionable items from this review. Items marked (design) are already specified correctly and are listed here only to confirm they must appear in the implementation.

- [ ] H-1: Apply `rawId < 0 || rawId > static_cast<lua_Integer>(UINT32_MAX - 1)` in all four new bindings
- [ ] H-2: Use `removeComponent`+`addComponent` or `registry().emplace_or_replace<T>()` for the overwrite path in `addTransform`, `addSprite`, and `addPreviousTransform`
- [ ] M-1: Confirm texture handle zero-rejection uses `rawHandle <= 0`, not `rawHandle < 0`
- [ ] (design) SEC-E-2: `isValid()` called before any component access in all bindings
- [ ] (design) SEC-E-3: World pointer null check before any dereference
- [ ] (design) SEC-E-5: `std::isfinite()` applied to all float arguments in `addTransform` and `addSprite`
- [ ] (design) SEC-E-6: Width and height must be `> 0` in addition to finite
- [ ] (design) SEC-E-7: Layer `lua_Integer` clamped or range-checked before narrowing cast to `i16`
- [ ] (design) SEC-E-9: `isValid()` called in `destroyEntity` before `World::destroyEntity()`
- [ ] M-2: api-designer adds dangling texture handle warning to `.context.md` before `game-dev-tester` session
