# ADR: Phase 10 M2 — Visual Scripting

**Status:** Accepted
**Author:** architect
**Date:** 2026-03-08
**Tiers:** LEGACY+ (OpenGL 3.3 minimum; the graph executor has no GPU dependency)
**Security Review Required:** YES — graph assets are external JSON files read from disk (path traversal, file size bombs, JSON depth attacks, arbitrary node-type injection, and cycle-based DoS all apply).

---

## 1. Context

FFE now has a mature Lua scripting layer (~220 bindings) that covers every major subsystem. Lua is powerful and expressive, but it requires text editing — there is no visual feedback about data flow, no drag-and-drop composition, and no in-engine way for non-programmers to wire logic together without writing code.

Unity's Visual Scripting (Bolt), Unreal's Blueprints, and Godot's VisualScript all address this gap: a node-based graph editor where logic nodes are connected by typed ports, evaluated in topological order each frame. Beginners who find text programming intimidating can build real game logic visually. Experienced developers can prototype quickly.

Phase 10 M2 delivers the first usable iteration of visual scripting in FFE: a graph asset format, a runtime executor, a built-in node library of 11 nodes, three Lua bindings, and a node canvas panel docked in the standalone editor.

### Scope of M2 — What Is In

- **Graph asset format:** JSON file describing nodes, connections, and default port values.
- **Node graph runtime:** `VisualScriptingSystem` — loads graphs, topologically sorts at load time, executes per-frame via function pointer array. No virtual dispatch, no per-frame heap allocation.
- **Built-in node library:** 11 nodes covering the most common beginner game-logic patterns (see Section 2.3).
- **ECS integration:** `GraphComponent` attaches a graph to any entity.
- **Lua bindings:** `ffe.loadGraph`, `ffe.attachGraph`, `ffe.detachGraph` (3 bindings).
- **Editor canvas panel:** `GraphEditorPanel` — pan/zoom, drag nodes, bezier connections, right-click add-node menu, read-only in play mode. Docked in the existing editor dockspace.
- **Security hardening:** Node type whitelist, cycle detection at load, max node/connection counts, path canonicalization matching the PrefabSystem pipeline.

### Scope of M2 — What Is Out

- Custom/user-defined node types (register-your-own-node API) — deferred to M3 or later.
- Sub-graphs / nested graphs — deferred.
- Data-flow graph evaluation (lazy pull-based evaluation, fan-out signals) — M2 uses push-based topological execution only.
- Variables panel / persistent state between frames beyond what nodes write to ECS components — deferred.
- Breakpoint debugging and step-through in the editor — deferred.
- Graph profiling overlay — deferred.
- Saving graph edits made in the editor canvas back to disk — M2 canvas is view+layout only; authoring workflow writes JSON by hand or via a future save step.

---

## 2. Decisions

### 2.1 File Locations and Agent Ownership

| File | Owner |
|------|-------|
| `engine/core/visual_scripting.h` | `engine-dev` |
| `engine/core/visual_scripting.cpp` | `engine-dev` |
| `engine/scripting/script_engine.h` (add member + 3 bindings) | `engine-dev` |
| `engine/scripting/script_engine.cpp` (bind `ffe.loadGraph`, `ffe.attachGraph`, `ffe.detachGraph`) | `engine-dev` |
| `tests/core/test_visual_scripting.cpp` | `engine-dev` |
| `engine/editor/graph_editor_panel.h` | `renderer-specialist` |
| `engine/editor/graph_editor_panel.cpp` | `renderer-specialist` |
| `engine/core/.context.md` (Visual Scripting section) | `engine-dev` |
| `engine/scripting/.context.md` (new bindings) | `api-designer` |

**Rationale for `engine/core/`:** The visual scripting runtime is an ECS-level concern — it reads entities, attaches `GraphComponent`, and drives per-frame execution. It has no GPU dependency. The same reasoning that placed `PrefabSystem` in `engine/core/` applies here: the node executor is entity/component logic, not rendering logic. The editor canvas panel (`graph_editor_panel`) lives in `engine/editor/` because it is ImGui code that only compiles under `#ifdef FFE_EDITOR`.

---

### 2.2 Node Graph Model

#### Type Identifiers

```cpp
namespace ffe {

using NodeTypeId = uint32_t;   // Per-type static identifier. 0 = invalid.
using NodeId     = uint32_t;   // Per-instance identifier within a graph. 0 = invalid.
using PortId     = uint8_t;    // Port index within a node. 0-based. Max 8 ports per node.

// GraphHandle follows the same opaque uint32_t pattern as PrefabHandle, MeshHandle, etc.
struct GraphHandle {
    uint32_t id = 0;           // 0 = invalid/null handle.
    explicit operator bool() const { return id != 0; }
};
inline bool isValid(const GraphHandle h) { return h.id != 0; }
static_assert(sizeof(GraphHandle) == 4);

} // namespace ffe
```

#### Port Types and PortValue

```cpp
namespace ffe {

enum class PortType : uint8_t {
    FLOAT  = 0,    // 32-bit float
    BOOL   = 1,    // Boolean
    ENTITY = 2,    // EntityId (uint32_t)
    VEC2   = 3,    // Two floats (x, y)
    VEC3   = 4,    // Three floats (x, y, z)
};

// PortValue is a fixed-size discriminated union. No heap allocation.
// Size: 12 bytes (largest member is VEC3: 3 x float = 12 bytes) + 1 byte tag,
// padded to 16 bytes by the compiler. Used in per-frame scratch arrays.
union PortPayload {
    float   f;            // FLOAT
    bool    b;            // BOOL
    uint32_t entity;      // ENTITY
    struct { float x, y; }       vec2;   // VEC2
    struct { float x, y, z; }    vec3;   // VEC3
};

struct PortValue {
    PortPayload data = {};
    PortType    type = PortType::FLOAT;
    uint8_t     _pad[2] = {};   // Explicit padding, zero-initialised
};
static_assert(sizeof(PortValue) == 16);

} // namespace ffe
```

`PortValue` is stored in a flat per-graph scratch array at execution time. The array is either stack-allocated (small graphs) or drawn from the frame arena. No per-frame heap allocation.

#### Port Direction

```cpp
namespace ffe {

enum class PortDir : uint8_t {
    INPUT  = 0,
    OUTPUT = 1,
};

struct PortDef {
    PortDir dir;
    PortType type;
    char name[16];   // Human-readable label for editor display. Null-terminated.
};

} // namespace ffe
```

#### NodeDef — Static Per-Type Metadata

`NodeDef` describes a node type. It is constant data: one `NodeDef` per node type, allocated once at startup, never modified. No heap per invocation.

