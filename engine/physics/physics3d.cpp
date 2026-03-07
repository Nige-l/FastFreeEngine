// ---------------------------------------------------------------------------
// physics3d.cpp — 3D rigid body physics implementation (Jolt Physics backend).
//
// All Jolt global state is file-scoped. No per-frame heap allocations.
// TempAllocator is pre-allocated (10 MB). Job system uses 1 thread for LEGACY.
//
// NaN/Inf guards on all public float parameters.
// ---------------------------------------------------------------------------

#include "physics/physics3d.h"
#include "core/logging.h"

#include <cmath>

// Jolt headers — must be included after Jolt.h
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

// ---------------------------------------------------------------------------
// GLM <-> Jolt conversion helpers (file-scoped, no linkage)
// ---------------------------------------------------------------------------
static inline JPH::Vec3 toJolt(const glm::vec3& v) {
    return JPH::Vec3(v.x, v.y, v.z);
}

static inline glm::vec3 fromJolt(const JPH::Vec3& v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}

static inline JPH::RVec3 toJoltR(const glm::vec3& v) {
    return JPH::RVec3(
        static_cast<JPH::Real>(v.x),
        static_cast<JPH::Real>(v.y),
        static_cast<JPH::Real>(v.z));
}

static inline glm::vec3 fromJoltR(const JPH::RVec3& v) {
    return {
        static_cast<float>(v.GetX()),
        static_cast<float>(v.GetY()),
        static_cast<float>(v.GetZ())};
}

