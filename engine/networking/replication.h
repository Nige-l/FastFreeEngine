#pragma once

// engine/networking/replication.h
//
// Replication system foundation for FFE networking.
//
// Provides:
//   - ReplicationRegistry: register component types for network replication
//   - Snapshot / EntitySnapshot: serialised ECS state at one tick
//   - SnapshotBuffer: ring buffer of recent snapshots for client interpolation
//   - Default serialize/deserialize/interpolate for Transform and Transform3D
//
// All data structures use fixed-size arrays (no per-frame heap allocations).
// SnapshotBuffer itself must be heap-allocated (std::unique_ptr) because it
// holds ~1 MB of snapshot data — too large for the stack.
//
// Tiers: LEGACY (primary), STANDARD, MODERN.

#include <cstdint>
#include <cstring>
#include <memory>

namespace ffe::networking {

// ---------------------------------------------------------------------------
// Function pointer types for component serialisation (no std::function)
// ---------------------------------------------------------------------------

/// Serialize a component into a byte buffer.
/// Returns number of bytes written, or 0 on failure.
using SerializeFn = uint16_t(*)(const void* component, uint8_t* buffer, uint16_t maxSize);

/// Deserialize a component from a byte buffer.
/// Returns true on success.
using DeserializeFn = bool(*)(void* component, const uint8_t* buffer, uint16_t size);

/// Interpolate between two component states.
/// Writes result to `out`. `a` and `b` are the two states, `t` is in [0, 1].
/// May be null — null means snap to newer value (no interpolation).
using InterpolateFn = void(*)(void* out, const void* a, const void* b, float t);

// ---------------------------------------------------------------------------
// Replicated component descriptor
// ---------------------------------------------------------------------------
struct ReplicatedComponent {
    uint16_t      componentId;        ///< Unique ID for wire protocol
    uint16_t      maxSerializedSize;  ///< Max bytes when serialised
    SerializeFn   serialize;
    DeserializeFn deserialize;
    InterpolateFn interpolate;        ///< May be nullptr (snap instead of interp)
};

constexpr uint16_t MAX_REPLICATED_COMPONENTS = 32;

// ---------------------------------------------------------------------------
// ReplicationRegistry — tracks which component types replicate over the wire
// ---------------------------------------------------------------------------
class ReplicationRegistry {
public:
    /// Register a component type for replication.
    /// Returns false if the registry is full or componentId is already registered.
    bool registerComponent(uint16_t componentId, uint16_t maxSize,
                           SerializeFn ser, DeserializeFn deser,
                           InterpolateFn interp = nullptr);

    /// Look up a registered component by its wire ID.
    /// Returns nullptr if not found.
    const ReplicatedComponent* find(uint16_t componentId) const;

    /// Number of registered component types.
    uint16_t count() const;

    /// Direct array access to registered components (0..count()-1).
    const ReplicatedComponent* components() const;

private:
    ReplicatedComponent m_components[MAX_REPLICATED_COMPONENTS] = {};
    uint16_t m_count = 0;
};

// ---------------------------------------------------------------------------
// Snapshot structures — serialised ECS state at one tick
// ---------------------------------------------------------------------------

constexpr uint32_t MAX_SNAPSHOT_ENTITIES = 256;
constexpr uint16_t MAX_ENTITY_COMPONENT_DATA = 256;

struct EntitySnapshot {
    uint32_t entityId       = 0;
    uint16_t dataSize       = 0;
    uint16_t componentMask  = 0;  ///< Bitmask of which components are present
    uint8_t  componentData[MAX_ENTITY_COMPONENT_DATA] = {};
};

struct Snapshot {
    uint32_t       tick        = 0;
    uint32_t       entityCount = 0;
    EntitySnapshot entities[MAX_SNAPSHOT_ENTITIES] = {};
};

// ---------------------------------------------------------------------------
// SnapshotBuffer — ring buffer of snapshots for client-side interpolation
// ---------------------------------------------------------------------------

constexpr uint32_t SNAPSHOT_BUFFER_SIZE = 16;

class SnapshotBuffer {
public:
    SnapshotBuffer();

    /// Push a snapshot into the ring buffer (overwrites oldest if full).
    void push(const Snapshot& snapshot);

    /// Get the most recently pushed snapshot, or nullptr if empty.
    const Snapshot* latest() const;

    /// Find two snapshots bracketing `renderTick` for interpolation.
    /// On success, sets `a` (older), `b` (newer), `t` (interpolation factor
    /// in [0,1]), and returns true.  Returns false if fewer than 2 snapshots
    /// are stored or renderTick is outside the buffered range.
    bool getInterpolationPair(uint32_t renderTick,
                              const Snapshot*& a, const Snapshot*& b,
                              float& t) const;

    /// Number of snapshots currently stored (0..SNAPSHOT_BUFFER_SIZE).
    uint32_t count() const;

private:
    // Heap-allocated: each Snapshot is ~66 KB, 16 of them is ~1 MB.
    std::unique_ptr<Snapshot[]> m_snapshots;
    uint32_t m_head  = 0;  ///< Next write position
    uint32_t m_count = 0;
};

// ---------------------------------------------------------------------------
// Default component registration
// ---------------------------------------------------------------------------

/// Well-known component IDs for built-in types.
constexpr uint16_t COMPONENT_ID_TRANSFORM   = 1;
constexpr uint16_t COMPONENT_ID_TRANSFORM3D = 2;

/// Register Transform (id=1) and Transform3D (id=2) with their
/// serialize / deserialize / interpolate functions.
void registerDefaultComponents(ReplicationRegistry& registry);

// ---------------------------------------------------------------------------
// Serialize / deserialize / interpolate for Transform
// ---------------------------------------------------------------------------
// Transform layout: position.x, position.y, position.z, rotation,
//                   scale.x, scale.y, scale.z  = 7 floats = 28 bytes

constexpr uint16_t TRANSFORM_SERIALIZED_SIZE = 28; // 7 * sizeof(float)

uint16_t serializeTransform(const void* component, uint8_t* buffer, uint16_t maxSize);
bool     deserializeTransform(void* component, const uint8_t* buffer, uint16_t size);
void     interpolateTransform(void* out, const void* a, const void* b, float t);

// ---------------------------------------------------------------------------
// Serialize / deserialize / interpolate for Transform3D
// ---------------------------------------------------------------------------
// Transform3D layout: position (3), rotation quat (4), scale (3) = 10 floats = 40 bytes

constexpr uint16_t TRANSFORM3D_SERIALIZED_SIZE = 40; // 10 * sizeof(float)

uint16_t serializeTransform3D(const void* component, uint8_t* buffer, uint16_t maxSize);
bool     deserializeTransform3D(void* component, const uint8_t* buffer, uint16_t size);
void     interpolateTransform3D(void* out, const void* a, const void* b, float t);

} // namespace ffe::networking
