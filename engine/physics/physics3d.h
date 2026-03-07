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

} // namespace ffe::physics