```cpp
namespace ffe {

// Maximum ports per node (inputs + outputs combined).
static constexpr uint8_t MAX_PORTS_PER_NODE = 8;

// Execution context passed to every node's execute() function pointer.
// The executor fills src_values before calling each node; the node writes
// to dst_values. No heap allocation — both arrays are slices of the
// per-frame scratch array.
struct NodeExecContext {
    const PortValue* inputs;       // Pointer into per-frame scratch (input port values)
    PortValue*       outputs;      // Pointer into per-frame scratch (output port values)
    uint8_t          inputCount;
    uint8_t          outputCount;
    EntityId         entity;       // Entity this graph is attached to
    float            dt;           // Delta time (seconds) for this frame
    World*           world;        // ECS World (never null during execution)
};

// Function pointer type for node execution. No virtual dispatch.
using NodeExecuteFn = void (*)(const NodeExecContext& ctx);

struct NodeDef {
    NodeTypeId    typeId;                          // Unique type identifier
    char          typeName[32];                    // Canonical string name (matches JSON whitelist)
    NodeExecuteFn execute;                         // Frame execution function pointer
    PortDef       ports[MAX_PORTS_PER_NODE];       // Port definitions (input then output, by convention)
    uint8_t       portCount;                       // Total number of ports
    uint8_t       inputCount;                      // Number of INPUT ports (first inputCount entries)
    uint8_t       outputCount;                     // Number of OUTPUT ports (last outputCount entries)
};

} // namespace ffe
```

All 11 built-in `NodeDef` instances are `static constexpr` (or `static const`) globals defined in `visual_scripting.cpp`. The node library is a flat array indexed by `NodeTypeId`.

#### NodeInstance — Per-Instance Runtime Data

```cpp
namespace ffe {

// One NodeInstance per node in a loaded graph.
// Stores the node's identity and default port values from JSON.
// Layout data (editor position) is stored separately in GraphLayoutData,
// which is only needed by the editor and not allocated in headless mode.
struct NodeInstance {
    NodeId     id;            // Unique within the graph. 0 = invalid.
    NodeTypeId typeId;        // Which NodeDef this instance uses.

    // Default port values from the JSON "defaults" object.
    // Overwritten at execution start by incoming connection values.
    // Index matches NodeDef::ports order.
    PortValue  defaults[MAX_PORTS_PER_NODE];
};

} // namespace ffe
```

#### Connection

```cpp
namespace ffe {

struct Connection {
    NodeId  srcNode;    // Source node ID
    PortId  srcPort;    // Output port index on the source node
    NodeId  dstNode;    // Destination node ID
    PortId  dstPort;    // Input port index on the destination node
};

} // namespace ffe
```

Connections are validated at load time:
- `srcNode` and `dstNode` must exist in the graph's node list.
- `srcPort` must be an OUTPUT port on `srcNode`; `dstPort` must be an INPUT port on `dstNode`.
- Port types must match exactly. Mismatched types cause load failure.
- The connection must not create a cycle (DFS cycle check at load).

#### GraphAsset — Loaded Graph Data

```cpp
namespace ffe {

// Maximum nodes and connections per graph (enforced at load).
static constexpr uint32_t MAX_NODES_PER_GRAPH        = 256;
static constexpr uint32_t MAX_CONNECTIONS_PER_GRAPH  = 512;

// Per-frame node execution cap (enforced at runtime by the executor).
// = MAX_NODES_PER_GRAPH × MAX_GRAPHS_LOADED = 256 × 32.
// Guards against runaway execution in future milestones that open custom node registration.
static constexpr uint32_t MAX_NODE_CALLS_PER_FRAME   = 8192;

// GraphAsset holds the runtime-ready data for one loaded graph.
// Allocated once at loadGraph() time. Freed at unloadGraph().
// All arrays are fixed-size (no std::vector — cold data, not per-frame).
struct GraphAsset {
    NodeInstance  nodes[MAX_NODES_PER_GRAPH];
    uint32_t      nodeCount = 0;

    Connection    connections[MAX_CONNECTIONS_PER_GRAPH];
    uint32_t      connectionCount = 0;

    // Topologically sorted execution order. Each entry is an index into nodes[].
    // Computed once at load by DFS. Invalid (cyclic) graphs are rejected at load.
    uint16_t      execOrder[MAX_NODES_PER_GRAPH];

    // Scratch array for port values during execution: one PortValue per port
    // per node. Layout: node 0 ports, then node 1 ports, etc.
    // Indexed as: scratchBase = portScratchOffset[i], then ports follow in order.
    // This array is written by the executor each frame — not const.
    uint32_t      portScratchOffset[MAX_NODES_PER_GRAPH];  // byte offset into scratch
    uint32_t      totalPortCount = 0;   // sum of portCount across all nodes

    char          sourcePath[512] = {};  // Canonical path this asset was loaded from
};

} // namespace ffe
```

**Size budget:** `GraphAsset` is cold data — one allocation per loaded graph, not per entity. It is sized to be allocated on the heap via `std::unique_ptr` (the pool stores pointers, not inline structs, following the PrefabSystem pattern for large data). Size of a full `GraphAsset`: approximately 256 × (sizeof(NodeInstance) = ~136B) + 512 × (sizeof(Connection) = 8B) + 256 × 2 (execOrder) + 256 × 4 (portScratchOffset) + 4 + 4 + 512 ≈ ~38 KB per graph. At pool capacity 32 graphs this is ~1.2 MB — within the LEGACY 2 MB arena budget if heap-allocated (pool stores raw owning pointers, same pattern as `PrefabSystem::m_pool`).

#### GraphComponent — ECS Component

```cpp
namespace ffe {

// Attached to an entity to bind a loaded graph to it.
// Size: 5 bytes, padded to 8 bytes.
struct GraphComponent {
    GraphHandle handle;    // Which graph asset to execute
    bool        active;    // If false, executor skips this entity
};
static_assert(sizeof(GraphComponent) <= 8);

} // namespace ffe
```

`GraphComponent` is registered in `engine/core/visual_scripting.h` alongside the other ECS components. It is a plain data component — no constructor, no destructor.

---

### 2.3 Built-In Node Library (M2)

All 11 nodes are defined as static `NodeDef` entries in `visual_scripting.cpp`. `NodeTypeId` values 1–11 are reserved for these built-ins. 0 is invalid. Values 12+ are reserved for future user-defined nodes (out of scope for M2).

Notation: `→` = output port. `←` = input port. Types: F=FLOAT, B=BOOL, E=ENTITY, V2=VEC2, V3=VEC3.

#### Event Nodes (fire once per trigger condition)

| TypeId | Name | Inputs | Outputs | Behaviour |
|--------|------|--------|---------|-----------|
| 1 | `OnUpdate` | _(none)_ | `→ dt: F` | Fires every frame. Outputs delta time in seconds. Entry point for per-frame logic. |
| 2 | `OnCollision` | _(none)_ | `→ other: E` | Fires when the owning entity's `Collider2D` receives a collision event. Outputs the other entity's ID. Reads `CollisionEventList` from ECS context. |
| 3 | `OnKeyPress` | `← key: F` | `→ fired: B` | Outputs `true` on the frame the specified key (integer key code, stored as float) is pressed. Calls `Input::isKeyPressed`. |

