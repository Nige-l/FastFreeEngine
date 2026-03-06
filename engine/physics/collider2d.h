#pragma once

#include "core/types.h"

namespace ffe {

// ---------------------------------------------------------------------------
// ColliderShape — two shapes for MVP 2D collision detection.
// AABB: axis-aligned bounding box, parameterised by halfWidth / halfHeight.
// CIRCLE: radius stored in halfWidth; halfHeight is ignored.
// ---------------------------------------------------------------------------
enum class ColliderShape : u8 {
    AABB   = 0,
    CIRCLE = 1
};

// ---------------------------------------------------------------------------
// Collider2D — 16-byte POD component for 2D collision detection.
//
// Attached to entities alongside Transform. The collider is centered on the
// entity's Transform position (no local offset in MVP).
//
// Layer/mask filtering: two entities A and B collide only if
//   (A.layer & B.mask) != 0  &&  (B.layer & A.mask) != 0
// 16-bit masks give 16 collision layers.
// ---------------------------------------------------------------------------
struct Collider2D {
    ColliderShape shape = ColliderShape::AABB; // 1 byte
    bool isTrigger      = false;               // 1 byte — overlap event only, no push
    u16 layer           = 0xFFFF;              // which layer this entity belongs to
    u16 mask            = 0xFFFF;              // which layers this entity collides with
    // 2 bytes padding
    f32 halfWidth       = 0.0f;                // AABB half-extent X, or circle radius
    f32 halfHeight      = 0.0f;                // AABB half-extent Y (ignored for CIRCLE)
};
static_assert(sizeof(Collider2D) == 16, "Collider2D must be 16 bytes POD");

// ---------------------------------------------------------------------------
// CollisionEvent — generated per-frame for each overlapping pair.
// entityA < entityB (canonical ordering for deduplication).
// Arena-allocated, valid for the current frame only.
// ---------------------------------------------------------------------------
struct CollisionEvent {
    EntityId entityA;
    EntityId entityB;
};
static_assert(sizeof(CollisionEvent) == 8, "CollisionEvent must be 8 bytes");

// ---------------------------------------------------------------------------
// CollisionEventList — stored in ECS context by the collision system.
// Points to an arena-allocated array. Valid for the current frame only.
// Read by ScriptEngine to deliver events to the Lua callback.
// ---------------------------------------------------------------------------
struct CollisionEventList {
    CollisionEvent* events = nullptr;
    u32 count              = 0;
};

// ---------------------------------------------------------------------------
// CollisionCallbackRef — stored in ECS context.
// Holds a Lua registry reference to the collision callback function.
// LUA_NOREF (typically -2) means no callback is registered.
// The collision system does NOT call Lua directly — it writes events to
// CollisionEventList. Application or ScriptEngine reads the events and
// invokes the Lua callback after all systems have run.
// ---------------------------------------------------------------------------
struct CollisionCallbackRef {
    int luaRef = -2; // LUA_NOREF without including lua.h
};

} // namespace ffe
