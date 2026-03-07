#pragma once

// ---------------------------------------------------------------------------
// physics3d_system.h — ECS integration for 3D physics.
//
// RigidBody3D component: attaches a Jolt body to an ECS entity.
// physics3dSyncSystem: syncs Jolt body transforms -> ECS Transform3D each frame.
// ---------------------------------------------------------------------------

#include "core/types.h"
#include "physics/physics3d.h"

namespace ffe {

// Forward declaration
class World;

// ---------------------------------------------------------------------------
// RigidBody3D — ECS component linking an entity to a Jolt physics body.
// Entities with Transform3D + RigidBody3D have their position/rotation
// driven by the physics simulation each frame.
// ---------------------------------------------------------------------------
struct RigidBody3D {
    physics::BodyHandle3D handle;
    bool initialized = false;
};

// ---------------------------------------------------------------------------
// physics3dSyncSystem — reads position/rotation from Jolt for each entity
// with Transform3D + RigidBody3D and writes it into Transform3D.
//
// Priority 60: after animation systems (50, 52), before gameplay (>= 100).
// ---------------------------------------------------------------------------
void physics3dSyncSystem(World& world, float dt);

inline constexpr i32 PHYSICS_3D_SYNC_PRIORITY = 60;

} // namespace ffe