Event nodes act as sources in the topological sort. They produce values but do not consume them from upstream connections. `OnUpdate` always fires. `OnCollision` fires only if a collision event exists for the entity this frame. `OnKeyPress` fires only if the key was pressed this frame. If an event node does not fire, its downstream subgraph is skipped (outputs remain at default values).

**Reachability requirement:** All nodes in a valid graph must be reachable from at least one event node via execution-flow connections. Graphs containing unreachable (isolated) nodes are rejected at load time (see Section 2.5 step 12b). This rule ensures every node in the graph executes only when triggered by an event.

**Skip propagation:** Each node has a `fired` flag in the per-frame scratch. If a node's primary control-flow input (e.g., `fired: B` from `OnKeyPress`) is `false`, the node is skipped and its outputs are set to defaults. Downstream nodes that depend only on the skipped node's outputs also skip. This is evaluated in topological order — each node checks its upstream `fired` state before executing.

#### Action Nodes (write to ECS or engine state)

| TypeId | Name | Inputs | Outputs | Behaviour |
|--------|------|--------|---------|-----------|
| 4 | `SetVelocity` | `← entity: E`, `← velocity: V2` | _(none)_ | Calls a velocity-setting helper on the entity's `Transform` component (2D). No-op if entity has no Transform. |
| 5 | `SetPosition` | `← entity: E`, `← position: V3` | _(none)_ | Writes `Transform3D.x/y/z` (3D) or `Transform.x/y` (2D) depending on which component the entity has. Checks 3D first. |
| 6 | `GetPosition` | `← entity: E` | `→ position: V3` | Reads `Transform3D.x/y/z` from the entity. Falls back to `Transform.x/y/0` if 3D not present. |
| 7 | `BranchIf` | `← condition: B`, `← ifTrue: F`, `← ifFalse: F` | `→ result: F` | Returns `ifTrue` if `condition` is true, else `ifFalse`. Pure data node — always executes (no skip propagation on condition). |
| 8 | `Add` | `← a: F`, `← b: F` | `→ result: F` | Returns `a + b`. Pure arithmetic. |
| 9 | `Multiply` | `← a: F`, `← b: F` | `→ result: F` | Returns `a * b`. Pure arithmetic. |
| 10 | `PlaySound` | `← soundId: F` | _(none)_ | Calls `Audio::playSound((uint32_t)soundId)`. No-op if soundId == 0 or audio system not initialized. |
| 11 | `DestroyEntity` | `← entity: E` | _(none)_ | Calls `world.destroyEntity(target)`. To prevent use-after-free during the executor's entity iteration loop, destruction is deferred: the executor accumulates pending destroy requests in a fixed-size buffer (`pendingDestroys[MAX_NODES_PER_GRAPH]`) and flushes them after the entity iteration loop completes. The destroyed entity's `GraphComponent` active flag is set to false immediately so the graph does not continue executing in the same frame. No-op if entity == NULL_ENTITY or already destroyed. |

**Port count summary:** No node exceeds 3 inputs + 1 output = 4 ports total, well under `MAX_PORTS_PER_NODE = 8`. The limit of 8 provides headroom for more complex nodes in future milestones.

---

### 2.4 Executor Model

The executor is the hot path. It runs every frame for every entity with an active `GraphComponent`. It must allocate nothing on the heap.

#### Topological Sort (Load Time)

At `loadGraph()` time, after all nodes and connections are validated, the executor performs a DFS topological sort over the node graph:

```
function topoSort(nodes, connections) -> execOrder[], or CYCLE_DETECTED:
  state[] = UNVISITED for each node
  order = []
  for each node n:
    if state[n] == UNVISITED:
      dfs(n)
  return reverse(order)

function dfs(n):
  if state[n] == IN_STACK: CYCLE_DETECTED — reject graph
  if state[n] == DONE: return
  state[n] = IN_STACK
  for each connection c where c.dstNode == n:
    dfs(c.srcNode)
  state[n] = DONE
  order.append(n)
```

DFS state uses a `uint8_t` array of size `nodeCount` on the stack (max 256 bytes). If a cycle is detected, `loadGraph()` returns `GraphHandle{0}` with a logged error. Cyclic graphs are never stored in the pool.

`execOrder[]` is stored in `GraphAsset` as a `uint16_t` array (node indices, 0-based). This ordering is computed once and reused every frame. No sort at runtime.

#### Per-Frame Execution

```
nodeCallsThisFrame = 0   // uint32, reset to 0 at the start of each execute() call

for each entity E with GraphComponent (active == true):
  asset = pool[component.handle.id]
  scratch = frame_arena.allocate(asset.totalPortCount * sizeof(PortValue))

  // Initialise scratch from node defaults
  for each node i in execOrder:
    def = getNodeDef(nodes[i].typeId)
    base = &scratch[asset.portScratchOffset[i]]
    memcpy(base, nodes[i].defaults, def.portCount * sizeof(PortValue))

  // Propagate connections (write output values into downstream input slots)
  for each connection c:
    src_val = scratch[portScratchOffset[c.srcNode] + asset.outputPortOffset(c.srcNode, c.srcPort)]
    scratch[portScratchOffset[c.dstNode] + c.dstPort] = src_val

  // Execute in topological order
  for each node index idx in execOrder:
    if nodeCallsThisFrame >= MAX_NODE_CALLS_PER_FRAME:
      log_warning("VisualScriptingSystem: MAX_NODE_CALLS_PER_FRAME exceeded; skipping remaining graphs this frame")
      goto end_frame   // skip all remaining graphs for this frame
    nodeCallsThisFrame++
    def = getNodeDef(nodes[idx].typeId)
    base = &scratch[asset.portScratchOffset[idx]]
    ctx = {
      inputs:      base,
      outputs:     base + def.inputCount,
      inputCount:  def.inputCount,
      outputCount: def.outputCount,
      entity:      E,
      dt:          frameDt,
      world:       &world,
    }
    def.execute(ctx)   // function pointer call — no virtual dispatch

end_frame:
// Flush deferred destroy requests (see DestroyEntity node, Section 2.3)
for each entity in pendingDestroys:
  world.destroyEntity(entity)
pendingDestroys.clear()
```

**Scratch allocation:** `scratch` is drawn from the per-frame arena allocator (`ffe::ArenaAllocator`). The arena is reset at the start of each frame. `PortValue` is 16 bytes; a 256-node graph with an average of 4 ports each uses 256 × 4 × 16 = 16 KB of scratch. Well within the LEGACY arena budget.

