#pragma once

// visual_scripting.h — Node-based visual scripting runtime for FFE.
//
// VisualScriptingSystem loads graph assets (JSON files), topologically sorts
// nodes at load time, and executes them per-frame via function pointer dispatch.
// No virtual dispatch, no per-frame heap allocation.
//
// Pool capacity: 32 graph slots (slot 0 reserved as null/invalid).
// File size limit: 512 KB per graph file.
// Node limit: 256 nodes per graph.
// Connection limit: 512 connections per graph.
// Per-frame node call cap: 8192 (256 graphs × 32 nodes).
//
// Security: loadGraph() canonicalizes the path, verifies it is within the
// declared asset root, enforces file size and JSON depth limits, validates
// node type whitelist, and performs DFS cycle detection and reachability
// validation before storing any graph in the pool.
//
// Tiers: RETRO / LEGACY / STANDARD / MODERN — the graph executor has no GPU
// dependency. All data is CPU-only.
//
// File ownership: engine/core/ (engine-dev)

#include "core/types.h"
#include "core/ecs.h"

#include <cstdint>
#include <string_view>

namespace ffe {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint32_t MAX_NODES_PER_GRAPH       = 256u;
static constexpr uint32_t MAX_CONNECTIONS_PER_GRAPH  = 512u;
static constexpr uint32_t MAX_GRAPHS_LOADED          = 32u;
static constexpr uint32_t MAX_NODE_CALLS_PER_FRAME   = 8192u;
static constexpr uint32_t MAX_PENDING_DESTROYS        = 256u;
static constexpr uint8_t  MAX_PORTS_PER_NODE          = 8u;

// ---------------------------------------------------------------------------
// Type aliases
// ---------------------------------------------------------------------------

using NodeTypeId = uint32_t;  // Per-type static identifier. 0 = invalid.
using NodeId     = uint32_t;  // Per-instance identifier within a graph. 0 = invalid.
using PortId     = uint8_t;   // Port index within a node. 0-based. Max 8 ports per node.

// ---------------------------------------------------------------------------
// GraphHandle — opaque 32-bit handle to a loaded graph asset.
//
// id == 0 is the null/invalid value. Valid handles have id in [1, 31].
// Consistent with PrefabHandle, MeshHandle, and all other FFE asset handles.
// ---------------------------------------------------------------------------
struct GraphHandle {
    uint32_t id = 0;
    explicit operator bool() const { return id != 0; }
};

// Forward declaration needed for isValid to query the static occupancy table.
class VisualScriptingSystem;

// isValid returns true only if the handle id is non-zero AND the pool slot
// is currently occupied (i.e., the graph has not been unloaded).
// Checks VisualScriptingSystem::s_occupied[], which is kept in sync by
// loadGraph() and unloadGraph().
bool isValid(const GraphHandle h);
static_assert(sizeof(GraphHandle) == 4);

// ---------------------------------------------------------------------------
// PortType — discriminant for PortValue union
// ---------------------------------------------------------------------------

enum class PortType : uint8_t {
    FLOAT  = 0,   // 32-bit float
    BOOL   = 1,   // Boolean
    ENTITY = 2,   // EntityId (uint32_t)
    VEC2   = 3,   // Two floats (x, y)
    VEC3   = 4,   // Three floats (x, y, z)
};

// ---------------------------------------------------------------------------
// NodeCategory — informational classification for the editor
// ---------------------------------------------------------------------------

enum class NodeCategory : uint8_t {
    EVENT  = 0,
    ACTION = 1,
    DATA   = 2,
    FLOW   = 3,
};

// ---------------------------------------------------------------------------
// PortValue — fixed-size discriminated union. No heap allocation.
// Size: 12 bytes (largest member is VEC3) + 1 byte tag + 3 bytes padding = 16.
// ---------------------------------------------------------------------------

union PortPayload {
    float    f;             // FLOAT
    bool     b;             // BOOL
    uint32_t entity;        // ENTITY
    struct { float x, y; }       vec2;  // VEC2
    struct { float x, y, z; }    vec3;  // VEC3
};

struct PortValue {
    PortPayload data = {};
    PortType    type = PortType::FLOAT;
    uint8_t     _pad[2] = {};  // Explicit padding, zero-initialised
};
static_assert(sizeof(PortValue) == 16, "PortValue must be 16 bytes");

// ---------------------------------------------------------------------------
// PortDir — direction of a port within a node definition
// ---------------------------------------------------------------------------

enum class PortDir : uint8_t {
    INPUT  = 0,
    OUTPUT = 1,
};

// ---------------------------------------------------------------------------
// PortDef — definition of a single port on a node type
// ---------------------------------------------------------------------------

struct PortDef {
    PortDir  dir;
    PortType type;
    char     name[16];  // Human-readable label. Null-terminated.
};

// ---------------------------------------------------------------------------
// NodeExecContext — passed to every node execute() function.
// All pointers are into per-frame scratch storage — never heap.
// ---------------------------------------------------------------------------

struct NodeExecContext {
    const PortValue* inputs;       // Pointer into per-frame scratch (input port values)
    PortValue*       outputs;      // Pointer into per-frame scratch (output port values)
    uint8_t          inputCount;
    uint8_t          outputCount;
    EntityId         entity;       // Entity this graph is attached to
    float            dt;           // Delta time (seconds)
    World*           world;        // ECS World (never null during execution)
};

// ---------------------------------------------------------------------------
// NodeExecuteFn — function pointer type. No virtual dispatch.
// ---------------------------------------------------------------------------

using NodeExecuteFn = void (*)(const NodeExecContext& ctx);

// ---------------------------------------------------------------------------
// NodeDef — static per-type metadata. One entry per node type.
// Allocated once at startup, never modified.
// ---------------------------------------------------------------------------

struct NodeDef {
    NodeTypeId    typeId;                       // Unique type identifier
    char          typeName[32];                 // Canonical string name (matches JSON whitelist)
    NodeExecuteFn execute;                      // Frame execution function pointer
    PortDef       ports[MAX_PORTS_PER_NODE];    // Port definitions (inputs first, then outputs)
    uint8_t       portCount;                    // Total number of ports
    uint8_t       inputCount;                   // Number of INPUT ports (first inputCount entries)
    uint8_t       outputCount;                  // Number of OUTPUT ports (last outputCount entries)
    NodeCategory  category;                     // Informational
};

// ---------------------------------------------------------------------------
// NodeInstance — per-instance runtime data for one node in a loaded graph.
// ---------------------------------------------------------------------------

struct NodeInstance {
    NodeId    id;        // Unique within the graph. 0 = invalid.
    NodeTypeId typeId;   // Which NodeDef this instance uses.

