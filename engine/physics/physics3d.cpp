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

#include <algorithm>
#include <cmath>
#include <cstring>

// Jolt headers — must be included after Jolt.h
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
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
// Entity-Body Mapping (fixed-size array, no heap)
// ---------------------------------------------------------------------------
namespace {

using ffe::u32;

// BodyID index -> entity ID mapping. 0xFFFFFFFF = unmapped.
// Jolt BodyID index is the lower 22 bits of GetIndexAndSequenceNumber().
// We use MAX_BODIES slots (matching Jolt's maxBodies config default of 1024).
constexpr u32 INVALID_ENTITY = 0xFFFFFFFF;

u32 s_bodyToEntity[ffe::physics::MAX_BODIES];
u32 s_bodyToEntityCount = 0; // for stats, not iteration

static void initBodyEntityMapping() {
    std::memset(s_bodyToEntity, 0xFF, sizeof(s_bodyToEntity));
    s_bodyToEntityCount = 0;
}

// Jolt BodyID stores index in lower bits. Extract it for our mapping array.
static inline u32 bodyIdToIndex(const JPH::BodyID& bodyId) {
    return bodyId.GetIndex();
}

static void mapBodyToEntity(const JPH::BodyID& bodyId, const u32 entityId) {
    const u32 idx = bodyIdToIndex(bodyId);
    if (idx < ffe::physics::MAX_BODIES) {
        s_bodyToEntity[idx] = entityId;
        ++s_bodyToEntityCount;
    }
}

static void unmapBody(const JPH::BodyID& bodyId) {
    const u32 idx = bodyIdToIndex(bodyId);
    if (idx < ffe::physics::MAX_BODIES) {
        if (s_bodyToEntity[idx] != INVALID_ENTITY) {
            s_bodyToEntity[idx] = INVALID_ENTITY;
            if (s_bodyToEntityCount > 0) { --s_bodyToEntityCount; }
        }
    }
}

static u32 lookupEntity(const JPH::BodyID& bodyId) {
    const u32 idx = bodyIdToIndex(bodyId);
    if (idx < ffe::physics::MAX_BODIES) {
        return s_bodyToEntity[idx];
    }
    return INVALID_ENTITY;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Collision Event Buffer (fixed-size, no heap)
// ---------------------------------------------------------------------------
namespace {

ffe::physics::CollisionEvent3D s_collisionEvents[ffe::physics::MAX_COLLISION_EVENTS];
u32 s_collisionEventCount = 0;

static void pushCollisionEvent(const ffe::physics::CollisionEvent3D& evt) {
    if (s_collisionEventCount < ffe::physics::MAX_COLLISION_EVENTS) {
        s_collisionEvents[s_collisionEventCount] = evt;
        ++s_collisionEventCount;
    }
    // Silently drop if buffer full — log at warn would thrash in busy scenes.
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// FFEContactListener — Jolt ContactListener that writes to the event buffer.
//
// Thread safety note: With 1 job thread (LEGACY tier) and Jolt's discrete
// collision mode, contact callbacks fire on the thread calling Update() (main
// thread). No mutex is needed for the event buffer.
// ---------------------------------------------------------------------------
namespace {

class FFEContactListener final : public JPH::ContactListener {
public:
    void OnContactAdded(
        const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        [[maybe_unused]] JPH::ContactSettings& ioSettings) override
    {
        ffe::physics::CollisionEvent3D evt;
        evt.entityA = lookupEntity(inBody1.GetID());
        evt.entityB = lookupEntity(inBody2.GetID());
        evt.type = ffe::physics::CollisionEventType::ENTER;

        // Use the first contact point on body 1 (world space).
        if (inManifold.mRelativeContactPointsOn1.size() > 0) {
            const JPH::RVec3 wp = inManifold.mBaseOffset + inManifold.mRelativeContactPointsOn1[0];
            evt.contactPoint = fromJoltR(wp);
        }
        evt.contactNormal = fromJolt(inManifold.mWorldSpaceNormal);

        pushCollisionEvent(evt);
    }

    void OnContactPersisted(
        const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        [[maybe_unused]] JPH::ContactSettings& ioSettings) override
    {
        ffe::physics::CollisionEvent3D evt;
        evt.entityA = lookupEntity(inBody1.GetID());
        evt.entityB = lookupEntity(inBody2.GetID());
        evt.type = ffe::physics::CollisionEventType::STAY;

        if (inManifold.mRelativeContactPointsOn1.size() > 0) {
            const JPH::RVec3 wp = inManifold.mBaseOffset + inManifold.mRelativeContactPointsOn1[0];
            evt.contactPoint = fromJoltR(wp);
        }
        evt.contactNormal = fromJolt(inManifold.mWorldSpaceNormal);

        pushCollisionEvent(evt);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override {
        // During OnContactRemoved, body data is NOT accessible (Jolt docs).
        // We can only get the BodyIDs from the sub-shape pair.
        ffe::physics::CollisionEvent3D evt;
        evt.entityA = lookupEntity(inSubShapePair.GetBody1ID());
        evt.entityB = lookupEntity(inSubShapePair.GetBody2ID());
        evt.type = ffe::physics::CollisionEventType::EXIT;
        // contactPoint and contactNormal remain zero — not available for EXIT events.

        pushCollisionEvent(evt);
    }
};

FFEContactListener s_contactListener;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Collision Callback State (file-scoped)
// ---------------------------------------------------------------------------
namespace {

ffe::physics::CollisionCallback3D s_collisionCallback = nullptr;
void* s_collisionCallbackUserData = nullptr;

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

    // Clamp maxBodies to our fixed mapping array size
    Physics3DConfig safeConfig = config;
    if (safeConfig.maxBodies > MAX_BODIES) {
        FFE_LOG_WARN("Physics3D",
                     "Requested maxBodies=%u exceeds MAX_BODIES=%u — clamping to %u",
                     safeConfig.maxBodies, MAX_BODIES, MAX_BODIES);
        safeConfig.maxBodies = MAX_BODIES;
    }

    // Initialize entity-body mapping
    initBodyEntityMapping();

    // Clear collision event buffer
    s_collisionEventCount = 0;

    // Clear collision callback
    s_collisionCallback = nullptr;
    s_collisionCallbackUserData = nullptr;

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
        safeConfig.maxBodies,
        0, // auto-detect mutex count
        safeConfig.maxBodyPairs,
        safeConfig.maxContactConstraints,
        *s_state.bpLayerInterface,
        *s_state.objVsBpFilter,
        *s_state.objPairFilter);

    // Register contact listener for collision events
    s_state.physicsSystem->SetContactListener(&s_contactListener);

    // Set gravity
    const glm::vec3 g = sanitizeVec3(safeConfig.gravity);
    s_state.physicsSystem->SetGravity(toJolt(g));

    s_state.initialized = true;
    FFE_LOG_INFO("Physics3D", "Jolt Physics initialized (maxBodies=%u, gravity=%.2f,%.2f,%.2f)",
                 safeConfig.maxBodies, g.x, g.y, g.z);

    return true;
}

void shutdownPhysics3D() {
    if (!s_state.initialized) {
        return;
    }

    // Unregister contact listener before destroying physics system
    if (s_state.physicsSystem != nullptr) {
        s_state.physicsSystem->SetContactListener(nullptr);
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

    // Clear entity mapping and collision state
    initBodyEntityMapping();
    s_collisionEventCount = 0;
    s_collisionCallback = nullptr;
    s_collisionCallbackUserData = nullptr;

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

    // Clear collision events at start of each physics step
    s_collisionEventCount = 0;

    // 1 collision step per update (sufficient for fixed-rate 60 Hz)
    s_state.physicsSystem->Update(safeDt, 1,
                                  s_state.tempAllocator,
                                  s_state.jobSystem);
}

// ---------------------------------------------------------------------------
// Body management — internal helper for shared creation logic
// ---------------------------------------------------------------------------
static BodyHandle3D createBodyInternal(const BodyDef3D& def) {
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

// ---------------------------------------------------------------------------
// Body management — public API
// ---------------------------------------------------------------------------

BodyHandle3D createBody(const BodyDef3D& def) {
    if (!s_state.initialized) {
        FFE_LOG_ERROR("Physics3D", "createBody called before initPhysics3D");
        return BodyHandle3D{};
    }

    const BodyHandle3D handle = createBodyInternal(def);
    // No entity mapping for the legacy overload (entityId remains INVALID_ENTITY)
    return handle;
}

BodyHandle3D createBody(const BodyDef3D& def, const u32 entityId) {
    if (!s_state.initialized) {
        FFE_LOG_ERROR("Physics3D", "createBody called before initPhysics3D");
        return BodyHandle3D{};
    }

    const BodyHandle3D handle = createBodyInternal(def);
    if (isValid(handle)) {
        const JPH::BodyID bodyId(handle.id);
        mapBodyToEntity(bodyId, entityId);
    }
    return handle;
}

u32 getBodyEntityId(const BodyHandle3D handle) {
    if (!isValid(handle)) { return INVALID_ENTITY; }
    const JPH::BodyID bodyId(handle.id);
    return lookupEntity(bodyId);
}

void destroyBody(const BodyHandle3D handle) {
    if (!s_state.initialized || !isValid(handle)) { return; }

    const JPH::BodyID bodyId(handle.id);

    // Clear entity mapping before removing the body
    unmapBody(bodyId);

    JPH::BodyInterface& bodyInterface = s_state.physicsSystem->GetBodyInterface();

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

// ---------------------------------------------------------------------------
// Collision Event System
// ---------------------------------------------------------------------------

const CollisionEvent3D* getCollisionEvents3D(u32& outCount) {
    outCount = s_collisionEventCount;
    return s_collisionEvents;
}

void clearCollisionEvents3D() {
    s_collisionEventCount = 0;
}

// ---------------------------------------------------------------------------
// Collision Callback Dispatch
// ---------------------------------------------------------------------------

void setCollisionCallback3D(const CollisionCallback3D callback, void* userData) {
    s_collisionCallback = callback;
    s_collisionCallbackUserData = userData;
}

void removeCollisionCallback3D() {
    s_collisionCallback = nullptr;
    s_collisionCallbackUserData = nullptr;
}

void dispatchCollisionEvents3D() {
    if (s_collisionCallback == nullptr || s_collisionEventCount == 0) {
        return;
    }

    // Iterate over all buffered events and fire the callback.
    // The callback is a raw function pointer — no virtual dispatch, no heap.
    for (u32 i = 0; i < s_collisionEventCount; ++i) {
        s_collisionCallback(s_collisionEvents[i], s_collisionCallbackUserData);
    }
}

// ---------------------------------------------------------------------------
// Raycasting
// ---------------------------------------------------------------------------

RayHit3D castRay(const glm::vec3& origin, const glm::vec3& direction, const f32 maxDistance) {
    RayHit3D result;

    if (!s_state.initialized) { return result; }

    // NaN/Inf guards
    const glm::vec3 safeOrigin = sanitizeVec3(origin);
    const glm::vec3 safeDir    = sanitizeVec3(direction);
    const float safeDist       = sanitize(maxDistance);

    if (safeDist <= 0.0f) { return result; }

    // Direction must be non-zero
    const float dirLenSq = safeDir.x * safeDir.x + safeDir.y * safeDir.y + safeDir.z * safeDir.z;
    if (dirLenSq < 1e-12f) { return result; }

    // Normalize direction and scale by maxDistance.
    // Jolt's RRayCast.mDirection encodes both direction and length —
    // hits are reported as a fraction [0, 1] of the full ray length.
    const float dirLen = std::sqrt(dirLenSq);
    const glm::vec3 normDir = safeDir / dirLen;
    const glm::vec3 scaledDir = normDir * safeDist;

    const JPH::RRayCast ray(toJoltR(safeOrigin), toJolt(scaledDir));
    JPH::RayCastResult hit;

    const JPH::NarrowPhaseQuery& query = s_state.physicsSystem->GetNarrowPhaseQuery();
    if (query.CastRay(ray, hit)) {
        result.valid = true;
        result.distance = hit.mFraction * safeDist;
        result.entityId = lookupEntity(hit.mBodyID);

        // Compute hit point
        const JPH::RVec3 hitPointJolt = ray.GetPointOnRay(hit.mFraction);
        result.hitPoint = fromJoltR(hitPointJolt);

        // Get the surface normal using the body lock interface (no-lock variant
        // since we're outside of physics update — safe to use locking interface).
        JPH::BodyLockRead lock(s_state.physicsSystem->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            result.hitNormal = fromJolt(
                body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPointJolt));
        }
    }

    return result;
}

u32 castRayAll(const glm::vec3& origin, const glm::vec3& direction, const f32 maxDistance,
               RayHit3D* outHits, const u32 maxHits) {
    if (outHits == nullptr || maxHits == 0) { return 0; }
    if (!s_state.initialized) { return 0; }

    // NaN/Inf guards
    const glm::vec3 safeOrigin = sanitizeVec3(origin);
    const glm::vec3 safeDir    = sanitizeVec3(direction);
    const float safeDist       = sanitize(maxDistance);

    if (safeDist <= 0.0f) { return 0; }

    const float dirLenSq = safeDir.x * safeDir.x + safeDir.y * safeDir.y + safeDir.z * safeDir.z;
    if (dirLenSq < 1e-12f) { return 0; }

    const float dirLen = std::sqrt(dirLenSq);
    const glm::vec3 normDir = safeDir / dirLen;
    const glm::vec3 scaledDir = normDir * safeDist;

    const JPH::RRayCast ray(toJoltR(safeOrigin), toJolt(scaledDir));

    // Use AllHitCollisionCollector (Jolt's built-in, uses std::vector internally
    // but this is not a per-frame hot path — raycasts are user-initiated queries).
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    JPH::RayCastSettings settings;

    const JPH::NarrowPhaseQuery& query = s_state.physicsSystem->GetNarrowPhaseQuery();
    query.CastRay(ray, settings, collector);

    if (!collector.HadHit()) { return 0; }

    // Sort by distance (nearest first)
    collector.Sort();

    const u32 cap = std::min(static_cast<u32>(collector.mHits.size()),
                             std::min(maxHits, MAX_RAY_HITS));

    for (u32 i = 0; i < cap; ++i) {
        const JPH::RayCastResult& joltHit = collector.mHits[i];

        RayHit3D& out = outHits[i];
        out.valid = true;
        out.distance = joltHit.mFraction * safeDist;
        out.entityId = lookupEntity(joltHit.mBodyID);

        const JPH::RVec3 hitPointJolt = ray.GetPointOnRay(joltHit.mFraction);
        out.hitPoint = fromJoltR(hitPointJolt);

        // Get surface normal via body lock
        JPH::BodyLockRead lock(s_state.physicsSystem->GetBodyLockInterface(), joltHit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            out.hitNormal = fromJolt(
                body.GetWorldSpaceSurfaceNormal(joltHit.mSubShapeID2, hitPointJolt));
        }
    }

    return cap;
}

} // namespace ffe::physics