**Connection propagation ordering:** The propagation pass (copying output values into downstream input slots) runs in connection-list order before the execution pass. This is correct because `execOrder` guarantees every producer runs before its consumer — so by the time a consumer node executes, its input slots already contain the latest output from its producers written in the same frame.

**Deferred destroy flush:** After the entity iteration loop completes, the executor flushes `pendingDestroys`: for each entity in the buffer, it calls `world.destroyEntity(entity)`. This ensures no mid-iteration invalidation of ECS entity lists. The buffer is fixed-size (`pendingDestroys[MAX_NODES_PER_GRAPH]` — at most one destroy request per node per graph), so no heap allocation is needed.

**Per-frame node-execution cap:** The executor maintains a frame-level counter `nodeCallsThisFrame` (uint32, reset to 0 at the start of each `VisualScriptingSystem::execute()` call). Each time a node's execute function is called, the counter is incremented. If `nodeCallsThisFrame >= MAX_NODE_CALLS_PER_FRAME` (8192), the executor logs a warning and skips remaining graphs for the frame. Although M2's 11 built-in nodes are all O(1) C++ function pointers and cannot produce an infinite loop, M3 will open custom node registration; this cap establishes the guardrail before M3 implementers need it.

**Performance target:** A 20-node graph with 30 connections on LEGACY tier hardware must complete execution in under 0.1 ms. Estimated budget: 20 function pointer calls + 30 `PortValue` copies + scratch memcpy = O(a few hundred instructions). This is comfortably within budget even on a single-core LEGACY CPU at 1 GHz.

**No virtual dispatch:** `def.execute` is a plain function pointer (`NodeExecuteFn`). The node type is resolved once from `typeId` via a flat array lookup, not a virtual table.

**No per-frame heap allocation:** All scratch memory comes from the arena. Node arrays, connection arrays, and `execOrder` are pre-allocated at load time in `GraphAsset`.

---

### 2.5 JSON Serialisation Format

Graph assets are stored as `.json` files. File extension is `.json`. Files live under the game's asset directory.

#### Schema

```json
{
  "version": 1,
  "nodes": [
    {
      "id": 1,
      "type": "OnUpdate",
      "editorX": 100.0,
      "editorY": 200.0,
      "defaults": {}
    },
    {
      "id": 2,
      "type": "Add",
      "editorX": 300.0,
      "editorY": 200.0,
      "defaults": {
        "a": 0.0,
        "b": 1.0
      }
    },
    {
      "id": 3,
      "type": "SetVelocity",
      "editorX": 500.0,
      "editorY": 200.0,
      "defaults": {
        "entity": 0
      }
    }
  ],
  "connections": [
    { "srcNode": 1, "srcPort": 0, "dstNode": 2, "dstPort": 0 },
    { "srcNode": 2, "srcPort": 0, "dstNode": 3, "dstPort": 1 }
  ]
}
```

#### Field Rules

- `"version"` — required integer. M2 only accepts version 1. Unknown versions → reject with log error.
- `"nodes"` — required array. If absent or not an array → reject.
- Each node object requires `"id"` (integer ≥ 1) and `"type"` (string). `"editorX"` / `"editorY"` are optional (default 0.0); used only by the editor canvas.
- `"defaults"` — optional object. Keys are port names (matching `PortDef::name`). Values are numbers (for FLOAT, ENTITY, VEC2.x etc.) or booleans. Unknown keys are skipped.
- `"connections"` — required array (may be empty). Each entry requires all four fields.

#### Load-Time Validation Pipeline (Security)

All validation occurs before any `GraphAsset` is stored in the pool. Validation order:

**Allocation ordering guarantee:** All structural validation steps (1–13) complete before any `GraphAsset` memory in the pool is written. The `GraphAsset` arrays (`nodes`, `connections`, `execOrder`) are filled only after the full validation pipeline passes. No partial writes occur on validation failure.

1. **Path canonicalization:** `canonicalizePath(path, buf, sizeof(buf))`. Reject on failure.
2. **UNC pre-check (Windows):** If path starts with `\\`, reject immediately before canonicalization.
3. **Asset-root boundary check:** Canonical path must begin with `m_assetRoot` + path separator. Reject if not.
4. **File size check:** `stat()` the canonical path. If size > `MAX_GRAPH_FILE_SIZE` (512 KB), reject. (Graph files are simpler than prefab files; 512 KB is generous for any hand-authored graph.)
5. **JSON parse with depth limit:** `nlohmann::json::parse(input, nullptr, false, false, 8)`. Reject if parse fails or depth limit exceeded.
6. **Version check:** `"version"` must be 1.
7. **Node count check:** `nodes.size()` must be ≤ `MAX_NODES_PER_GRAPH` (256). Reject if exceeded.
8. **Node ID uniqueness:** All `"id"` values must be distinct integers in [1, 65535]. Reject duplicates.
9. **Node type whitelist:** Each `"type"` string must match one of the 11 built-in type names (`"OnUpdate"`, `"OnCollision"`, ..., `"DestroyEntity"`). Unknown types → reject immediately (not skip). This is stricter than the prefab system's forward-compat skip policy, because unknown node types would silently produce incorrect runtime behaviour.
10. **Connection count check:** `connections.size()` must be ≤ `MAX_CONNECTIONS_PER_GRAPH` (512).
11. **Connection validity:** For each connection, `srcNode` and `dstNode` must reference existing node IDs. `srcPort` must index an OUTPUT port on `srcNode`; `dstPort` must index an INPUT port on `dstNode`. Port types must match exactly. Reject any invalid connection.
12. **Cycle detection:** DFS over the directed graph. Reject if a cycle is found.
12b. **Reachability validation:** From each event node (`OnUpdate`, `OnCollision`, `OnKeyPress` — nodes with no execution-flow input), perform a forward DFS traversal over execution-flow connections. Any node not reachable from at least one event node is "isolated." Graphs containing isolated nodes are rejected at load time with a logged error identifying the unreachable node IDs. This prevents CPU waste from permanently-active nodes and gives authors immediate feedback when a subgraph is accidentally disconnected.
13. **Topological sort:** DFS to produce `execOrder`. Stored in `GraphAsset`.

All rejections return `GraphHandle{0}` and log the failure reason. No partial graph is stored.

#### File Size Limit

| Limit | Value | Rationale |
|-------|-------|-----------|
| Max file size | 512 KB | Hand-authored graph files are typically < 50 KB. 512 KB prevents memory exhaustion without being restrictive. |
| Max nodes | 256 | Keeps `GraphAsset` fixed-size. 256 nodes is sufficient for complex visual scripts. |
| Max connections | 512 | ~2 connections per node on average. Generous headroom. |
| Max nesting depth | 8 | JSON nesting; the schema has at most 4 levels in practice. |

---

### 2.6 VisualScriptingSystem — Public API