    // Default port values from JSON "defaults". Overwritten at execution start
    // by incoming connection values. Index matches NodeDef::ports order.
    PortValue defaults[MAX_PORTS_PER_NODE];
};

// ---------------------------------------------------------------------------
// Connection — directed edge between two nodes.
// ---------------------------------------------------------------------------

struct Connection {
    NodeId  srcNode;   // Source node ID
    PortId  srcPort;   // Output port index on source node (0-based within output ports)
    NodeId  dstNode;   // Destination node ID
    PortId  dstPort;   // Input port index on destination node (0-based within input ports)
};

// ---------------------------------------------------------------------------
// GraphAsset — loaded, validated, and topologically sorted graph.
//
// Heap-allocated (via new) because of its size (~38 KB per graph).
// Pool stores raw owning pointers, same pattern as PrefabSystem::m_pool.
// All arrays are fixed-size — no std::vector.
// ---------------------------------------------------------------------------

struct GraphAsset {
    NodeInstance  nodes[MAX_NODES_PER_GRAPH];
    uint32_t      nodeCount = 0;

    Connection    connections[MAX_CONNECTIONS_PER_GRAPH];
    uint32_t      connectionCount = 0;

    // Topologically sorted execution order.
    // Each entry is an index into nodes[] (0-based).
    // Computed once at load by DFS. Reused every frame.
    uint16_t      execOrder[MAX_NODES_PER_GRAPH];
    uint32_t      execOrderCount = 0;

    // Per-node port scratch offset: scratch[portScratchOffset[i]] is the first
    // PortValue slot for node i. totalPortCount is the sum of portCount across
    // all nodes — the scratch array size needed per graph per frame.
    uint32_t      portScratchOffset[MAX_NODES_PER_GRAPH];
    uint32_t      totalPortCount = 0;

    // Canonical path this asset was loaded from.
    char          sourcePath[512] = {};
};

// ---------------------------------------------------------------------------
// GraphComponent — ECS component. Attaches a graph asset to an entity.
// Size: 5 bytes, padded to 8 bytes.
// ---------------------------------------------------------------------------

struct GraphComponent {
    GraphHandle handle;   // Which graph asset to execute
    bool        active;   // If false, executor skips this entity
};
static_assert(sizeof(GraphComponent) <= 8);

// ---------------------------------------------------------------------------
// VisualScriptingSystem — load, manage, and execute node graphs.
//
// One instance lives in ScriptEngine (alongside PrefabSystem). All calls
// must be made from the main thread.
//
// Pool capacity: 32 graph slots. Slot 0 is reserved (null handle).
// File size limit: 512 KB per graph file.
// Node limit: 256 nodes per graph.
// Connection limit: 512 connections per graph.
//
// Tiers: LEGACY and above. No GPU dependency.
// ---------------------------------------------------------------------------

class VisualScriptingSystem {
public:
    VisualScriptingSystem();
    ~VisualScriptingSystem();

