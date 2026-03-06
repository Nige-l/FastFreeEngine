# Design Note: Sprite Animation & Atlas Support

**Status:** Proposed
**Author:** architect
**Tier support:** ALL (RETRO, LEGACY, STANDARD, MODERN)

---

## Problem

Currently every sprite references a full texture. Animated sprites require switching between multiple textures per frame, which is wasteful. A spritesheet/atlas approach lets us store all animation frames in a single texture and select frames via UV coordinates — zero texture switches, zero extra GPU uploads.

---

## 1. Sprite Atlas — Grid-Based

For MVP we use **grid-based atlases**: all frames are the same size, arranged in rows and columns within a single PNG. No offline packing tool is needed. The developer loads a spritesheet and specifies frame dimensions; the engine computes UV regions at runtime.

This is sufficient for character animations, tilesets, and VFX sheets. Arbitrary-packed atlases (texture packing with per-frame rects) can be added later as a non-breaking extension.

**Atlas metadata is not a separate asset.** The frame count and column count live on the `SpriteAnimation` component. The texture is just a normal texture loaded via `ffe.loadTexture()`.

---

## 2. Data Model

A new ECS component, separate from `Sprite`:

```cpp
struct SpriteAnimation {
    u16 frameCount;      // total frames in the atlas
    u16 columns;         // grid columns in the spritesheet
    u16 currentFrame;    // current frame index (0-based)
    u16 _pad;            // explicit padding for alignment
    f32 frameTime;       // seconds per frame
    f32 elapsed;         // time accumulator
    bool looping;        // true = loop, false = play once and stop
    bool playing;        // true = animation is advancing
};
// Size: 20 bytes. POD. No heap. No pointers.
```

**Why a separate component, not fields on `Sprite`?**

- Not every sprite is animated. Static sprites (backgrounds, UI, tiles) should not pay for animation fields.
- Separation lets the animation system query only `(Sprite, SpriteAnimation)` pairs — better iteration density.
- Follows ECS composition: add animation by attaching a component, remove it by detaching.

The existing `Sprite` component already carries `uvMin`/`uvMax`. The animation system writes into those fields each frame. No changes to `Sprite`, `SpriteInstance`, `SpriteBatch`, or the shader.

---

## 3. UV Computation

Given a frame index, the atlas grid dimensions, and the texture dimensions:

```
col = currentFrame % columns
row = currentFrame / columns

frameW = textureWidth / columns
frameH = textureHeight / (frameCount / columns + (frameCount % columns ? 1 : 0))

uvMin.x = col * frameW / textureWidth    // simplifies to: col / columns
uvMin.y = row * frameH / textureHeight
uvMax.x = (col + 1) * frameW / textureWidth
uvMax.y = (row + 1) * frameH / textureHeight
```

In practice, since frames are uniform, this reduces to division by `columns` and `rows` — no need to know pixel dimensions. The system computes `rows = ceil(frameCount / columns)` once when the component is added, then each frame is just two integer divides and four float multiplies.

**Half-texel inset:** To avoid bleeding from adjacent frames due to bilinear filtering, inset UVs by 0.5 / textureWidth (and height). This matters for `LINEAR` filter mode; `NEAREST` does not bleed. The system applies the inset automatically when the texture filter is `LINEAR`.

---

## 4. System Integration

New ECS system: `animationUpdateSystem`

- **Priority:** 50 (after `copyTransformSystem` at 5, before gameplay systems at 100+). Animation state is visual — it should update before render prepare but does not need to run before physics or game logic.
- **Query:** all entities with both `Sprite` and `SpriteAnimation`.
- **Per entity:**
  1. Skip if `!playing`.
  2. `elapsed += dt`.
  3. While `elapsed >= frameTime`: advance `currentFrame`, subtract `frameTime`.
  4. If `currentFrame >= frameCount`: if `looping`, wrap to 0; else clamp to `frameCount - 1` and set `playing = false`.
  5. Compute `uvMin`/`uvMax` from `currentFrame` and write into the entity's `Sprite` component.

**No heap allocation. No virtual calls. Pure arithmetic on POD data.**

---

## 5. Lua API

```lua
-- Attach animation to an entity that already has a Sprite component
ffe.addSpriteAnimation(entityId, frameCount, columns, frameTime, looping)

-- Playback control
ffe.playAnimation(entityId)
ffe.stopAnimation(entityId)

-- Manual frame control (also stops auto-advance)
ffe.setAnimationFrame(entityId, frameIndex)

-- Query
ffe.getAnimationFrame(entityId)  -- returns current frame index
ffe.isAnimationPlaying(entityId) -- returns bool
```

`addSpriteAnimation` validates: `frameCount > 0`, `columns > 0`, `columns <= frameCount`, `frameTime > 0`. Returns false and logs on invalid input. `setAnimationFrame` clamps to `[0, frameCount - 1]`.

---

## 6. Performance Notes

| Concern | Assessment |
|---|---|
| Component size | 20 bytes POD — fits in cache line with Sprite |
| Per-frame cost | 2 integer ops + 4 float muls per animated entity |
| Texture switches | Zero — all frames in one atlas texture |
| Heap allocation | None |
| Hot-path virtuals | None |
| Batching impact | None — animated sprites batch identically to static sprites (same texture handle) |

For 1000 animated entities this is roughly 20 microseconds of work — well within budget on LEGACY hardware.

---

## 7. What This Does NOT Cover

- **Arbitrary atlas packing** (non-uniform frame sizes, JSON rect descriptors). Future extension; grid-based is the MVP.
- **Multi-animation clips on one sheet** (e.g., walk = frames 0-7, idle = frames 8-11). Can be layered on top by adding `firstFrame`/`lastFrame` range fields later. The current design plays the full sheet.
- **Animation blending or state machines.** Out of scope for the atlas design note; belongs in a dedicated animation-system ADR if needed.
- **Atlas generation tooling.** Developers bring their own spritesheets. Packing tools are an editor feature, not an engine runtime feature.

---

## 8. Implementation Sequence

1. Add `SpriteAnimation` component to `engine/renderer/render_system.h`.
2. Implement `animationUpdateSystem` in `engine/renderer/render_system.cpp`.
3. Register the system in `Application` at priority 50.
4. Add Lua bindings in `engine/scripting/`.
5. `test-engineer` writes unit tests for UV computation and frame advancement.
6. `api-designer` updates renderer `.context.md`.
7. `game-dev-tester` builds an animated-sprite example.