```cpp
namespace ffe {

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
class VisualScriptingSystem {
public:
    VisualScriptingSystem();
    ~VisualScriptingSystem();

    // Non-copyable, non-movable.
    VisualScriptingSystem(const VisualScriptingSystem&) = delete;
    VisualScriptingSystem& operator=(const VisualScriptingSystem&) = delete;

    // Set the asset root for path validation. Must be called before loadGraph().
    // Identical contract to PrefabSystem::setAssetRoot().
    void setAssetRoot(std::string_view root);

    // --- Cold path ---

    // Load a graph asset from a JSON file at `path`.
    // Validates path, parses JSON, validates nodes/connections, detects cycles,
    // computes topological sort. Stores result in pool.
    // Returns GraphHandle{0} on any error; logs the reason.
    GraphHandle loadGraph(std::string_view path);

    // Unload a loaded graph. Entities with GraphComponent referencing this
    // handle will have their execute() skipped from the next frame onward
    // (executor checks handle validity before executing).
    // Passing GraphHandle{0} or an unloaded handle is a no-op.
    void unloadGraph(GraphHandle handle);

    // Number of currently loaded graphs.
    int getGraphCount() const;

    // --- Per-frame hot path ---

    // Execute all active GraphComponents in `world`.
    // Called once per frame by Application (or by ScriptEngine::tick()).
    // Reads GraphComponent from all entities; executes active graphs in
    // topological order using per-frame arena scratch.
    // No heap allocation. Target: 20-node graph < 0.1 ms on LEGACY.
    void execute(World& world, float dt, ArenaAllocator& frameArena);

    // --- Editor helpers (compiled in always; editor reads graph structure) ---

    // Return a pointer to the GraphAsset for inspection (editor canvas, tests).
    // Returns nullptr if handle is invalid or unloaded.
    const GraphAsset* getGraphAsset(GraphHandle handle) const;

    // Return the NodeDef for a given typeId. Returns nullptr if typeId is
    // out of range or not registered.
    static const NodeDef* getNodeDef(NodeTypeId typeId);

    // Return the total number of registered built-in node types.
    static int getNodeTypeCount();

private:
    static constexpr int MAX_GRAPHS = 32;   // Slot 0 reserved.

    // Heap-allocated graph assets. Null when slot is unoccupied.
    // Matches PrefabSystem's m_pool pointer-per-slot pattern.
    GraphAsset* m_pool[MAX_GRAPHS] = {};
    bool        m_occupied[MAX_GRAPHS] = {};
    int         m_count = 0;
    char        m_assetRoot[512] = {};
};

} // namespace ffe
```

**Handle-to-slot mapping:** `handle.id` is the direct slot index (`pool[handle.id]`). Valid IDs are in [1, MAX_GRAPHS - 1]. Bounds-checked in all methods: `if (handle.id == 0 || handle.id >= MAX_GRAPHS || !m_occupied[handle.id])`.

**Pool size rationale:** 32 graphs is sufficient for M2 (most games have 1–5 distinct graph scripts). If more are needed, `MAX_GRAPHS` can be raised — the pool is pointers, so the cost is 32 additional null pointers.

**Slot allocation:** Linear scan from slot 1 on `loadGraph`. First unoccupied slot wins.

**Integration with ScriptEngine:** `VisualScriptingSystem m_visualScripting` is added as a private member of `ScriptEngine`, alongside `m_prefabSystem`, `m_vegetationSystem`, and `m_waterManager`. `setAssetRoot` is called lazily from the `ffe.loadGraph` Lua binding using `m_assetRoot`, identically to `m_prefabSystem.setAssetRoot`.

**Integration with Application:** `Application` calls `m_scriptEngine.executeGraphs(world, dt)` in its per-frame tick, after physics and before rendering. `ScriptEngine::executeGraphs` delegates to `m_visualScripting.execute(world, dt, m_frameArena)`.

---

### 2.7 Lua Bindings

Three new bindings are added to the `ffe.*` namespace. Implementation in `engine/scripting/script_engine.cpp`.

```lua
-- Load a graph asset from disk (cold path — call at startup or scene load).
-- Returns a graph handle (integer > 0), or 0 on failure.
local handle = ffe.loadGraph("assets/graphs/player_move.json")

-- Attach a loaded graph to an entity. Adds GraphComponent to the entity.
-- If the entity already has a GraphComponent, it is replaced.
-- No-op if handle == 0 or entity == ffe.NULL_ENTITY.
ffe.attachGraph(entityId, handle)

-- Detach and remove the GraphComponent from an entity.
-- No-op if the entity has no GraphComponent.
ffe.detachGraph(entityId)
```

**C++ binding signatures (in script_engine.cpp):**

```cpp
// ffe.loadGraph(path: string) -> integer
static int lua_loadGraph(lua_State* L) {
    // 1. Check arg count and type.
    // 2. Extract path string.
    // 3. Call m_visualScripting.loadGraph(path).
    // 4. Push handle.id as lua_Integer. Push 0 on failure.
    // Returns 1.
}

// ffe.attachGraph(entityId: integer, handle: integer) -> (none)
static int lua_attachGraph(lua_State* L) {
    // 1. Extract entityId and handle.id.
    // 2. Validate handle: bounds check + occupied check.
    // 3. world->addComponent<GraphComponent>(entity, {handle, true}).
    //    (Or world->emplace_or_replace if already present.)
    // Returns 0.
}

// ffe.detachGraph(entityId: integer) -> (none)
static int lua_detachGraph(lua_State* L) {
    // 1. Extract entityId.
    // 2. world->removeComponent<GraphComponent>(entity) if present.
    // Returns 0.
}
```

**Coexistence with Lua scripts:** A single entity can have both a `GraphComponent` and a Lua script function (e.g., `update_player`). The executor runs graph execution first, then Lua `callFunction` runs. There is no conflict — both can read and write ECS components. If both write `Transform.x` in the same frame, the last writer wins (Lua, since it runs after graphs). This ordering is intentional: graphs handle simple data flow; Lua overrides take precedence.

**Handle validation in bindings:** `ffe.attachGraph` validates the handle before calling into C++: `if (id == 0 || id >= MAX_GRAPHS) return error`. This prevents invalid handles from reaching `addComponent`.

---

### 2.8 Editor Integration — GraphEditorPanel

The graph editor panel is a new ImGui panel docked in the existing editor dockspace. It is only compiled when `FFE_EDITOR` is defined. Owned by `renderer-specialist`.