static inline JPH::Quat toJoltQuat(const glm::quat& q) {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

static inline glm::quat fromJoltQuat(const JPH::Quat& q) {
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

// ---------------------------------------------------------------------------
// Float sanitisation — NaN/Inf replaced with 0.0
// ---------------------------------------------------------------------------
static inline float sanitize(const float v) {
    return std::isfinite(v) ? v : 0.0f;
}

static inline glm::vec3 sanitizeVec3(const glm::vec3& v) {
    return {sanitize(v.x), sanitize(v.y), sanitize(v.z)};
}

static inline glm::quat sanitizeQuat(const glm::quat& q) {
    const float w = sanitize(q.w);
    const float x = sanitize(q.x);
    const float y = sanitize(q.y);
    const float z = sanitize(q.z);
    // If everything zeroed out, return identity.
    const float lenSq = w * w + x * x + y * y + z * z;
    if (lenSq < 1e-12f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return glm::quat(w, x, y, z);
}

// ---------------------------------------------------------------------------
// Jolt object-layer / broadphase-layer definitions.
// Two layers: NON_MOVING (0) for static bodies, MOVING (1) for dynamic/kinematic.
// Two broadphase layers that mirror the object layers.
// ---------------------------------------------------------------------------

namespace {

constexpr JPH::ObjectLayer OBJ_LAYER_NON_MOVING = 0;
constexpr JPH::ObjectLayer OBJ_LAYER_MOVING     = 1;
constexpr JPH::uint        NUM_OBJECT_LAYERS     = 2;

constexpr JPH::BroadPhaseLayer BP_LAYER_NON_MOVING(0);
constexpr JPH::BroadPhaseLayer BP_LAYER_MOVING(1);
constexpr JPH::uint            NUM_BP_LAYERS = 2;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Module-level state (file-scoped statics)
// ---------------------------------------------------------------------------
namespace {

struct Physics3DState {
    bool initialized = false;

    // Jolt layer interfaces (allocated once at init, freed at shutdown).
    // These use virtual functions but are cold-path only (init/body creation).
    JPH::BroadPhaseLayerInterfaceTable*       bpLayerInterface  = nullptr;
    JPH::ObjectVsBroadPhaseLayerFilterTable*   objVsBpFilter     = nullptr;
    JPH::ObjectLayerPairFilterTable*           objPairFilter     = nullptr;

    // Core Jolt objects
    JPH::PhysicsSystem*    physicsSystem = nullptr;
    JPH::TempAllocatorImpl* tempAllocator = nullptr;
    JPH::JobSystemThreadPool* jobSystem   = nullptr;
};

Physics3DState s_state;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

namespace ffe::physics {

bool initPhysics3D(const Physics3DConfig& config) {
    if (s_state.initialized) {
        FFE_LOG_WARN("Physics3D", "initPhysics3D called when already initialized — no-op");
        return true;
    }

    // Jolt global init (cold path — heap allocations are fine here)
    JPH::RegisterDefaultAllocator();

    // Factory is needed for RegisterTypes to register collision handlers.
    if (JPH::Factory::sInstance == nullptr) {
        JPH::Factory::sInstance = new JPH::Factory(); // cold path
    }

    JPH::RegisterTypes();

    // Pre-allocated temp allocator — 10 MB, no per-frame heap
    s_state.tempAllocator = new JPH::TempAllocatorImpl(10u * 1024u * 1024u); // cold path

    // Job system — 1 worker thread for LEGACY tier
    s_state.jobSystem = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1); // cold path

    // Layer interfaces
    s_state.objPairFilter = new JPH::ObjectLayerPairFilterTable(NUM_OBJECT_LAYERS); // cold path
    s_state.objPairFilter->EnableCollision(OBJ_LAYER_NON_MOVING, OBJ_LAYER_MOVING);
    s_state.objPairFilter->EnableCollision(OBJ_LAYER_MOVING, OBJ_LAYER_MOVING);
    // NON_MOVING vs NON_MOVING: disabled (static bodies never collide with each other)

    s_state.bpLayerInterface = new JPH::BroadPhaseLayerInterfaceTable(
        NUM_OBJECT_LAYERS, NUM_BP_LAYERS); // cold path
    s_state.bpLayerInterface->MapObjectToBroadPhaseLayer(OBJ_LAYER_NON_MOVING, BP_LAYER_NON_MOVING);
    s_state.bpLayerInterface->MapObjectToBroadPhaseLayer(OBJ_LAYER_MOVING, BP_LAYER_MOVING);

    s_state.objVsBpFilter = new JPH::ObjectVsBroadPhaseLayerFilterTable(
        *s_state.bpLayerInterface, NUM_BP_LAYERS,
        *s_state.objPairFilter, NUM_OBJECT_LAYERS); // cold path

    // Physics system
    s_state.physicsSystem = new JPH::PhysicsSystem(); // cold path
    s_state.physicsSystem->Init(
        config.maxBodies,
        0, // auto-detect mutex count
        config.maxBodyPairs,
        config.maxContactConstraints,
        *s_state.bpLayerInterface,
        *s_state.objVsBpFilter,
        *s_state.objPairFilter);

    // Set gravity
    const glm::vec3 g = sanitizeVec3(config.gravity);
    s_state.physicsSystem->SetGravity(toJolt(g));

    s_state.initialized = true;
    FFE_LOG_INFO("Physics3D", "Jolt Physics initialized (maxBodies=%u, gravity=%.2f,%.2f,%.2f)",
                 config.maxBodies, g.x, g.y, g.z);

    return true;
}

void shutdownPhysics3D() {
    if (!s_state.initialized) {
        return;
    }

    delete s_state.physicsSystem;    s_state.physicsSystem = nullptr;
    delete s_state.jobSystem;        s_state.jobSystem     = nullptr;
    delete s_state.tempAllocator;    s_state.tempAllocator = nullptr;
    delete s_state.objVsBpFilter;    s_state.objVsBpFilter = nullptr;
    delete s_state.bpLayerInterface; s_state.bpLayerInterface = nullptr;
    delete s_state.objPairFilter;    s_state.objPairFilter = nullptr;

    JPH::UnregisterTypes();

    // Destroy factory
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    s_state.initialized = false;
    FFE_LOG_INFO("Physics3D", "Jolt Physics shut down");
}

bool isPhysics3DInitialized() {
    return s_state.initialized;
}

// ---------------------------------------------------------------------------
// Simulation step
// ---------------------------------------------------------------------------

void stepPhysics3D(const float dt) {
    if (!s_state.initialized) { return; }

    const float safeDt = sanitize(dt);
    if (safeDt <= 0.0f) { return; }

    // 1 collision step per update (sufficient for fixed-rate 60 Hz)
    s_state.physicsSystem->Update(safeDt, 1,
                                  s_state.tempAllocator,
                                  s_state.jobSystem);
}

// ---------------------------------------------------------------------------
// Body management
// ---------------------------------------------------------------------------

BodyHandle3D createBody(const BodyDef3D& def) {
    if (!s_state.initialized) {
        FFE_LOG_ERROR("Physics3D", "createBody called before initPhysics3D");
        return BodyHandle3D{};
    }

    // Build shape (cold path — heap allocation inside Jolt is acceptable)
    JPH::RefConst<JPH::Shape> shape;
    switch (def.shapeType) {
        case ShapeType3D::BOX: {
            const glm::vec3 he = sanitizeVec3(def.halfExtents);
            const float hx = std::max(he.x, 0.001f);
            const float hy = std::max(he.y, 0.001f);
            const float hz = std::max(he.z, 0.001f);
            shape = new JPH::BoxShape(JPH::Vec3(hx, hy, hz));
            break;
        }
        case ShapeType3D::SPHERE: {
            const float r = std::max(sanitize(def.radius), 0.001f);
            shape = new JPH::SphereShape(r);
            break;
        }
        case ShapeType3D::CAPSULE: {
            const float r = std::max(sanitize(def.radius), 0.001f);
            const float h = std::max(sanitize(def.height), 0.001f);
            shape = new JPH::CapsuleShape(h, r);
            break;
        }
    }

    // Map motion type
    JPH::EMotionType joltMotion = JPH::EMotionType::Dynamic;
    JPH::ObjectLayer  objectLayer = OBJ_LAYER_MOVING;
    switch (def.motionType) {
        case MotionType3D::STATIC:
            joltMotion  = JPH::EMotionType::Static;
            objectLayer = OBJ_LAYER_NON_MOVING;
            break;
        case MotionType3D::KINEMATIC:
            joltMotion  = JPH::EMotionType::Kinematic;
            objectLayer = OBJ_LAYER_MOVING;
            break;
        case MotionType3D::DYNAMIC:
            joltMotion  = JPH::EMotionType::Dynamic;
            objectLayer = OBJ_LAYER_MOVING;
            break;
    }

    const glm::vec3 pos = sanitizeVec3(def.position);
    const glm::quat rot = sanitizeQuat(def.rotation);

    JPH::BodyCreationSettings settings(
        shape,
        toJoltR(pos),
        toJoltQuat(rot),
        joltMotion,
        objectLayer);

    settings.mFriction    = std::max(sanitize(def.friction), 0.0f);
    settings.mRestitution = std::clamp(sanitize(def.restitution), 0.0f, 1.0f);

    // Mass override for dynamic bodies
    if (def.motionType == MotionType3D::DYNAMIC) {
        const float mass = std::max(sanitize(def.mass), 0.001f);
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = mass;
    }

    JPH::BodyInterface& bodyInterface = s_state.physicsSystem->GetBodyInterface();
    const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
        settings,
        (joltMotion == JPH::EMotionType::Static)
            ? JPH::EActivation::DontActivate
            : JPH::EActivation::Activate);

    if (bodyId.IsInvalid()) {
        FFE_LOG_ERROR("Physics3D", "createBody: Jolt body creation failed (body limit reached?)");
        return BodyHandle3D{};
    }

    return BodyHandle3D{bodyId.GetIndexAndSequenceNumber()};
}

void destroyBody(const BodyHandle3D handle) {
    if (!s_state.initialized || !isValid(handle)) { return; }

    JPH::BodyInterface& bodyInterface = s_state.physicsSystem->GetBodyInterface();
    const JPH::BodyID bodyId(handle.id);

    // Check if the body still exists (safe against double-destroy)
    if (!bodyInterface.IsAdded(bodyId)) { return; }

    bodyInterface.RemoveBody(bodyId);
    bodyInterface.DestroyBody(bodyId);
}

// ---------------------------------------------------------------------------
// Position / rotation
// ---------------------------------------------------------------------------

void setBodyPosition(const BodyHandle3D handle, const glm::vec3& pos) {
    if (!s_state.initialized || !isValid(handle)) { return; }
    const glm::vec3 safePos = sanitizeVec3(pos);
    JPH::BodyInterface& bi = s_state.physicsSystem->GetBodyInterface();
    bi.SetPosition(JPH::BodyID(handle.id), toJoltR(safePos), JPH::EActivation::Activate);
}

glm::vec3 getBodyPosition(const BodyHandle3D handle) {
    if (!s_state.initialized || !isValid(handle)) { return {0.0f, 0.0f, 0.0f}; }
    JPH::BodyInterface& bi = s_state.physicsSystem->GetBodyInterface();
    return fromJoltR(bi.GetPosition(JPH::BodyID(handle.id)));
}

void setBodyRotation(const BodyHandle3D handle, const glm::quat& rot) {
    if (!s_state.initialized || !isValid(handle)) { return; }
    const glm::quat safeRot = sanitizeQuat(rot);
    JPH::BodyInterface& bi = s_state.physicsSystem->GetBodyInterface();
    bi.SetRotation(JPH::BodyID(handle.id), toJoltQuat(safeRot), JPH::EActivation::Activate);
}

glm::quat getBodyRotation(const BodyHandle3D handle) {
    if (!s_state.initialized || !isValid(handle)) { return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); }
    JPH::BodyInterface& bi = s_state.physicsSystem->GetBodyInterface();
    return fromJoltQuat(bi.GetRotation(JPH::BodyID(handle.id)));
}

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------

void setLinearVelocity(const BodyHandle3D handle, const glm::vec3& vel) {
    if (!s_state.initialized || !isValid(handle)) { return; }
    const glm::vec3 safeVel = sanitizeVec3(vel);
    JPH::BodyInterface& bi = s_state.physicsSystem->GetBodyInterface();
    bi.SetLinearVelocity(JPH::BodyID(handle.id), toJolt(safeVel));
}

glm::vec3 getLinearVelocity(const BodyHandle3D handle) {
    if (!s_state.initialized || !isValid(handle)) { return {0.0f, 0.0f, 0.0f}; }
    JPH::BodyInterface& bi = s_state.physicsSystem->GetBodyInterface();
    return fromJolt(bi.GetLinearVelocity(JPH::BodyID(handle.id)));
}

// ---------------------------------------------------------------------------
// Forces / impulses
// ---------------------------------------------------------------------------

void applyForce(const BodyHandle3D handle, const glm::vec3& force) {
    if (!s_state.initialized || !isValid(handle)) { return; }
    const glm::vec3 safeForce = sanitizeVec3(force);
    JPH::BodyInterface& bi = s_state.physicsSystem->GetBodyInterface();
    bi.AddForce(JPH::BodyID(handle.id), toJolt(safeForce));
}

void applyImpulse(const BodyHandle3D handle, const glm::vec3& impulse) {
    if (!s_state.initialized || !isValid(handle)) { return; }
    const glm::vec3 safeImpulse = sanitizeVec3(impulse);
    JPH::BodyInterface& bi = s_state.physicsSystem->GetBodyInterface();
    bi.AddImpulse(JPH::BodyID(handle.id), toJolt(safeImpulse));
}

// ---------------------------------------------------------------------------
// Gravity
// ---------------------------------------------------------------------------

void setGravity(const glm::vec3& g) {
    if (!s_state.initialized) { return; }
    const glm::vec3 safeG = sanitizeVec3(g);
    s_state.physicsSystem->SetGravity(toJolt(safeG));
}

glm::vec3 getGravity() {
    if (!s_state.initialized) { return {0.0f, -9.81f, 0.0f}; }
    return fromJolt(s_state.physicsSystem->GetGravity());
}

} // namespace ffe::physics
