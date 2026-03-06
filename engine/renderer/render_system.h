#pragma once

#include "core/types.h"
#include "core/system.h"
#include "renderer/rhi_types.h"
#include "renderer/render_queue.h"
#include "renderer/sprite_batch.h"

#include <glm/glm.hpp>

namespace ffe {

// --- Transform component (needed by render system) ---
// Defined here because the render system needs it and it is not yet in core.
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
    f32 rotation       = 0.0f; // Radians, z-axis rotation for 2D
};

// --- PreviousTransform component — used for render interpolation ---
// Mirrors Transform exactly. Snapshotted by CopyTransformSystem at the
// START of each tick (before gameplay systems update Transform).
// renderPrepareSystem lerps between PreviousTransform and Transform using
// the render alpha to eliminate fixed-timestep jitter.
// Opt in by adding this component alongside Transform + Sprite.
// Static entities (tilemaps, UI) can omit it — they render without lerp.
struct PreviousTransform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
    f32 rotation       = 0.0f;
};

// --- Sprite component ---
struct Sprite {
    rhi::TextureHandle texture;
    glm::vec2 size;
    glm::vec2 uvMin = {0.0f, 0.0f};
    glm::vec2 uvMax = {1.0f, 1.0f};
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    i16 layer     = 0;
    i16 sortOrder = 0;
    bool flipX    = false;
    bool flipY    = false;
};

// --- SpriteAnimation component ---
// Drives atlas-based sprite animation. Entities with both Sprite and
// SpriteAnimation will have their uvMin/uvMax updated each frame by
// animationUpdateSystem based on the current frame index and grid layout.
// POD. No heap. No pointers.
struct SpriteAnimation {
    u16 frameCount   = 1;       // total frames in the atlas
    u16 columns      = 1;       // atlas grid columns
    u16 currentFrame = 0;       // current frame index (0-based)
    u16 _pad         = 0;       // explicit padding for alignment
    f32 frameTime    = 0.1f;    // seconds per frame
    f32 elapsed      = 0.0f;    // time accumulator
    bool looping     = true;
    bool playing     = false;
};

} // namespace ffe

namespace ffe::renderer {

// CopyTransformSystem — run at priority 5, before any gameplay system.
// Snapshots Transform into PreviousTransform for all entities that have both.
// Must run before gameplay systems modify Transform each tick.
void copyTransformSystem(World& world, float dt);

// Priority at which copyTransformSystem should be registered.
// Must be lower than any gameplay system priority (gameplay typically >= 100).
inline constexpr i32 COPY_TRANSFORM_PRIORITY = 5;

// AnimationUpdateSystem — run at priority 50, after CopyTransform (5) and
// before gameplay systems (>= 100). Advances SpriteAnimation timers and
// writes updated UV coordinates into the Sprite component.
void animationUpdateSystem(World& world, float dt);

// Priority at which animationUpdateSystem should be registered.
inline constexpr i32 ANIMATION_UPDATE_PRIORITY = 50;

// The render preparation system. Called explicitly from Application::render(),
// NOT registered in the system list. Accepts alpha (interpolation factor [0,1))
// and lerps between PreviousTransform and Transform when building DrawCommands.
void renderPrepareSystem(World& world, float alpha);

} // namespace ffe::renderer