```cpp
// engine/editor/graph_editor_panel.h
#pragma once
#ifdef FFE_EDITOR

#include "core/visual_scripting.h"

namespace ffe::editor {

// GraphEditorPanel renders an interactive node canvas for inspecting
// and laying out a loaded graph asset.
//
// In play mode (isPlaying == true): the panel is read-only (no drag,
// no add/delete). Node execution highlights are drawn if a graph is active.
//
// In edit mode: nodes can be dragged to new positions. Connections are
// drawn as bezier curves. Right-click adds a new node (from the built-in
// node type list). No graph edits are persisted to disk in M2 — the
// canvas is a viewer/layout tool. Save-to-disk is deferred to M3.
//
// Owner: renderer-specialist (ImGui code; no engine logic).
class GraphEditorPanel {
public:
    GraphEditorPanel();
    ~GraphEditorPanel();

    // Set which graph to display. Pass GraphHandle{0} to show empty canvas.
    void setActiveGraph(GraphHandle handle, VisualScriptingSystem* vs);

    // Render the panel into the current ImGui dockspace. Call once per frame.
    // `isPlaying` controls read-only mode.
    void render(bool isPlaying);

    bool isVisible() const;
    void setVisible(bool visible);

private:
    // --- Canvas state ---
    GraphHandle            m_activeHandle;
    const GraphAsset*      m_asset = nullptr;   // Non-owning view into VSS pool
    VisualScriptingSystem* m_vs    = nullptr;   // Non-owning pointer

    // Per-node editor positions (not stored in GraphAsset — editor-only).
    // Loaded from editorX/editorY in the JSON at setActiveGraph() time.
    // Max 256 to match MAX_NODES_PER_GRAPH.
    struct NodeLayout {
        float x = 0.0f;
        float y = 0.0f;
    };
    NodeLayout m_layouts[256] = {};

    // Canvas pan and zoom
    float m_panX  = 0.0f;
    float m_panY  = 0.0f;
    float m_zoom  = 1.0f;

    bool  m_visible = true;

    // --- Rendering helpers ---
    void drawGrid(ImDrawList* dl, ImVec2 origin);
    void drawNode(ImDrawList* dl, uint32_t nodeIdx, ImVec2 origin);
    void drawConnections(ImDrawList* dl, ImVec2 origin);
    void drawAddNodeMenu();

    // Screen-space port position for a given node + port index.
    ImVec2 portScreenPos(uint32_t nodeIdx, uint8_t portIdx, ImVec2 origin) const;
};

} // namespace ffe::editor
#endif // FFE_EDITOR
```

**Docking:** `GraphEditorPanel` is added to `EditorOverlay::render()` as a new docked window, alongside the existing hierarchy, inspector, and console panels.

**Bezier connections:** Drawn via `ImDrawList::AddBezierCubic`. Control points are offset horizontally from the source and destination port positions by a factor of zoom × 80px.

**Pan/zoom:** Left-drag on canvas background pans. Scroll wheel zooms in/out (clamped to [0.1, 4.0]). Node positions are stored in canvas space; screen-space positions are computed as `(canvasPos + pan) * zoom + windowOrigin`.

**Node drag:** In edit mode only. Left-drag on a node header updates `m_layouts[nodeIdx]`. The updated positions are not written back to the JSON file (save deferred to M3).

**Read-only in play mode:** When `isPlaying == true`, drag and right-click are disabled. The panel renders node highlights (a coloured border) on nodes that executed this frame — this requires a per-node execution flag set by the executor, stored as a bitfield in `GraphAsset` during execution (or as a frame-counter integer per node). Implementation detail left to `renderer-specialist`.

**Right-click add-node menu:** Shows all 11 built-in node type names. Selecting one appends a `NodeInstance` to a local staging buffer (not the live `GraphAsset` — staging is editor-side only in M2). Because save-to-disk is deferred, this is primarily a UX preview for M3.

---

### 2.9 Tier Support

| Tier | Supported | Notes |
|------|-----------|-------|
| RETRO (OpenGL 2.1) | Yes | No GPU dependency in graph executor. |
| LEGACY (OpenGL 3.3) | Yes — primary target | Default development tier. Performance target: 20-node graph < 0.1 ms. |
| STANDARD (OpenGL 4.5 / Vulkan) | Yes | No changes needed. |
| MODERN (Vulkan) | Yes | No changes needed. |

The graph executor is pure CPU code. The editor canvas uses ImGui (CPU-side drawing commands submitted to the GPU as triangles — no custom shaders, no RHI calls).

**VRAM budget:** Zero. `VisualScriptingSystem` holds only CPU data. `GraphAsset` pools (~38 KB per asset × 32 slots = ~1.2 MB) are heap-allocated, not GPU-uploaded.

---

### 2.10 Security Threat Model

This section is written for `security-auditor`'s shift-left review.

#### Threat 1: Path Traversal via `ffe.loadGraph`

**Attack:** `ffe.loadGraph("../../etc/passwd")` or `ffe.loadGraph("/etc/shadow")` from a Lua script or game code.

**Mitigation:** Identical pipeline to `PrefabSystem::loadPrefab`:
1. Pre-check for UNC paths (`\\` prefix on Windows) before any canonicalization — reject immediately.
2. `canonicalizePath(path, buf, sizeof(buf))` via `engine/core/platform.h`. POSIX uses `realpath`; Windows uses `_fullpath`. Resolves all `..` and symlinks.
3. Asset-root prefix check: canonical path must start with `m_assetRoot` followed by a path separator (not merely start with the string — prevents `/game/assets-evil/` bypassing a root of `/game/assets`).
4. Only if all checks pass does `loadGraph` open the file.

#### Threat 2: File Size Bomb

**Attack:** A multi-gigabyte `.json` file causing memory exhaustion before parsing.

**Mitigation:**
1. `stat()` the canonical path before opening. If size > 512 KB, reject immediately.
2. Pipeline order: canonicalize → asset-root check → stat + size check → open + read. The file is never opened before size is verified.

#### Threat 3: JSON Depth Attack

**Attack:** A deeply nested JSON file causing stack overflow in the recursive parser.

**Mitigation:** `nlohmann::json::parse(input, nullptr, false, false, 8)` — depth limit of 8. Same approach as `PrefabSystem`. If the vendored nlohmann version does not support the fifth parameter, implement a post-parse depth counter.

#### Threat 4: Unknown Node Type Injection

**Attack:** A crafted JSON with a `"type"` value not in the built-in whitelist (e.g., `"type": "EXEC_SHELLCODE"` or a future node type not yet implemented in M2).

**Mitigation:** Unknown `"type"` strings → **reject the entire graph** at load time, not skip. This is stricter than the PrefabSystem's forward-compat skip policy because an unknown node type would silently produce a no-op gap in execution flow, making graph behaviour unpredictable. A developer who loads an M3-authored graph on an M2 build gets a clear error, not silent misbehaviour.

**Whitelist:** The 11 type names are compared with `strcmp` against a static array. No hash, no dynamic lookup, no hash-collision surface.

#### Threat 5: Cycle-Based Denial of Service

**Attack:** A crafted graph with a cycle between nodes. A naive executor that follows connections without cycle detection would loop forever.

