#pragma once

// animation_system.h — Skeletal animation update system for FFE.
//
// animationUpdateSystem3D() runs per tick (before render). It iterates entities
// with AnimationState + Skeleton + Mesh components, samples keyframes at the
// current time, computes bone matrices via parent-child hierarchy walk, and
// stores the final transforms (inverseBindMatrix * worldPose) into the
// Skeleton::boneMatrices[] array ready for GPU upload.
//
// This system is separate from the 2D sprite animationUpdateSystem in
// render_system.h. Both can coexist — they query different component sets.
//
// Performance: O(bones * log(keyframes)) per animated entity per tick.
// No heap allocations. Fixed-size arrays throughout.
//
// Tier support: LEGACY (all computation is CPU-side).

#include "core/types.h"
#include "core/ecs.h"

namespace ffe::renderer {

// Priority for the 3D animation update system.
// Runs after 2D sprite animation (50) and before gameplay systems (>= 100).
inline constexpr i32 ANIMATION_3D_UPDATE_PRIORITY = 52;

// Update all skeletal animations for entities with AnimationState + Skeleton + Mesh.
// dt: fixed timestep delta (seconds).
// Called from Application::tick() via the system list.
void animationUpdateSystem3D(World& world, float dt);

} // namespace ffe::renderer
