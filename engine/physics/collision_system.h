#pragma once

#include "core/types.h"
#include "core/system.h"

namespace ffe {

// Forward declaration
class World;

// ---------------------------------------------------------------------------
// collisionSystem — 2D collision detection ECS system.
//
// Runs at priority 200 (physics band, after gameplay systems at 100-199).
// Detects overlaps between entities with Transform + Collider2D components.
// Writes a CollisionEventList to the ECS registry context each frame.
//
// Internally uses a spatial hash grid (arena-allocated, rebuilt each frame)
// for broad phase, then narrow-phase overlap tests (AABB, Circle, AABB-Circle).
//
// Layer/mask filtering: (A.layer & B.mask) && (B.layer & A.mask).
//
// No heap allocation. No virtual calls. No std::function.
// All temporary storage comes from the per-frame ArenaAllocator.
// ---------------------------------------------------------------------------
void collisionSystem(World& world, float dt);

// Priority at which collisionSystem should be registered.
// Must run after gameplay/scripting systems (100-199).
inline constexpr i32 COLLISION_SYSTEM_PRIORITY = 200;

} // namespace ffe