**Mitigation:** DFS cycle detection at load time (see Section 2.4). If a back-edge is found (`IN_STACK` state encountered), the graph is rejected immediately. Cyclic graphs are never stored in the pool. The executor never encounters a cycle at runtime.

**DFS stack depth:** The DFS call stack grows to at most `nodeCount` depth = 256 levels. On any modern platform, 256 stack frames is negligible (each frame uses ~O(10) bytes of stack).

#### Threat 6: Port Count / Connection Out-of-Bounds

**Attack:** A JSON connection with `srcPort` or `dstPort` values that exceed the actual port count of the referenced node type.

**Mitigation:** During connection validation (step 11 of the load pipeline), each `srcPort` and `dstPort` is bounds-checked against `NodeDef::portCount`. `srcPort` must be < `outputCount`; `dstPort` must be < `inputCount`. Out-of-bounds → reject graph.

#### Threat 7: Handle Validation in Lua Bindings

**Attack:** `ffe.attachGraph(entityId, 999)` — a crafted handle ID that is out of the pool bounds.

**Mitigation:** `lua_attachGraph` checks: `if (id == 0 || id >= MAX_GRAPHS || !m_occupied[id]) { log_error; return 0; }`. The bounds check prevents any out-of-bounds array access. `NULL_ENTITY` is checked for `entityId`.

#### Threat 8: Port Type Mismatch at Runtime

**Attack:** A connection in the JSON that passes a `BOOL` output to a `FLOAT` input. If not caught at load, the executor's `PortValue` copy could be interpreted with the wrong union member.

**Mitigation:** Connection validation (step 11) checks `srcNode`'s output port type == `dstNode`'s input port type. Type mismatch → reject graph at load.

#### Threat 9: Max Node / Connection Count as Resource Exhaustion

**Attack:** A JSON with `nodes.size() = 1000000`, causing `GraphAsset` allocation to fail or exhaust memory.

**Mitigation:** Step 7 checks `nodes.size() <= 256` before any allocation. Step 10 checks `connections.size() <= 512`. Both checks happen immediately after JSON parse, before any `GraphAsset` memory is touched.

---

### 2.11 Relation to Lua Scripting

Visual scripting and Lua scripting are parallel, independent systems. They share the ECS world but have no coupling between them.

| Dimension | Lua Scripting | Visual Scripting |
|-----------|--------------|-----------------|
| Language | LuaJIT | Node graph JSON |
| Author | Programmer | Beginner / designer |
| Execution | `ScriptEngine::callFunction` | `VisualScriptingSystem::execute` |
| Component | No dedicated component (script name is registered globally) | `GraphComponent` |
| Per-entity | One Lua function per entity (by convention) | One `GraphHandle` per entity |
| Coexistence | Yes — can run on same entity as a graph | Yes — runs after graph execution |
| New dependency | None | None (`nlohmann/json` already vendored) |

---

### 2.12 Test Plan

`tests/core/test_visual_scripting.cpp` must cover the following. This list is the minimum; additional edge cases are encouraged.

| # | Category | Test Description |
|---|----------|-----------------|
| 1 | Happy path | Load a valid 1-node `OnUpdate` graph; verify `GraphHandle` is valid. |
| 2 | Happy path | Load a 3-node graph (`OnUpdate → Add → SetVelocity`); verify `getGraphCount() == 1`. |
| 3 | Happy path | Execute a graph on an entity; verify `SetVelocity` node writes expected value to Transform. |
| 4 | Happy path | `BranchIf` node: condition=true selects ifTrue branch; condition=false selects ifFalse branch. |
| 5 | Happy path | `Add` node: a=3.0, b=4.0 → result=7.0. |
| 6 | Happy path | `Multiply` node: a=2.0, b=5.0 → result=10.0. |
| 7 | Happy path | `GetPosition` node reads `Transform3D` from entity and outputs VEC3. |
| 8 | Happy path | `SetPosition` node writes `Transform3D.x/y/z` on entity. |
| 9 | Happy path | `attachGraph` attaches `GraphComponent` to entity; `detachGraph` removes it; entity no longer executes. |
| 10 | Happy path | `unloadGraph` frees pool slot; `getGraphCount()` decrements; subsequent `execute()` skips entity. |
| 11 | Happy path | Load 31 graphs (fill all valid slots); verify `getGraphCount() == 31`. |
| 12 | Happy path | `OnKeyPress` node: mock key pressed → `fired` output is true. |
| 13 | Happy path | `DestroyEntity` node: entity is removed from world after graph execution. |
| 14 | Limits | Load a 32nd graph when pool is full; verify `GraphHandle{0}` returned. |
| 15 | Limits | Graph with exactly 256 nodes; verify load succeeds and `execOrder` has 256 entries. |
| 16 | Limits | Graph with 257 nodes; verify load fails with `GraphHandle{0}`. |
| 17 | Limits | Graph with exactly 512 connections; verify load succeeds. |
| 18 | Limits | Graph with 513 connections; verify load fails. |
| 19 | Limits | Two entities both with `GraphComponent` for same graph handle; verify both execute independently. |
| 20 | Invalid input | `loadGraph` on non-existent file; verify `GraphHandle{0}`, no crash. |
| 21 | Invalid input | `loadGraph` on a 600 KB file; verify rejected before parse. |
| 22 | Invalid input | JSON missing `"nodes"` key; verify `GraphHandle{0}`. |
| 23 | Invalid input | JSON with `"version": 2`; verify rejected. |
| 24 | Invalid input | Node with unknown `"type": "FutureNode"`; verify graph is rejected (not just node skipped). |
| 25 | Invalid input | Connection with `srcPort` out of bounds for source node; verify rejected. |
| 26 | Invalid input | Connection with mismatched port types (FLOAT → BOOL); verify rejected. |
| 27 | Invalid input | `attachGraph` with `GraphHandle{0}`; verify no crash, no component added. |
| 28 | Invalid input | `attachGraph` with out-of-bounds handle id (e.g., 999); verify rejected, no crash. |
| 29 | Security | `loadGraph("../../etc/passwd")`; verify path traversal rejected, `GraphHandle{0}`. |
| 30 | Security | `loadGraph` with path outside asset root after canonicalization; verify rejected. |
| 31 | Security | Graph with a cycle (`A → B → A`); verify cycle detected, `GraphHandle{0}`. |
| 32 | Security | Graph with self-loop (`A → A`); verify cycle detected. |
| 33 | Security | JSON with 9-level nesting depth; verify rejected. |

---

## 3. Consequences

### Positive