    // Non-copyable, non-movable.
    VisualScriptingSystem(const VisualScriptingSystem&) = delete;
    VisualScriptingSystem& operator=(const VisualScriptingSystem&) = delete;
    VisualScriptingSystem(VisualScriptingSystem&&) = delete;
    VisualScriptingSystem& operator=(VisualScriptingSystem&&) = delete;

    // Set the asset root for path validation.
    // Identical contract to PrefabSystem::setAssetRoot().
    // Must be called before loadGraph().
    void setAssetRoot(std::string_view root);

    // --- Cold path ---

    // Load a graph asset from a JSON file at `path`.
    // Validates path, parses JSON, validates nodes/connections, detects cycles,
    // performs reachability check, computes topological sort.
    // Returns GraphHandle{0} on any error; logs the reason.
    GraphHandle loadGraph(std::string_view path);

    // Unload a loaded graph.
    // Entities with GraphComponent referencing this handle will have execute()
    // skipped from the next frame (executor checks handle validity first).
    // Passing GraphHandle{0} or an unloaded handle is a no-op.
    void unloadGraph(GraphHandle handle);

    // Return the number of currently loaded graphs.
    int getGraphCount() const;

    // --- Per-frame hot path ---

    // Execute all active GraphComponents in `world`.
    // Called once per frame. Uses an internal static scratch buffer — no heap.
    // Target: 20-node graph < 0.1 ms on LEGACY tier.
    void execute(World& world, float dt);

    // --- Editor helpers ---

    // Return a pointer to the GraphAsset for inspection (editor, tests).
    // Returns nullptr if handle is invalid or unloaded.
    const GraphAsset* getGraphAsset(GraphHandle handle) const;

    // Return the NodeDef for a given typeId. Returns nullptr if out of range.
    static const NodeDef* getNodeDef(NodeTypeId typeId);

    // Return the total number of registered built-in node types.
    static int getNodeTypeCount();

    // Register the audio play-sound callback. Called once by ScriptEngine after
    // audio init. Avoids a circular link dependency (ffe_audio links ffe_core;
    // ffe_core cannot link ffe_audio). The callback receives a SoundHandle id
    // (uint32_t); it is a no-op if the id == 0 or audio is unavailable.
    using PlaySoundFn = void (*)(uint32_t soundId);
    static void registerPlaySoundFn(PlaySoundFn fn);

private:
    static constexpr int MAX_GRAPHS = static_cast<int>(MAX_GRAPHS_LOADED);

    // Heap-allocated graph assets. Null when slot is unoccupied.
    // Matches PrefabSystem's m_pool pointer-per-slot pattern.
    GraphAsset* m_pool[MAX_GRAPHS] = {};
    bool        m_occupied[MAX_GRAPHS] = {};

    // Static occupancy table mirroring m_occupied, consulted by the free
    // function isValid(GraphHandle) which has no system pointer.
    // Kept in sync with m_occupied in loadGraph() and unloadGraph().
    static bool s_occupied[MAX_GRAPHS_LOADED];

    friend bool isValid(const GraphHandle h);
    int         m_count = 0;
    char        m_assetRoot[512] = {};

    // Per-frame node call counter. Reset at the start of each execute().
    uint32_t m_nodeCallsThisFrame = 0;

    // Deferred destroy buffer — populated by DestroyEntity nodes,
    // flushed after the entity iteration loop to prevent mid-iteration
    // invalidation of the ECS entity list.
    EntityId m_pendingDestroys[MAX_PENDING_DESTROYS] = {};
    uint32_t m_pendingDestroyCount = 0;

    // Internal static scratch buffer for per-frame PortValue arrays.
    // Size: MAX_NODES_PER_GRAPH * MAX_PORTS_PER_NODE * sizeof(PortValue)
    //     = 256 * 8 * 16 = 32 KB per graph.
    // We hold one buffer for the currently-executing graph.
    static constexpr uint32_t SCRATCH_COUNT =
        MAX_NODES_PER_GRAPH * MAX_PORTS_PER_NODE;
    PortValue m_scratch[SCRATCH_COUNT] = {};
};

} // namespace ffe
