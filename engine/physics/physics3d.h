#pragma once

// ---------------------------------------------------------------------------
// physics3d.h — 3D rigid body physics public API (Jolt Physics backend).
//
// Foundation layer: body creation/destruction, simulation stepping, position/
// rotation/velocity queries, force/impulse application.
//
// Tier support: LEGACY+ (CPU-only, single-thread job system).
// No per-frame heap allocations. Pre-allocated temp allocator (10 MB).
// ---------------------------------------------------------------------------

#include "core/types.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ffe::physics {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct Physics3DConfig {
    glm::vec3 gravity = {0.0f, -9.81f, 0.0f};
    u32 maxBodies            = 1024;
    u32 maxBodyPairs         = 4096;
    u32 maxContactConstraints = 2048;
};

// ---------------------------------------------------------------------------
// Body handle — opaque wrapper around Jolt BodyID.
// Sentinel value 0xFFFFFFFF means "no body / invalid".
// ---------------------------------------------------------------------------
struct BodyHandle3D {
    u32 id = 0xFFFFFFFF;
};

inline bool isValid(const BodyHandle3D h) { return h.id != 0xFFFFFFFF; }

// ---------------------------------------------------------------------------
// Motion type
// ---------------------------------------------------------------------------
enum class MotionType3D : u8 {
    STATIC,
    KINEMATIC,
    DYNAMIC
};

// ---------------------------------------------------------------------------
// Shape type
// ---------------------------------------------------------------------------
enum class ShapeType3D : u8 {
    BOX,
    SPHERE,
    CAPSULE
};

// ---------------------------------------------------------------------------
// Body definition — everything needed to create a rigid body.
// ---------------------------------------------------------------------------
struct BodyDef3D {
    ShapeType3D  shapeType  = ShapeType3D::BOX;
    MotionType3D motionType = MotionType3D::DYNAMIC;

    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f}; // BOX half-extents
    f32       radius      = 0.5f;                 // SPHERE and CAPSULE
    f32       height      = 1.0f;                 // CAPSULE half-height of cylinder part

    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f}; // w,x,y,z — identity

    f32 mass        = 1.0f;  // ignored for STATIC
    f32 friction    = 0.5f;
    f32 restitution = 0.3f;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool initPhysics3D(const Physics3DConfig& config = {});
void shutdownPhysics3D();
bool isPhysics3DInitialized();

// ---------------------------------------------------------------------------
// Simulation step
// ---------------------------------------------------------------------------
void stepPhysics3D(float dt);

// ---------------------------------------------------------------------------
// Body management
// ---------------------------------------------------------------------------
BodyHandle3D createBody(const BodyDef3D& def);
void         destroyBody(BodyHandle3D handle);

// ---------------------------------------------------------------------------
// Position / rotation
// ---------------------------------------------------------------------------
void      setBodyPosition(BodyHandle3D handle, const glm::vec3& pos);
glm::vec3 getBodyPosition(BodyHandle3D handle);
void      setBodyRotation(BodyHandle3D handle, const glm::quat& rot);
glm::quat getBodyRotation(BodyHandle3D handle);

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------
void      setLinearVelocity(BodyHandle3D handle, const glm::vec3& vel);
glm::vec3 getLinearVelocity(BodyHandle3D handle);

// ---------------------------------------------------------------------------
// Forces / impulses (applied at center of mass)
// ---------------------------------------------------------------------------
void applyForce(BodyHandle3D handle, const glm::vec3& force);
void applyImpulse(BodyHandle3D handle, const glm::vec3& impulse);

// ---------------------------------------------------------------------------
// Gravity
// ---------------------------------------------------------------------------
void      setGravity(const glm::vec3& g);
glm::vec3 getGravity();

// ---------------------------------------------------------------------------
// Collision Event System
// ---------------------------------------------------------------------------
enum class CollisionEventType : u8 { ENTER, STAY, EXIT };

// Collision event — one per contact pair per frame.
// Fixed-size, POD, no heap allocation.
struct CollisionEvent3D {
    u32 entityA = 0xFFFFFFFF;  // EnTT entity ID (raw value), 0xFFFFFFFF = unmapped
    u32 entityB = 0xFFFFFFFF;
    glm::vec3 contactPoint  = {0.0f, 0.0f, 0.0f};
    glm::vec3 contactNormal = {0.0f, 0.0f, 0.0f};
    CollisionEventType type = CollisionEventType::ENTER;
};

// Max collision events buffered per frame.
inline constexpr u32 MAX_COLLISION_EVENTS = 256;

// Max bodies tracked for BodyID -> entity mapping.
inline constexpr u32 MAX_BODIES = 1024;

// Get the collision events from the last physics step.
// Returns a pointer to the internal buffer and the count.
const CollisionEvent3D* getCollisionEvents3D(u32& outCount);

// Clear collision events (called at start of each physics step).
void clearCollisionEvents3D();

// ---------------------------------------------------------------------------
// Body management (entity-aware overload)
// ---------------------------------------------------------------------------
BodyHandle3D createBody(const BodyDef3D& def, u32 entityId);

// Look up the entity ID associated with a body handle.
// Returns 0xFFFFFFFF if the handle is invalid or has no entity mapping.
u32 getBodyEntityId(BodyHandle3D handle);

// ---------------------------------------------------------------------------
// Raycasting
// ---------------------------------------------------------------------------

// Ray hit result — POD, no heap allocation.
struct RayHit3D {
    u32 entityId = 0xFFFFFFFF;  // entity that was hit
    glm::vec3 hitPoint  = {0.0f, 0.0f, 0.0f};
    glm::vec3 hitNormal = {0.0f, 0.0f, 0.0f};
    f32 distance = 0.0f;
    bool valid = false;
};

inline constexpr u32 MAX_RAY_HITS = 32;

// Cast ray, return first hit.
RayHit3D castRay(const glm::vec3& origin, const glm::vec3& direction, f32 maxDistance);

// Cast ray, return all hits (up to maxHits, capped at MAX_RAY_HITS).
// Results written to outHits, sorted by distance (nearest first).
// Returns number of hits written.
u32 castRayAll(const glm::vec3& origin, const glm::vec3& direction, f32 maxDistance,
               RayHit3D* outHits, u32 maxHits = MAX_RAY_HITS);

// ---------------------------------------------------------------------------
// Collision Callback Dispatch
// ---------------------------------------------------------------------------

// Function pointer callback type — no std::function (hot-path safe).
// userData is forwarded from setCollisionCallback3D.
using CollisionCallback3D = void(*)(const CollisionEvent3D& event, void* userData);

// Register a global collision callback. Only one callback at a time.
// Pass nullptr to remove.
void setCollisionCallback3D(CollisionCallback3D callback, void* userData = nullptr);

// Remove the registered collision callback (equivalent to setCollisionCallback3D(nullptr)).
void removeCollisionCallback3D();

// Dispatch all buffered collision events through the registered callback.
// Called once per frame after stepPhysics3D(). Safe to call if no callback is set.
void dispatchCollisionEvents3D();

} // namespace ffe::physics
