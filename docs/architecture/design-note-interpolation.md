# Design Note: Render Interpolation for Sprite Jitter

**Status:** Proposed
**Author:** architect
**Date:** 2026-03-06
**Related:** ADR-001 (core skeleton), ADR-002 (renderer RHI)

---

## 1. Root Cause

`Application::run()` computes an interpolation alpha immediately before calling `render()`:

```cpp
const float alpha = accumulator / fixedDt; // [0, 1)
render(alpha);
```

`alpha` represents how far through the current fixed timestep the render call is occurring. A value of 0.0 means the render happens exactly on a tick boundary; a value of 0.99 means the render happens just before the next tick.

Inside `Application::render()`, the first statement is:

```cpp
(void)alpha; // Interpolation not yet implemented
```

Alpha is immediately discarded. `renderPrepareSystem` runs during the fixed tick — not during `render()` — and copies `Transform.position` directly into `DrawCommand.posX / posY` at their post-tick values. There is no history of where entities were on the previous tick.

The result: every entity snaps to its post-tick position regardless of when within the timestep the render occurs. At 60Hz fixed tick with a 60Hz display this is often invisible, but at any display rate that does not perfectly align with the tick rate (144Hz monitors, frame rate spikes, vsync jitter) sprites visibly snap between integer-aligned tick positions rather than moving continuously. This is the classic fixed-timestep jitter problem.

---

## 2. Option A: PreviousTransform Component

Add a `PreviousTransform` component to the ECS that shadows `Transform`. A dedicated system copies `Transform` into `PreviousTransform` at the start of each tick, before gameplay systems update `Transform`. `renderPrepareSystem` receives `alpha` and lerps between `PreviousTransform` and `Transform` when writing `DrawCommand` positions.

**Pros:**
- Clean ECS data model — previous-state is explicit and queryable by any system, not hidden in the renderer.
- Works with any future backend (Vulkan, 3D renderer) without renderer-side changes.
- Lerp is trivially correct: `prev + (curr - prev) * alpha`.
- No change to `DrawCommand` layout or the sort key packing.
- Entities that do not move (static scenery, UI) can skip the `PreviousTransform` component entirely using EnTT's sparse storage — no unnecessary iteration.

**Cons:**
- Every moving entity requires a second `Transform`-sized component (36 bytes: `vec3 position + vec3 scale + f32 rotation`). For a scene with 1000 moving entities that is ~36 KB of additional component storage — negligible on LEGACY tier (1 GB VRAM, hundreds of MB of system RAM).
- A `CopyTransformSystem` must run at the correct priority (before all gameplay systems) every tick. If a developer inserts a system at priority 0 that modifies `Transform` before `CopyTransformSystem` runs, the snapshot will be wrong. Priority discipline is required.

---

## 3. Option B: Store Previous Positions in DrawCommand

Pass both current and previous positions inside `DrawCommand`. `renderPrepareSystem` still runs during the tick but populates both fields. `Application::render()` passes `alpha` into the sprite batch loop, which lerps on submission.

**Pros:**
- Interpolation is entirely renderer-contained — no ECS change, no new component type.
- No per-tick copy system required.

**Cons:**
- `DrawCommand` currently occupies 40 bytes and sits comfortably within one cache line. Adding `prevPosX` and `prevPosY` (8 bytes) pushes it to 48 bytes, still within one cache line. However, adding previous scale and rotation (another 12 bytes) crosses into two cache lines, degrading the cache efficiency of `sortRenderQueue` which iterates the entire command array. The design would need to interpolate position only and accept that scale/rotation interpolation is skipped, which produces incorrect results for rotating or scaling entities.
- `renderPrepareSystem` runs during the tick, not during `render()`. It has no access to `alpha` — `alpha` is computed after the tick loop completes. This means `renderPrepareSystem` would need to store both current and previous positions at tick time but cannot perform the lerp itself. The lerp must happen in `Application::render()`, spreading interpolation logic across two files and two call sites.
- The previous-position concept leaks into the renderer layer, which has no business owning game-state history.

---

## 4. Option C: Sub-Step Rendering (Increase Tick Rate)

Do not interpolate. Instead, raise the fixed tick rate high enough that the per-tick position delta is sub-pixel. At 240Hz and a sprite moving at 500 px/s, the per-tick delta is 500/240 ≈ 2.08 px — still perceptible. At 960Hz the delta is 0.52 px — sub-pixel for a 1080p display but only marginally so.

**Pros:**
- No new code required — just change `ApplicationConfig::tickRate`.
- Eliminates the need to track previous state entirely.

**Cons:**
- On LEGACY tier hardware (circa 2012 CPU, likely 4 cores at 2–3 GHz), running gameplay systems at 240Hz or higher will saturate CPU time that should be available for rendering and audio. A tick rate of 240Hz means 4x the current system update cost, which may push per-frame CPU budget past 16.67ms on slower machines.
- Does not actually fix the root cause. Alpha still varies between 0 and 1 within every tick. At 60Hz display with a 240Hz tick the improvement is real but the jitter is still present; it is merely smaller. A 144Hz monitor with a 240Hz tick still sees visible snapping at certain beat frequencies.
- Increases simulation determinism burden — physics and gameplay systems that assume ~16ms timesteps may need retuning.
- This option is categorically the wrong answer for a fix. The correct fix is interpolation.

---

## 5. Recommendation: Option A

