// engine/networking/replication.cpp
//
// Replication system foundation — registry, snapshots, interpolation.

#include "networking/replication.h"
#include "renderer/render_system.h"

#include <cmath>
#include <cstring>

namespace ffe::networking {

// ===========================================================================
// ReplicationRegistry
// ===========================================================================

bool ReplicationRegistry::registerComponent(
    const uint16_t componentId,
    const uint16_t maxSize,
    const SerializeFn ser,
    const DeserializeFn deser,
    const InterpolateFn interp)
{
    if (m_count >= MAX_REPLICATED_COMPONENTS) {
        return false;
    }
    // Reject duplicate componentId
    for (uint16_t i = 0; i < m_count; ++i) {
        if (m_components[i].componentId == componentId) {
            return false;
        }
    }
    m_components[m_count] = ReplicatedComponent{
        componentId, maxSize, ser, deser, interp
    };
    ++m_count;
    return true;
}

const ReplicatedComponent* ReplicationRegistry::find(const uint16_t componentId) const {
    for (uint16_t i = 0; i < m_count; ++i) {
        if (m_components[i].componentId == componentId) {
            return &m_components[i];
        }
    }
    return nullptr;
}

uint16_t ReplicationRegistry::count() const {
    return m_count;
}

const ReplicatedComponent* ReplicationRegistry::components() const {
    return m_components;
}

// ===========================================================================
// SnapshotBuffer
// ===========================================================================

SnapshotBuffer::SnapshotBuffer()
    : m_snapshots(std::make_unique<Snapshot[]>(SNAPSHOT_BUFFER_SIZE))
{
}

void SnapshotBuffer::push(const Snapshot& snapshot) {
    m_snapshots[m_head] = snapshot;
    m_head = (m_head + 1) % SNAPSHOT_BUFFER_SIZE;
    if (m_count < SNAPSHOT_BUFFER_SIZE) {
        ++m_count;
    }
}

const Snapshot* SnapshotBuffer::latest() const {
    if (m_count == 0) {
        return nullptr;
    }
    // m_head points to the NEXT write slot, so latest is one behind.
    const uint32_t idx = (m_head + SNAPSHOT_BUFFER_SIZE - 1) % SNAPSHOT_BUFFER_SIZE;
    return &m_snapshots[idx];
}

bool SnapshotBuffer::getInterpolationPair(
    const uint32_t renderTick,
    const Snapshot*& a,
    const Snapshot*& b,
    float& t) const
{
    if (m_count < 2) {
        return false;
    }

    // Gather stored snapshots in chronological order (oldest first).
    // The oldest is at index (m_head + SNAPSHOT_BUFFER_SIZE - m_count) % SIZE,
    // and they proceed forward from there.

    const Snapshot* older = nullptr;
    const Snapshot* newer = nullptr;

    for (uint32_t i = 0; i < m_count; ++i) {
        const uint32_t idx =
            (m_head + SNAPSHOT_BUFFER_SIZE - m_count + i) % SNAPSHOT_BUFFER_SIZE;
        const Snapshot* s = &m_snapshots[idx];

        if (s->tick <= renderTick) {
            older = s;
        } else {
            // First snapshot with tick > renderTick — this is the upper bound.
            newer = s;
            break;
        }
    }

    if (older == nullptr || newer == nullptr) {
        return false;
    }

    a = older;
    b = newer;

    const uint32_t span = newer->tick - older->tick;
    if (span == 0) {
        t = 0.0f;
    } else {
        t = static_cast<float>(renderTick - older->tick) / static_cast<float>(span);
    }
    return true;
}

uint32_t SnapshotBuffer::count() const {
    return m_count;
}

// ===========================================================================
// Helper: write / read floats to/from a byte buffer (memcpy, no aliasing UB)
// ===========================================================================

static void writeFloat(uint8_t* dst, const float val) {
    std::memcpy(dst, &val, sizeof(float));
}

static float readFloat(const uint8_t* src) {
    float val = 0.0f;
    std::memcpy(&val, src, sizeof(float));
    return val;
}

// ===========================================================================
// Transform serialisation (7 floats = 28 bytes)
// Layout: pos.x, pos.y, pos.z, rotation, scale.x, scale.y, scale.z
// ===========================================================================

uint16_t serializeTransform(const void* component, uint8_t* buffer, const uint16_t maxSize) {
    if (maxSize < TRANSFORM_SERIALIZED_SIZE) {
        return 0;
    }
    const auto* tx = static_cast<const ffe::Transform*>(component);
    uint8_t* p = buffer;
    writeFloat(p,      tx->position.x);
    writeFloat(p + 4,  tx->position.y);
    writeFloat(p + 8,  tx->position.z);
    writeFloat(p + 12, tx->rotation);
    writeFloat(p + 16, tx->scale.x);
    writeFloat(p + 20, tx->scale.y);
    writeFloat(p + 24, tx->scale.z);
    return TRANSFORM_SERIALIZED_SIZE;
}

bool deserializeTransform(void* component, const uint8_t* buffer, const uint16_t size) {
    if (size < TRANSFORM_SERIALIZED_SIZE) {
        return false;
    }
    auto* tx = static_cast<ffe::Transform*>(component);
    tx->position.x = readFloat(buffer);
    tx->position.y = readFloat(buffer + 4);
    tx->position.z = readFloat(buffer + 8);
    tx->rotation   = readFloat(buffer + 12);
    tx->scale.x    = readFloat(buffer + 16);
    tx->scale.y    = readFloat(buffer + 20);
    tx->scale.z    = readFloat(buffer + 24);
    return true;
}

void interpolateTransform(void* out, const void* va, const void* vb, const float t) {
    const auto* a = static_cast<const ffe::Transform*>(va);
    const auto* b = static_cast<const ffe::Transform*>(vb);
    auto* o = static_cast<ffe::Transform*>(out);

    // Lerp position
    o->position.x = a->position.x + (b->position.x - a->position.x) * t;
    o->position.y = a->position.y + (b->position.y - a->position.y) * t;
    o->position.z = a->position.z + (b->position.z - a->position.z) * t;

    // Lerp rotation (simple angle lerp — suitable for small deltas)
    o->rotation = a->rotation + (b->rotation - a->rotation) * t;

    // Lerp scale
    o->scale.x = a->scale.x + (b->scale.x - a->scale.x) * t;
    o->scale.y = a->scale.y + (b->scale.y - a->scale.y) * t;
    o->scale.z = a->scale.z + (b->scale.z - a->scale.z) * t;
}

// ===========================================================================
// Transform3D serialisation (10 floats = 40 bytes)
// Layout: pos.x, pos.y, pos.z, rot.w, rot.x, rot.y, rot.z, scale.x, scale.y, scale.z
// ===========================================================================

uint16_t serializeTransform3D(const void* component, uint8_t* buffer, const uint16_t maxSize) {
    if (maxSize < TRANSFORM3D_SERIALIZED_SIZE) {
        return 0;
    }
    const auto* tx = static_cast<const ffe::Transform3D*>(component);
    uint8_t* p = buffer;
    writeFloat(p,      tx->position.x);
    writeFloat(p + 4,  tx->position.y);
    writeFloat(p + 8,  tx->position.z);
    writeFloat(p + 12, tx->rotation.w);
    writeFloat(p + 16, tx->rotation.x);
    writeFloat(p + 20, tx->rotation.y);
    writeFloat(p + 24, tx->rotation.z);
    writeFloat(p + 28, tx->scale.x);
    writeFloat(p + 32, tx->scale.y);
    writeFloat(p + 36, tx->scale.z);
    return TRANSFORM3D_SERIALIZED_SIZE;
}

bool deserializeTransform3D(void* component, const uint8_t* buffer, const uint16_t size) {
    if (size < TRANSFORM3D_SERIALIZED_SIZE) {
        return false;
    }
    auto* tx = static_cast<ffe::Transform3D*>(component);
    tx->position.x = readFloat(buffer);
    tx->position.y = readFloat(buffer + 4);
    tx->position.z = readFloat(buffer + 8);
    tx->rotation.w = readFloat(buffer + 12);
    tx->rotation.x = readFloat(buffer + 16);
    tx->rotation.y = readFloat(buffer + 20);
    tx->rotation.z = readFloat(buffer + 24);
    tx->scale.x    = readFloat(buffer + 28);
    tx->scale.y    = readFloat(buffer + 32);
    tx->scale.z    = readFloat(buffer + 36);
    return true;
}

// ---------------------------------------------------------------------------
// Slerp for quaternions (manual — no glm dependency in hot path)
// ---------------------------------------------------------------------------
static void slerpQuat(
    float outQ[4],
    const float aQ[4], // w, x, y, z
    const float bQ[4],
    const float t)
{
    // Compute dot product
    float dot = aQ[0]*bQ[0] + aQ[1]*bQ[1] + aQ[2]*bQ[2] + aQ[3]*bQ[3];

    // If dot < 0, negate one quaternion to take the short path
    float bNeg[4] = { bQ[0], bQ[1], bQ[2], bQ[3] };
    if (dot < 0.0f) {
        dot = -dot;
        bNeg[0] = -bQ[0];
        bNeg[1] = -bQ[1];
        bNeg[2] = -bQ[2];
        bNeg[3] = -bQ[3];
    }

    // If quaternions are very close, fall back to lerp to avoid division by zero
    constexpr float SLERP_THRESHOLD = 0.9995f;
    if (dot > SLERP_THRESHOLD) {
        for (int i = 0; i < 4; ++i) {
            outQ[i] = aQ[i] + (bNeg[i] - aQ[i]) * t;
        }
        // Normalize
        const float len = std::sqrt(outQ[0]*outQ[0] + outQ[1]*outQ[1] +
                                    outQ[2]*outQ[2] + outQ[3]*outQ[3]);
        if (len > 0.0f) {
            const float invLen = 1.0f / len;
            for (int i = 0; i < 4; ++i) {
                outQ[i] *= invLen;
            }
        }
        return;
    }

    // Standard slerp
    const float theta = std::acos(dot);
    const float sinTheta = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / sinTheta;
    const float wb = std::sin(t * theta) / sinTheta;

    for (int i = 0; i < 4; ++i) {
        outQ[i] = aQ[i] * wa + bNeg[i] * wb;
    }
}

void interpolateTransform3D(void* out, const void* va, const void* vb, const float t) {
    const auto* a = static_cast<const ffe::Transform3D*>(va);
    const auto* b = static_cast<const ffe::Transform3D*>(vb);
    auto* o = static_cast<ffe::Transform3D*>(out);

    // Lerp position
    o->position.x = a->position.x + (b->position.x - a->position.x) * t;
    o->position.y = a->position.y + (b->position.y - a->position.y) * t;
    o->position.z = a->position.z + (b->position.z - a->position.z) * t;

    // Slerp rotation quaternion
    const float aQ[4] = { a->rotation.w, a->rotation.x, a->rotation.y, a->rotation.z };
    const float bQ[4] = { b->rotation.w, b->rotation.x, b->rotation.y, b->rotation.z };
    float oQ[4] = {};
    slerpQuat(oQ, aQ, bQ, t);
    o->rotation.w = oQ[0];
    o->rotation.x = oQ[1];
    o->rotation.y = oQ[2];
    o->rotation.z = oQ[3];

    // Lerp scale
    o->scale.x = a->scale.x + (b->scale.x - a->scale.x) * t;
    o->scale.y = a->scale.y + (b->scale.y - a->scale.y) * t;
    o->scale.z = a->scale.z + (b->scale.z - a->scale.z) * t;
}

// ===========================================================================
// registerDefaultComponents
// ===========================================================================

void registerDefaultComponents(ReplicationRegistry& registry) {
    registry.registerComponent(
        COMPONENT_ID_TRANSFORM,
        TRANSFORM_SERIALIZED_SIZE,
        serializeTransform,
        deserializeTransform,
        interpolateTransform);

    registry.registerComponent(
        COMPONENT_ID_TRANSFORM3D,
        TRANSFORM3D_SERIALIZED_SIZE,
        serializeTransform3D,
        deserializeTransform3D,
        interpolateTransform3D);
}

} // namespace ffe::networking