- **Lowers the floor for beginners.** Non-programmers can wire `OnUpdate → Add → SetVelocity` without writing a single line of Lua. This directly serves FFE's mission of making game development accessible.
- **Coexists with Lua.** Experienced developers can use Lua for complex logic and visual scripting for data flow pipelines. Neither system interferes with the other.
- **Zero per-frame heap allocation.** The executor is allocation-free: scratch memory is arena-drawn, node definitions are static, connection propagation is a flat loop. Suitable for LEGACY tier.
- **No new vcpkg dependency.** Uses `nlohmann/json` (already vendored) and `engine/core/platform.h`. The instruction budget sandbox from the Lua layer does not apply to graph execution (graphs execute native C++ function pointers — no Lua state involved).
- **Consistent handle pattern.** `GraphHandle` follows the same `uint32_t` opaque handle pattern as `PrefabHandle`, `MeshHandle`, `TerrainHandle`. Lua developers find the API immediately familiar.
- **Hardened load pipeline.** All security checks are at load time, not runtime. A graph that passes loading is safe to execute.

### Tradeoffs

- **Node type whitelist is strict (reject unknown, not skip).** Forward compatibility is sacrificed: a graph authored for M3 (with user-defined nodes) will not load on an M2 build. This is the right choice for safety — a silent no-op node would produce wrong game behaviour with no diagnostic. A clear error message is better.
- **No save-to-disk from editor canvas.** M2's `GraphEditorPanel` is a viewer and layout tool. Developers who want to create new graphs must hand-author JSON. Save-to-disk is M3 work.
- **32-graph pool.** Sufficient for all current FFE use cases. Raisable by changing `MAX_GRAPHS` — the cost is 32 additional null pointers.
- **Port types are strict.** FLOAT cannot implicitly coerce to BOOL at connection time. This avoids type confusion bugs at the cost of needing explicit `BranchIf` nodes for conversions.
- **GraphAsset is ~38 KB per graph.** At 32 slots, ~1.2 MB of heap is committed when all slots are full. Within LEGACY budget but non-trivial. Mitigation: the pool stores pointers, so only occupied slots consume 38 KB each.

---

## 4. Open Questions for PM Review

### 4.1 Arena Allocator Availability in execute()

`VisualScriptingSystem::execute` requires a per-frame arena for scratch `PortValue` arrays. The current `Application` loop has a `m_frameArena` (or equivalent). The integration must verify that the arena is accessible at the call site (`ScriptEngine::executeGraphs`). If the arena is not directly accessible from `ScriptEngine`, the alternative is to use a fixed-size static scratch buffer inside `VisualScriptingSystem` (e.g., 64 KB, enough for ~256 nodes × 16 ports × 16 bytes). PM should confirm which approach to use.

**Recommendation:** Static scratch buffer inside `VisualScriptingSystem` avoids the dependency on arena availability while remaining allocation-free. It is simpler to implement. The 64 KB overhead is acceptable on LEGACY tier.

### 4.2 Execution Ordering Relative to Lua

~~The current design runs graph execution before `ScriptEngine::callFunction` so Lua can override graph outputs. If a game uses both systems on the same entity, this is the safest default. PM should confirm this ordering is correct for the use cases planned in M2 demos.~~

**RESOLVED (security-auditor finding 2):** All nodes in a valid graph must be reachable from at least one event node. Isolated subgraphs are rejected at load time (Section 2.5 step 12b). This resolves the concern about accidentally-disconnected subgraphs consuming CPU silently. Execution ordering relative to Lua (graphs first, then Lua) is confirmed as the correct default for M2.

### 4.3 OnCollision Node — Event Delivery Mechanism

~~`OnCollision` reads `CollisionEventList` from the ECS context. The collision system populates this list once per physics tick. If graph execution runs at render rate (not physics rate), `OnCollision` may fire multiple times for a single collision event, or miss events. PM should clarify whether graph execution should run at fixed-tick rate (alongside physics) or render rate. The safest answer for M2 is **fixed-tick rate** — same as `ScriptEngine::callFunction` — which makes `OnCollision` correct.~~

**RESOLVED (security-auditor finding 8):** The `DestroyEntity` node now uses a deferred destroy buffer (Section 2.3, Section 2.4) to prevent use-after-free during the entity iteration loop. The destroyed entity's `GraphComponent` active flag is cleared immediately; `world.destroyEntity` is flushed after the iteration loop completes. `OnCollision` runs at fixed-tick rate (same as `ScriptEngine::callFunction`) — confirmed correct for M2.

### 4.4 `SetVelocity` Node — 2D Only or 2D+3D?

The current design targets 2D (`Transform` component). If M2 demos will use 3D entities (common given the 3D showcase game), `SetVelocity` may need a `Transform3D` variant or a separate `SetVelocity3D` node. PM should clarify. The simplest M2 resolution: `SetVelocity` writes to `Transform.x/y` (2D velocity); add `SetVelocity3D` as node 12 only if the M2 demo requires it. This keeps the 11-node list clean without over-specifying.

### 4.5 GraphEditorPanel — Integration Point in Editor

`EditorOverlay::render()` currently calls `drawPerformancePanel`, `drawEntityInspector`, and `drawConsolePanel`. The `GraphEditorPanel` needs to be instantiated somewhere in `EditorOverlay` or alongside it. The simplest approach: add `GraphEditorPanel m_graphPanel` as a member of `EditorOverlay` and call `m_graphPanel.render(isPlaying)` inside `EditorOverlay::render()`. PM should confirm this pattern or specify an alternative if the editor architecture has changed since Phase 3.

---

## 5. Implementation Agent Dispatch Notes

This section is for PM's reference when writing the Phase 5 dispatch plan.

**Phase 2 — Implementation split:**

- **Foundation (sequential, engine-dev):** Write `engine/core/visual_scripting.h` first. All types (`GraphHandle`, `PortType`, `PortValue`, `NodeDef`, `NodeInstance`, `Connection`, `GraphAsset`, `GraphComponent`, `VisualScriptingSystem` class declaration) must be in the header before parallel work begins. Define all 11 `NodeDef` entries in `visual_scripting.cpp`.

- **Workers (parallel after foundation):**
  - **engine-dev worker A:** `engine/core/visual_scripting.cpp` (executor: load pipeline, DFS sort, per-frame execute loop, node implementations for all 11 nodes), `engine/scripting/script_engine.h` (add member + 3 binding declarations), `engine/scripting/script_engine.cpp` (add `ffe.loadGraph`, `ffe.attachGraph`, `ffe.detachGraph`), `tests/core/test_visual_scripting.cpp` (33 tests), `engine/core/.context.md` (Visual Scripting section).
  - **renderer-specialist:** `engine/editor/graph_editor_panel.h`, `engine/editor/graph_editor_panel.cpp`, integration into `EditorOverlay`.

**Phase 3 — Expert panel (parallel):** `performance-critic` + `security-auditor` + `api-designer` (updates `engine/scripting/.context.md` for 3 new bindings).

**Phase 5 — Build:** FAST (Clang-18 only). FULL (Clang-18 + GCC-13) at PM's discretion if compiler portability is a concern for new template code.