Option A is recommended. The `PreviousTransform` memory cost is trivial (36 bytes per moving entity, hundreds of KB in the worst case on LEGACY tier), the ECS model is clean, and interpolation logic is centralized in one place. Option B distributes the fix across the wrong abstraction boundaries and threatens `DrawCommand` cache efficiency. Option C does not fix the problem.

---

## 6. Implementation Sketch for Option A

### Files that change

| File | Change |
|---|---|
| `engine/renderer/render_system.h` | Add `PreviousTransform` struct (mirrors `Transform`). Add `COPY_TRANSFORM_PRIORITY` constant. Declare `copyTransformSystem`. Update `renderPrepareSystem` signature to accept `alpha`. |
| `engine/renderer/render_system.cpp` | Implement `copyTransformSystem`. Update `renderPrepareSystem` to lerp using `alpha`. |
| `engine/core/application.h` | Pass `alpha` through to `renderPrepareSystem` via ECS context (see below). |
| `engine/core/application.cpp` | Store `alpha` in ECS context before calling `render()`. Register `copyTransformSystem` at correct priority. Remove `(void)alpha`. |
| `tests/renderer/` | Add tests for lerp correctness at alpha=0, alpha=0.5, alpha=1. |
| `engine/renderer/.context.md` | Document `PreviousTransform`, update `renderPrepareSystem` description, note the copy-before-update ordering requirement. |

### PreviousTransform struct

`PreviousTransform` is identical in layout to `Transform`. It lives in `render_system.h` alongside `Transform`:

```cpp
struct PreviousTransform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
    f32 rotation       = 0.0f;
};
```

### System update sequence (critical ordering)

The ordering within a single tick is:

```
Priority 0:  InputUpdateSystem       — reads hardware state
Priority 5:  CopyTransformSystem     — snapshot Transform -> PreviousTransform
Priority 10: (gameplay systems)      — modify Transform
Priority 100: RenderPrepareSystem    — reads both, writes DrawCommands
```

`CopyTransformSystem` **must** run before any gameplay system modifies `Transform`. If it runs after, the snapshot captures the post-update position, making `PreviousTransform` identical to `Transform` and the lerp a no-op. Priority 5 provides a clear gap for future systems.

### Passing alpha to renderPrepareSystem

`renderPrepareSystem` is registered as a `SystemFn` with signature `void(World&, float dt)`. The `float` parameter is `dt`, not `alpha`. Alpha is computed after the tick loop and is not available at tick time.

The clean solution: store `alpha` in the ECS registry context after the tick loop, before calling `render()`. `renderPrepareSystem` is moved out of the tick loop and called explicitly from `Application::render()` instead, receiving `alpha` directly. This also fixes the architectural mismatch where a render-preparation system currently runs inside the simulation tick.

```
Application::run() loop:
  while accumulator >= fixedDt:
    copyTransformSystem runs (priority 5, inside tick)
    gameplay systems run (inside tick)
    accumulator -= fixedDt

  alpha = accumulator / fixedDt
  renderPrepareSystem(world, alpha)   // called from render(), not tick()
  sortRenderQueue()
  drawFrame()
```

This requires removing `renderPrepareSystem` from the registered system list and calling it explicitly in `Application::render()`. The `RenderQueue` clear must remain at the top of `render()`, not at the top of the tick loop, since `renderPrepareSystem` now runs during render.

### Lerp in renderPrepareSystem

```cpp
// Entities with PreviousTransform get interpolated positions
const auto interpView = world.view<const PreviousTransform, const Transform, const Sprite>();
for (const auto entity : interpView) {
    const auto& prev = interpView.get<const PreviousTransform>(entity);
    const auto& curr = interpView.get<const Transform>(entity);
    // ...
    cmd.posX = prev.position.x + (curr.position.x - prev.position.x) * alpha;
    cmd.posY = prev.position.y + (curr.position.y - prev.position.y) * alpha;
    // rotation: use glm::mix or manual lerp; for short arcs lerp is sufficient
    // scale: lerp independently
}

// Entities without PreviousTransform (static) use current transform directly
const auto staticView = world.view<const Transform, const Sprite>(
    entt::exclude<PreviousTransform>);
for (const auto entity : staticView) {
    // copy transform.position directly — no lerp needed
}
```

EnTT supports `entt::exclude<T>` in views, so the two passes can be kept separate without a branch per entity in either loop.

### What CopyTransformSystem does

```cpp
void copyTransformSystem(World& world, float /*dt*/) {
    // For every entity that has both Transform and PreviousTransform,
    // snapshot current Transform into PreviousTransform.
    auto view = world.view<const Transform, PreviousTransform>();
    for (const auto entity : view) {
        const auto& curr = view.get<const Transform>(entity);
        auto& prev       = view.get<PreviousTransform>(entity);
        prev.position = curr.position;
        prev.scale    = curr.scale;
        prev.rotation = curr.rotation;
    }
}
```

This is a tight loop over contiguous component arrays — no allocation, no branching, cache-friendly. At 1000 entities it is approximately 36 KB of reads and 36 KB of writes per tick, well within budget on LEGACY tier.

### Adding PreviousTransform to an entity

The game developer opts in by adding the component alongside `Transform` and `Sprite`. Entities without `PreviousTransform` are treated as static and skipped by the interpolation view:

```cpp
world.addComponent<Transform>(entity);
world.addComponent<PreviousTransform>(entity);  // opt in to interpolation
world.addComponent<Sprite>(entity);
```

This keeps the opt-in explicit, avoids wasting memory on static entities (tilemaps, UI elements), and maps cleanly to the "pay for what you use" philosophy of the engine's data-oriented design.
