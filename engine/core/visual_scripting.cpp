// visual_scripting.cpp — Node-based visual scripting runtime for FFE.
//
// See visual_scripting.h for the public API and usage documentation.
// See docs/architecture/adr-phase10-m2-visual-scripting.md for the design
// rationale and the full security threat model.
//
// File ownership: engine/core/ (engine-dev)

#include "core/visual_scripting.h"
#include "core/logging.h"
#include "core/platform.h"
#include "core/input.h"
#include "renderer/render_system.h"  // Transform, Transform3D
#include "physics/collider2d.h"      // CollisionEventList (struct only, no link needed)

#include <nlohmann/json.hpp>

#include <climits>     // PATH_MAX
#include <cstring>     // memcpy, strcmp, strncpy
#include <cstdio>      // snprintf
#include <fstream>
#include <new>         // std::nothrow
#include <string>
#include <vector>      // depth-check stack (cold path only)
#include <sys/stat.h>  // stat()

using json = nlohmann::json;

namespace ffe {

// ---------------------------------------------------------------------------
// Static occupancy table — mirrors VisualScriptingSystem::m_occupied.
// Consulted by the free function isValid(GraphHandle).
// ---------------------------------------------------------------------------

bool VisualScriptingSystem::s_occupied[MAX_GRAPHS_LOADED] = {};

bool isValid(const GraphHandle h) {
    if (h.id == 0 || h.id >= MAX_GRAPHS_LOADED) {
        return false;
    }
    return VisualScriptingSystem::s_occupied[h.id];
}

// ---------------------------------------------------------------------------
// Built-in node execute functions (static, no virtual dispatch)
// ---------------------------------------------------------------------------

// Node 1: OnUpdate — fires every frame, outputs delta time.
static void execOnUpdate(const NodeExecContext& ctx) {
    // Output 0: dt (FLOAT)
    if (ctx.outputCount > 0) {
        ctx.outputs[0].type      = PortType::FLOAT;
        ctx.outputs[0].data.f    = ctx.dt;
    }
}

// Node 2: OnCollision — fires if this entity has a collision event this frame.
// Outputs the other entity's ID. If no collision, outputs NULL_ENTITY.
static void execOnCollision(const NodeExecContext& ctx) {
    if (ctx.outputCount == 0 || ctx.world == nullptr) return;

    ctx.outputs[0].type         = PortType::ENTITY;
    ctx.outputs[0].data.entity  = NULL_ENTITY;

    // Read CollisionEventList from ECS context.
    auto& reg = ctx.world->registry();
    const auto* evtList = reg.ctx().find<CollisionEventList>();
    if (evtList == nullptr || evtList->count == 0 || evtList->events == nullptr) return;

    for (uint32_t i = 0; i < evtList->count; ++i) {
        const CollisionEvent& ev = evtList->events[i];
        if (ev.entityA == ctx.entity) {
            ctx.outputs[0].data.entity = ev.entityB;
            return;
        }
        if (ev.entityB == ctx.entity) {
            ctx.outputs[0].data.entity = ev.entityA;
            return;
        }
    }
}

// Node 3: OnKeyPress — fires if the key (stored as float key code) is pressed.
// Input 0: key code (FLOAT — integer GLFW key cast to float).
// Output 0: fired (BOOL).
static void execOnKeyPress(const NodeExecContext& ctx) {
    if (ctx.outputCount > 0) {
        ctx.outputs[0].type   = PortType::BOOL;
        ctx.outputs[0].data.b = false;
    }

    if (ctx.inputCount > 0) {
        const int keyCode = static_cast<int>(ctx.inputs[0].data.f);
        // Bounds-check against valid GLFW key range.
        if (keyCode >= 0 && keyCode < MAX_KEYS) {
            const bool pressed = ffe::isKeyPressed(static_cast<Key>(keyCode));
            if (ctx.outputCount > 0) {
                ctx.outputs[0].data.b = pressed;
            }
        }
    }
}

// Node 4: SetVelocity — writes velocity into Transform2D (x/y).
// Input 0: entity (ENTITY). Input 1: velocity (VEC2).
static void execSetVelocity(const NodeExecContext& ctx) {
    if (ctx.inputCount < 2 || ctx.world == nullptr) return;

    const EntityId target = ctx.inputs[0].data.entity;
    if (target == NULL_ENTITY || !ctx.world->isValid(target)) return;

    // Write velocity as Transform.position x/y delta (scaled by dt would be wrong;
    // per ADR SetVelocity writes the velocity vector to Transform.x/y directly).
    if (ctx.world->hasComponent<Transform>(target)) {
        Transform& t = ctx.world->getComponent<Transform>(target);
        t.position.x = ctx.inputs[1].data.vec2.x;
        t.position.y = ctx.inputs[1].data.vec2.y;
    }
}

// Node 5: SetPosition — writes Transform3D.x/y/z (3D first, then 2D fallback).
// Input 0: entity (ENTITY). Input 1: position (VEC3).
static void execSetPosition(const NodeExecContext& ctx) {
    if (ctx.inputCount < 2 || ctx.world == nullptr) return;

    const EntityId target = ctx.inputs[0].data.entity;
    if (target == NULL_ENTITY || !ctx.world->isValid(target)) return;

    const float px = ctx.inputs[1].data.vec3.x;
    const float py = ctx.inputs[1].data.vec3.y;
    const float pz = ctx.inputs[1].data.vec3.z;

    if (ctx.world->hasComponent<Transform3D>(target)) {
        Transform3D& t = ctx.world->getComponent<Transform3D>(target);
        t.position.x = px;
        t.position.y = py;
        t.position.z = pz;
    } else if (ctx.world->hasComponent<Transform>(target)) {
        Transform& t = ctx.world->getComponent<Transform>(target);
        t.position.x = px;
        t.position.y = py;
    }
}

// Node 6: GetPosition — reads Transform3D.x/y/z (3D first, 2D fallback).
// Input 0: entity (ENTITY). Output 0: position (VEC3).
static void execGetPosition(const NodeExecContext& ctx) {
    if (ctx.outputCount > 0) {
        ctx.outputs[0].type         = PortType::VEC3;
        ctx.outputs[0].data.vec3    = {0.0f, 0.0f, 0.0f};
    }

    if (ctx.inputCount == 0 || ctx.world == nullptr) return;

    const EntityId target = ctx.inputs[0].data.entity;
    if (target == NULL_ENTITY || !ctx.world->isValid(target)) return;

    if (ctx.world->hasComponent<Transform3D>(target)) {
        const Transform3D& t = ctx.world->getComponent<Transform3D>(target);
        if (ctx.outputCount > 0) {
            ctx.outputs[0].data.vec3 = {t.position.x, t.position.y, t.position.z};
        }
    } else if (ctx.world->hasComponent<Transform>(target)) {
        const Transform& t = ctx.world->getComponent<Transform>(target);
        if (ctx.outputCount > 0) {
            ctx.outputs[0].data.vec3 = {t.position.x, t.position.y, 0.0f};
        }
    }
}

// Node 7: BranchIf — selects ifTrue or ifFalse based on condition.
// Input 0: condition (BOOL). Input 1: ifTrue (FLOAT). Input 2: ifFalse (FLOAT).
// Output 0: result (FLOAT).
static void execBranchIf(const NodeExecContext& ctx) {
    if (ctx.outputCount == 0) return;

    ctx.outputs[0].type = PortType::FLOAT;
    ctx.outputs[0].data.f = 0.0f;

    if (ctx.inputCount < 3) return;

    const bool condition = ctx.inputs[0].data.b;
    ctx.outputs[0].data.f = condition ? ctx.inputs[1].data.f : ctx.inputs[2].data.f;
}

// Node 8: Add — returns a + b.
// Input 0: a (FLOAT). Input 1: b (FLOAT). Output 0: result (FLOAT).
static void execAdd(const NodeExecContext& ctx) {
    if (ctx.outputCount == 0) return;

    ctx.outputs[0].type   = PortType::FLOAT;
    ctx.outputs[0].data.f = 0.0f;

    if (ctx.inputCount >= 2) {
        ctx.outputs[0].data.f = ctx.inputs[0].data.f + ctx.inputs[1].data.f;
    }
}

// Node 9: Multiply — returns a * b.
// Input 0: a (FLOAT). Input 1: b (FLOAT). Output 0: result (FLOAT).
static void execMultiply(const NodeExecContext& ctx) {
    if (ctx.outputCount == 0) return;

    ctx.outputs[0].type   = PortType::FLOAT;
    ctx.outputs[0].data.f = 0.0f;

    if (ctx.inputCount >= 2) {
        ctx.outputs[0].data.f = ctx.inputs[0].data.f * ctx.inputs[1].data.f;
    }
}

// Node 10: PlaySound — calls through a registered function pointer to avoid
// a circular dependency (ffe_audio links ffe_core; ffe_core cannot link ffe_audio).
// The audio callback is registered by ScriptEngine via
// VisualScriptingSystem::registerPlaySoundFn() after init.
// If no callback is registered, this node is a no-op.
using PlaySoundFn = void (*)(uint32_t soundId);
static PlaySoundFn s_playSoundFn = nullptr;

static void execPlaySound(const NodeExecContext& ctx) {
    if (ctx.inputCount == 0 || s_playSoundFn == nullptr) return;

    const uint32_t soundId = static_cast<uint32_t>(ctx.inputs[0].data.f);
    if (soundId == 0) return;

    s_playSoundFn(soundId);
}

// Node 11: DestroyEntity — deferred entity destruction.
// Input 0: entity (ENTITY).
// Adds to the per-frame pending destroy buffer; flushed after iteration.
// The GraphComponent active flag is set false immediately.
// NOTE: The destroy is actually accumulated in the system's m_pendingDestroys
// array. Since execDestroyEntity only has NodeExecContext, we encode the entity
// ID in a thread-local/global scratch that the executor reads.
// The executor passes world + entity, so we can defer via a separate mechanism.
// Per the ADR: "the executor accumulates pending destroy requests in a fixed-size
// buffer". We write the entity into outputs[0] with type ENTITY as a signal,
// using a sentinel typeId=11 that the executor reads after calling execute.
// Simpler approach: use a separate accumulator array in VisualScriptingSystem
// that is written from execute(), not from the node function itself.
// For that to work from a static function, we encode the request as output[0].
static void execDestroyEntity(const NodeExecContext& ctx) {
    if (ctx.inputCount == 0) return;

    const EntityId target = ctx.inputs[0].data.entity;
    if (target == NULL_ENTITY || ctx.world == nullptr) return;
    if (!ctx.world->isValid(target)) return;

    // Signal the executor by writing a special output.
    // The executor checks outputs[0] after calling this node (typeId 11).
    // We have no output ports on DestroyEntity, so we use a different mechanism:
    // write into a special "sentinel" slot in ctx.outputs if outputCount > 0.
    // Since the ADR says the executor flushes pendingDestroys after iteration,
    // and we need the accumulator to be in VisualScriptingSystem, we pass the
    // entity to destroy via the outputs slot [0] that we temporarily repurpose.
    // The executor, when running a DestroyEntity node (typeId==11), reads
    // outputs[0] as the entity to defer-destroy.
    // We use a secret output slot: since MAX_PORTS_PER_NODE=8 and DestroyEntity
    // has 0 declared outputs, we write index [0] in the output region (which is
    // base + inputCount offset in the scratch array).
    // Actually, ctx.outputs already points past inputs (it is base + inputCount).
    // We write index 0 of outputs to signal the entity:
    ctx.outputs[0].type         = PortType::ENTITY;
    ctx.outputs[0].data.entity  = target;

    // Immediately deactivate the entity's own GraphComponent if it is this entity.
    if (target == ctx.entity && ctx.world->hasComponent<GraphComponent>(target)) {
        ctx.world->getComponent<GraphComponent>(target).active = false;
    }
}

// ---------------------------------------------------------------------------
// Node registry — 11 built-in NodeDef entries.
// TypeId values 1-11 reserved. 0 = invalid. 12+ = future user-defined.
// ---------------------------------------------------------------------------

static const NodeDef s_nodeRegistry[] = {
    // TypeId 1: OnUpdate
    {
        1, "OnUpdate", execOnUpdate,
        {
            // No inputs. Output 0: dt (FLOAT).
            { PortDir::OUTPUT, PortType::FLOAT, "dt" },
            {}, {}, {}, {}, {}, {}, {}
        },
        1, 0, 1, NodeCategory::EVENT
    },
    // TypeId 2: OnCollision
    {
        2, "OnCollision", execOnCollision,
        {
            // No inputs. Output 0: other (ENTITY).
            { PortDir::OUTPUT, PortType::ENTITY, "other" },
            {}, {}, {}, {}, {}, {}, {}
        },
        1, 0, 1, NodeCategory::EVENT
    },
    // TypeId 3: OnKeyPress
    {
        3, "OnKeyPress", execOnKeyPress,
        {
            // Input 0: key (FLOAT — key code). Output 0: fired (BOOL).
            { PortDir::INPUT,  PortType::FLOAT, "key"   },
            { PortDir::OUTPUT, PortType::BOOL,  "fired" },
            {}, {}, {}, {}, {}, {}
        },
        2, 1, 1, NodeCategory::EVENT
    },
    // TypeId 4: SetVelocity
    {
        4, "SetVelocity", execSetVelocity,
        {
            // Input 0: entity (ENTITY). Input 1: velocity (VEC2). No outputs.
            { PortDir::INPUT, PortType::ENTITY, "entity"   },
            { PortDir::INPUT, PortType::VEC2,   "velocity" },
            {}, {}, {}, {}, {}, {}
        },
        2, 2, 0, NodeCategory::ACTION
    },
    // TypeId 5: SetPosition
    {
        5, "SetPosition", execSetPosition,
        {
            // Input 0: entity (ENTITY). Input 1: position (VEC3). No outputs.
            { PortDir::INPUT, PortType::ENTITY, "entity"   },
            { PortDir::INPUT, PortType::VEC3,   "position" },
            {}, {}, {}, {}, {}, {}
        },
        2, 2, 0, NodeCategory::ACTION
    },
    // TypeId 6: GetPosition
    {
        6, "GetPosition", execGetPosition,
        {
            // Input 0: entity (ENTITY). Output 0: position (VEC3).
            { PortDir::INPUT,  PortType::ENTITY, "entity"   },
            { PortDir::OUTPUT, PortType::VEC3,   "position" },
            {}, {}, {}, {}, {}, {}
        },
        2, 1, 1, NodeCategory::DATA
    },
    // TypeId 7: BranchIf
    {
        7, "BranchIf", execBranchIf,
        {
            // Input 0: condition (BOOL). Input 1: ifTrue (FLOAT). Input 2: ifFalse (FLOAT).
            // Output 0: result (FLOAT).
            { PortDir::INPUT,  PortType::BOOL,  "condition" },
            { PortDir::INPUT,  PortType::FLOAT, "ifTrue"    },
            { PortDir::INPUT,  PortType::FLOAT, "ifFalse"   },
            { PortDir::OUTPUT, PortType::FLOAT, "result"    },
            {}, {}, {}, {}
        },
        4, 3, 1, NodeCategory::FLOW
    },
    // TypeId 8: Add
    {
        8, "Add", execAdd,
        {
            // Input 0: a (FLOAT). Input 1: b (FLOAT). Output 0: result (FLOAT).
            { PortDir::INPUT,  PortType::FLOAT, "a"      },
            { PortDir::INPUT,  PortType::FLOAT, "b"      },
            { PortDir::OUTPUT, PortType::FLOAT, "result" },
            {}, {}, {}, {}, {}
        },
        3, 2, 1, NodeCategory::DATA
    },
    // TypeId 9: Multiply
    {
        9, "Multiply", execMultiply,
        {
            // Input 0: a (FLOAT). Input 1: b (FLOAT). Output 0: result (FLOAT).
            { PortDir::INPUT,  PortType::FLOAT, "a"      },
            { PortDir::INPUT,  PortType::FLOAT, "b"      },
            { PortDir::OUTPUT, PortType::FLOAT, "result" },
            {}, {}, {}, {}, {}
        },
        3, 2, 1, NodeCategory::DATA
    },
    // TypeId 10: PlaySound
    {
        10, "PlaySound", execPlaySound,
        {
            // Input 0: soundId (FLOAT — cast to uint32_t). No outputs.
            { PortDir::INPUT, PortType::FLOAT, "soundId" },
            {}, {}, {}, {}, {}, {}, {}
        },
        1, 1, 0, NodeCategory::ACTION
    },
    // TypeId 11: DestroyEntity
    // Special: executor reads outputs[0] to get entity to defer-destroy.
    // We declare 1 output (hidden, ENTITY type) so the executor has space.
    {
        11, "DestroyEntity", execDestroyEntity,
        {
            // Input 0: entity (ENTITY). Output 0: [internal — deferred entity].
            { PortDir::INPUT,  PortType::ENTITY, "entity"    },
            { PortDir::OUTPUT, PortType::ENTITY, "_deferred" },
            {}, {}, {}, {}, {}, {}
        },
        2, 1, 1, NodeCategory::ACTION
    },
};

static constexpr int NODE_REGISTRY_COUNT =
    static_cast<int>(sizeof(s_nodeRegistry) / sizeof(s_nodeRegistry[0]));

// ---------------------------------------------------------------------------
// VisualScriptingSystem — static helpers
// ---------------------------------------------------------------------------

static constexpr std::size_t MAX_GRAPH_FILE_SIZE = 512u * 1024u;  // 512 KB
static constexpr int MAX_JSON_DEPTH = 8;

// Find a NodeDef by typeName (strcmp). Returns nullptr if not found.
static const NodeDef* findNodeDefByName(const char* typeName) {
    for (int i = 0; i < NODE_REGISTRY_COUNT; ++i) {
        if (std::strcmp(s_nodeRegistry[i].typeName, typeName) == 0) {
            return &s_nodeRegistry[i];
        }
    }
    return nullptr;
}

// Find a NodeDef by typeId. Returns nullptr if out of range.
static const NodeDef* findNodeDefById(NodeTypeId typeId) {
    for (int i = 0; i < NODE_REGISTRY_COUNT; ++i) {
        if (s_nodeRegistry[i].typeId == typeId) {
            return &s_nodeRegistry[i];
        }
    }
    return nullptr;
}

// Max-depth check on a parsed JSON document (iterative, cold path).
static bool jsonExceedsDepth(const json& doc, const int maxDepth) {
    struct Frame { const json* node; int depth; };
    std::vector<Frame> stack;
    stack.reserve(64);
    stack.push_back({&doc, 1});

    while (!stack.empty()) {
        const Frame f = stack.back();
        stack.pop_back();

        if (f.depth > maxDepth) return true;

        if (f.node->is_object()) {
            for (const auto& [key, val] : f.node->items()) {
                (void)key;
                if (val.is_object() || val.is_array()) {
                    stack.push_back({&val, f.depth + 1});
                }
            }
        } else if (f.node->is_array()) {
            for (const auto& elem : *f.node) {
                if (elem.is_object() || elem.is_array()) {
                    stack.push_back({&elem, f.depth + 1});
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// VisualScriptingSystem — constructor / destructor
// ---------------------------------------------------------------------------

VisualScriptingSystem::VisualScriptingSystem() {
    // Zero-initialise pool and scratch arrays (already value-initialised via
    // member initialisation in the header, but be explicit).
    for (int i = 0; i < MAX_GRAPHS; ++i) {
        m_pool[i]     = nullptr;
        m_occupied[i] = false;
    }
}

VisualScriptingSystem::~VisualScriptingSystem() {
    for (int i = 0; i < MAX_GRAPHS; ++i) {
        if (m_occupied[i] && m_pool[i] != nullptr) {
            delete m_pool[i];
            m_pool[i]     = nullptr;
            m_occupied[i] = false;
            s_occupied[i] = false;
        }
    }
}

// ---------------------------------------------------------------------------
// setAssetRoot
// ---------------------------------------------------------------------------

void VisualScriptingSystem::setAssetRoot(std::string_view root) {
    if (root.empty()) {
        m_assetRoot[0] = '\0';
        return;
    }

    const std::size_t copyLen =
        root.size() < sizeof(m_assetRoot) - 1u ? root.size() : sizeof(m_assetRoot) - 1u;
    std::memcpy(m_assetRoot, root.data(), copyLen);
    m_assetRoot[copyLen] = '\0';

    // Strip trailing slashes — asset root check appends sep+path.
    std::size_t len = std::strlen(m_assetRoot);
    while (len > 0 && (m_assetRoot[len - 1] == '/' || m_assetRoot[len - 1] == '\\')) {
        m_assetRoot[--len] = '\0';
    }
}

// ---------------------------------------------------------------------------
// loadGraph — full security-hardened pipeline per ADR Section 2.5
// ---------------------------------------------------------------------------

GraphHandle VisualScriptingSystem::loadGraph(std::string_view path) {
    if (path.empty()) {
        FFE_LOG_ERROR("VisualScripting", "loadGraph: path is empty");
        return GraphHandle{0};
    }

    // Build null-terminated path buffer.
    char pathBuf[PATH_MAX + 1];
    const std::size_t copyLen =
        path.size() < PATH_MAX ? path.size() : PATH_MAX;
    std::memcpy(pathBuf, path.data(), copyLen);
    pathBuf[copyLen] = '\0';

    // --- Step 2: UNC pre-check (Windows + cross-platform // rejection) ---
    // Reject paths starting with \\ or // before any canonicalization.
    if ((pathBuf[0] == '\\' && pathBuf[1] == '\\') ||
        (pathBuf[0] == '/' && pathBuf[1] == '/')) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: UNC/network paths are not allowed: %s", pathBuf);
        return GraphHandle{0};
    }

    // --- Step 1: canonicalizePath ---
    char canonBuf[PATH_MAX + 1];
    if (!canonicalizePath(pathBuf, canonBuf, sizeof(canonBuf))) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: canonicalizePath failed for: %s", pathBuf);
        return GraphHandle{0};
    }
    const std::string canonPath(canonBuf);

    // --- Step 3: asset-root boundary check ---
    if (m_assetRoot[0] != '\0') {
        const std::string root(m_assetRoot);
        bool withinRoot = false;

        if (canonPath == root) {
            withinRoot = true;  // exact match (will fail stat+parse, but allowed here)
        } else if (canonPath.size() > root.size()) {
            const char sep = canonPath[root.size()];
            withinRoot = (canonPath.compare(0, root.size(), root) == 0)
                      && (sep == '/'
#ifdef _WIN32
                          || sep == '\\'
#endif
                         );
        }

        if (!withinRoot) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: path traversal rejected — '%s' is outside asset root '%s'",
                          canonBuf, m_assetRoot);
            return GraphHandle{0};
        }
    }

    // --- Step 4: file size check (stat before open) ---
    struct stat st{};
    if (::stat(canonBuf, &st) != 0) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: file does not exist or stat() failed: %s", canonBuf);
        return GraphHandle{0};
    }
    if (static_cast<std::size_t>(st.st_size) > MAX_GRAPH_FILE_SIZE) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: file too large (%lld bytes, limit %zu): %s",
                      static_cast<long long>(st.st_size), MAX_GRAPH_FILE_SIZE, canonBuf);
        return GraphHandle{0};
    }

    // --- Step 5: open and read ---
    std::ifstream ifs(canonBuf, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: failed to open file: %s", canonBuf);
        return GraphHandle{0};
    }
    std::string text;
    text.reserve(static_cast<std::size_t>(st.st_size) + 1u);
    text.assign(std::istreambuf_iterator<char>(ifs),
                std::istreambuf_iterator<char>());
    ifs.close();

    // --- Step 5b: JSON parse ---
    json doc = json::parse(text, nullptr, /*allow_exceptions=*/false,
                           /*ignore_comments=*/false);
    if (doc.is_discarded()) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: JSON parse failed: %s", canonBuf);
        return GraphHandle{0};
    }

    // --- Step 5c: depth check ---
    if (jsonExceedsDepth(doc, MAX_JSON_DEPTH)) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: JSON nesting depth exceeds %d: %s",
                      MAX_JSON_DEPTH, canonBuf);
        return GraphHandle{0};
    }

    // --- Step 6: version check ---
    if (!doc.contains("version") || !doc["version"].is_number_integer()) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: missing or invalid 'version' field: %s", canonBuf);
        return GraphHandle{0};
    }
    if (doc["version"].get<int>() != 1) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: unsupported version %d (expected 1): %s",
                      doc["version"].get<int>(), canonBuf);
        return GraphHandle{0};
    }

    // --- Step 6b: nodes array presence ---
    if (!doc.contains("nodes") || !doc["nodes"].is_array()) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: missing or invalid 'nodes' array: %s", canonBuf);
        return GraphHandle{0};
    }
    const json& nodesJson = doc["nodes"];

    // --- Step 7: node count check ---
    if (nodesJson.size() > MAX_NODES_PER_GRAPH) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: too many nodes (%zu > %u): %s",
                      nodesJson.size(), MAX_NODES_PER_GRAPH, canonBuf);
        return GraphHandle{0};
    }

    const uint32_t nodeCount = static_cast<uint32_t>(nodesJson.size());

    // --- Step 8: node ID uniqueness + node type whitelist (Step 9) ---
    // Build temporary working arrays for validation (cold path — stack/heap ok here).
    struct TempNode {
        NodeId     id;
        NodeTypeId typeId;
    };
    // Stack-allocate for up to MAX_NODES_PER_GRAPH nodes.
    TempNode tempNodes[MAX_NODES_PER_GRAPH];
    std::memset(tempNodes, 0, sizeof(tempNodes));

    // For ID uniqueness check, use a 65536-bit flag array.
    // 65536 / 8 = 8192 bytes = 8 KB on stack — acceptable for cold path.
    uint8_t idSeen[8192];
    std::memset(idSeen, 0, sizeof(idSeen));

    for (uint32_t ni = 0; ni < nodeCount; ++ni) {
        const json& nodeJson = nodesJson[ni];

        if (!nodeJson.contains("id") || !nodeJson["id"].is_number_integer()) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: node[%u] missing or invalid 'id': %s",
                          ni, canonBuf);
            return GraphHandle{0};
        }
        if (!nodeJson.contains("type") || !nodeJson["type"].is_string()) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: node[%u] missing or invalid 'type': %s",
                          ni, canonBuf);
            return GraphHandle{0};
        }

        const int rawId = nodeJson["id"].get<int>();
        if (rawId < 1 || rawId > 65535) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: node[%u] id %d out of range [1,65535]: %s",
                          ni, rawId, canonBuf);
            return GraphHandle{0};
        }
        const NodeId nodeId = static_cast<NodeId>(rawId);

        // Uniqueness check
        const uint32_t byteIdx = nodeId / 8u;
        const uint8_t  bitMask = static_cast<uint8_t>(1u << (nodeId % 8u));
        if (idSeen[byteIdx] & bitMask) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: duplicate node id %u: %s",
                          nodeId, canonBuf);
            return GraphHandle{0};
        }
        idSeen[byteIdx] |= bitMask;

        // Type whitelist (Step 9)
        const std::string& typeName = nodeJson["type"].get_ref<const std::string&>();
        const NodeDef* def = findNodeDefByName(typeName.c_str());
        if (def == nullptr) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: unknown node type '%s' — rejected: %s",
                          typeName.c_str(), canonBuf);
            return GraphHandle{0};
        }

        tempNodes[ni].id     = nodeId;
        tempNodes[ni].typeId = def->typeId;
    }

    // --- Steps 10-11: connections validation ---
    if (!doc.contains("connections") || !doc["connections"].is_array()) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: missing or invalid 'connections' array: %s", canonBuf);
        return GraphHandle{0};
    }
    const json& connJson = doc["connections"];

    if (connJson.size() > MAX_CONNECTIONS_PER_GRAPH) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: too many connections (%zu > %u): %s",
                      connJson.size(), MAX_CONNECTIONS_PER_GRAPH, canonBuf);
        return GraphHandle{0};
    }

    const uint32_t connCount = static_cast<uint32_t>(connJson.size());

    struct TempConn {
        NodeId  srcNode;
        PortId  srcPort;
        NodeId  dstNode;
        PortId  dstPort;
        uint32_t srcNodeIdx;  // index into tempNodes[]
        uint32_t dstNodeIdx;
    };
    TempConn tempConns[MAX_CONNECTIONS_PER_GRAPH];
    std::memset(tempConns, 0, sizeof(tempConns));

    // Helper: find index of a node by id within tempNodes.
    auto findNodeIdx = [&](const NodeId id, uint32_t& outIdx) -> bool {
        for (uint32_t i = 0; i < nodeCount; ++i) {
            if (tempNodes[i].id == id) {
                outIdx = i;
                return true;
            }
        }
        return false;
    };

    for (uint32_t ci = 0; ci < connCount; ++ci) {
        const json& c = connJson[ci];

        if (!c.contains("srcNode") || !c["srcNode"].is_number_integer() ||
            !c.contains("srcPort") || !c["srcPort"].is_number_integer() ||
            !c.contains("dstNode") || !c["dstNode"].is_number_integer() ||
            !c.contains("dstPort") || !c["dstPort"].is_number_integer()) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: connection[%u] missing required fields: %s",
                          ci, canonBuf);
            return GraphHandle{0};
        }

        const int rawSrcNode = c["srcNode"].get<int>();
        const int rawSrcPort = c["srcPort"].get<int>();
        const int rawDstNode = c["dstNode"].get<int>();
        const int rawDstPort = c["dstPort"].get<int>();

        if (rawSrcNode < 1 || rawSrcNode > 65535 ||
            rawDstNode < 1 || rawDstNode > 65535 ||
            rawSrcPort < 0 || rawSrcPort > 255  ||
            rawDstPort < 0 || rawDstPort > 255) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: connection[%u] fields out of range: %s",
                          ci, canonBuf);
            return GraphHandle{0};
        }

        const NodeId srcNodeId = static_cast<NodeId>(rawSrcNode);
        const NodeId dstNodeId = static_cast<NodeId>(rawDstNode);
        const PortId srcPort   = static_cast<PortId>(rawSrcPort);
        const PortId dstPort   = static_cast<PortId>(rawDstPort);

        uint32_t srcIdx = 0;
        uint32_t dstIdx = 0;
        if (!findNodeIdx(srcNodeId, srcIdx)) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: connection[%u] srcNode %u not found: %s",
                          ci, srcNodeId, canonBuf);
            return GraphHandle{0};
        }
        if (!findNodeIdx(dstNodeId, dstIdx)) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: connection[%u] dstNode %u not found: %s",
                          ci, dstNodeId, canonBuf);
            return GraphHandle{0};
        }

        // Validate srcPort is an OUTPUT port on srcNode.
        const NodeDef* srcDef = findNodeDefById(tempNodes[srcIdx].typeId);
        const NodeDef* dstDef = findNodeDefById(tempNodes[dstIdx].typeId);
        if (srcDef == nullptr || dstDef == nullptr) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: connection[%u] invalid node typeId: %s",
                          ci, canonBuf);
            return GraphHandle{0};
        }

        // srcPort is an index within the OUTPUT ports (0 = first output).
        // Absolute port index = inputCount + srcPort.
        if (srcPort >= srcDef->outputCount) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: connection[%u] srcPort %u out of bounds "
                          "(outputCount=%u) for node '%s': %s",
                          ci, static_cast<unsigned>(srcPort),
                          static_cast<unsigned>(srcDef->outputCount),
                          srcDef->typeName, canonBuf);
            return GraphHandle{0};
        }

        // dstPort is an index within the INPUT ports (0 = first input).
        if (dstPort >= dstDef->inputCount) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: connection[%u] dstPort %u out of bounds "
                          "(inputCount=%u) for node '%s': %s",
                          ci, static_cast<unsigned>(dstPort),
                          static_cast<unsigned>(dstDef->inputCount),
                          dstDef->typeName, canonBuf);
            return GraphHandle{0};
        }

        // Port type match: source OUTPUT port type must equal destination INPUT port type.
        const PortType srcType = srcDef->ports[srcDef->inputCount + srcPort].type;
        const PortType dstType = dstDef->ports[dstPort].type;
        if (srcType != dstType) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: connection[%u] port type mismatch "
                          "(src type=%u, dst type=%u): %s",
                          ci,
                          static_cast<unsigned>(srcType),
                          static_cast<unsigned>(dstType),
                          canonBuf);
            return GraphHandle{0};
        }

        tempConns[ci] = {srcNodeId, srcPort, dstNodeId, dstPort, srcIdx, dstIdx};
    }

    // --- Step 12: cycle detection (DFS) ---
    // Build adjacency: for each connection srcNodeIdx -> dstNodeIdx.
    // DFS states: 0=UNVISITED, 1=IN_STACK, 2=DONE.
    uint8_t visitState[MAX_NODES_PER_GRAPH];
    std::memset(visitState, 0, sizeof(visitState));

    // Build adjacency list (which nodes does node[i] point to?).
    // i.e., srcNodeIdx -> dstNodeIdx for each connection.
    // We use a fixed-size adjacency array: adjNext[edge] and adjHead[node].
    // Simple O(V+E) DFS using fixed stack.
    // The "edge direction" for topo sort: node i has outgoing edges to all
    // nodes that i connects to as a source.
    // For cycle detection: follow srcNodeIdx -> dstNodeIdx per connection.
    // Use a uint8_t dfsStack to track DFS path.

    // DFS call stack (iterative to avoid deep C++ recursion — max 256 levels).
    struct DfsFrame {
        uint32_t nodeIdx;
        uint32_t connScanPos;  // Which connection index we are currently scanning
    };
    DfsFrame dfsStack[MAX_NODES_PER_GRAPH + 1];
    uint32_t dfsTop = 0;

    bool cycleDetected = false;

    for (uint32_t start = 0; start < nodeCount && !cycleDetected; ++start) {
        if (visitState[start] != 0) continue;  // already processed

        dfsTop = 0;
        dfsStack[dfsTop++] = {start, 0};
        visitState[start] = 1;  // IN_STACK

        while (dfsTop > 0 && !cycleDetected) {
            DfsFrame& frame = dfsStack[dfsTop - 1];
            const uint32_t curIdx = frame.nodeIdx;

            // Find next unprocessed outgoing connection from curIdx.
            bool pushed = false;
            while (frame.connScanPos < connCount) {
                const uint32_t ci = frame.connScanPos++;
                if (tempConns[ci].srcNodeIdx != curIdx) continue;

                const uint32_t nextIdx = tempConns[ci].dstNodeIdx;
                if (visitState[nextIdx] == 1) {
                    // Back-edge — cycle detected.
                    cycleDetected = true;
                    break;
                }
                if (visitState[nextIdx] == 0) {
                    visitState[nextIdx] = 1;  // IN_STACK
                    dfsStack[dfsTop++] = {nextIdx, 0};
                    pushed = true;
                    break;
                }
                // visitState == 2 (DONE) — skip
            }

            if (!pushed && !cycleDetected) {
                // All outgoing edges processed — pop and mark DONE.
                visitState[curIdx] = 2;
                --dfsTop;
            }
        }
    }

    if (cycleDetected) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: cycle detected in graph: %s", canonBuf);
        return GraphHandle{0};
    }

    // --- Step 12b: Reachability validation ---
    // Forward DFS from all event nodes (OnUpdate=1, OnCollision=2, OnKeyPress=3).
    // Any node not reachable from at least one event node is rejected.
    uint8_t reachable[MAX_NODES_PER_GRAPH];
    std::memset(reachable, 0, sizeof(reachable));

    // Simple iterative flood-fill from event nodes.
    // Use a small stack (max nodeCount entries).
    uint32_t bfsStack[MAX_NODES_PER_GRAPH];
    uint32_t bfsTop = 0;

    // Seed with all event nodes.
    for (uint32_t ni = 0; ni < nodeCount; ++ni) {
        const NodeTypeId typeId = tempNodes[ni].typeId;
        if (typeId == 1 || typeId == 2 || typeId == 3) {  // OnUpdate, OnCollision, OnKeyPress
            if (!reachable[ni]) {
                reachable[ni] = 1;
                bfsStack[bfsTop++] = ni;
            }
        }
    }

    while (bfsTop > 0) {
        const uint32_t curIdx = bfsStack[--bfsTop];
        // Follow all outgoing connections from curIdx.
        for (uint32_t ci = 0; ci < connCount; ++ci) {
            if (tempConns[ci].srcNodeIdx == curIdx) {
                const uint32_t nextIdx = tempConns[ci].dstNodeIdx;
                if (!reachable[nextIdx]) {
                    reachable[nextIdx] = 1;
                    bfsStack[bfsTop++] = nextIdx;
                }
            }
        }
    }

    // Check all nodes are reachable.
    for (uint32_t ni = 0; ni < nodeCount; ++ni) {
        if (!reachable[ni]) {
            FFE_LOG_ERROR("VisualScripting",
                          "loadGraph: node id %u ('%s') is unreachable from any event node: %s",
                          tempNodes[ni].id,
                          findNodeDefById(tempNodes[ni].typeId)
                              ? findNodeDefById(tempNodes[ni].typeId)->typeName
                              : "?",
                          canonBuf);
            return GraphHandle{0};
        }
    }

    // --- Step 13: Topological sort (DFS, post-order) ---
    // visitState array reused (reset to 0 = UNVISITED).
    std::memset(visitState, 0, sizeof(visitState));

    // We build execOrder in reverse post-order (standard topo sort).
    uint16_t topoOrder[MAX_NODES_PER_GRAPH];
    uint32_t topoCount = 0;

    // Iterative post-order DFS. Push node index AFTER all descendants done.
    std::memset(dfsStack, 0, sizeof(dfsStack));
    dfsTop = 0;

    for (uint32_t start = 0; start < nodeCount; ++start) {
        if (visitState[start] != 0) continue;

        dfsTop = 0;
        dfsStack[dfsTop++] = {start, 0};
        visitState[start] = 1;  // IN_STACK

        while (dfsTop > 0) {
            DfsFrame& frame = dfsStack[dfsTop - 1];
            const uint32_t curIdx = frame.nodeIdx;

            bool pushed = false;
            while (frame.connScanPos < connCount) {
                const uint32_t ci = frame.connScanPos++;
                if (tempConns[ci].srcNodeIdx != curIdx) continue;

                const uint32_t nextIdx = tempConns[ci].dstNodeIdx;
                if (visitState[nextIdx] == 0) {
                    visitState[nextIdx] = 1;
                    dfsStack[dfsTop++] = {nextIdx, 0};
                    pushed = true;
                    break;
                }
            }

            if (!pushed) {
                // Post-order: emit curIdx.
                topoOrder[topoCount++] = static_cast<uint16_t>(curIdx);
                visitState[curIdx] = 2;
                --dfsTop;
            }
        }
    }

    // topoOrder is in reverse topological order (post-order).
    // Reverse it to get the correct execution order (producers before consumers).
    for (uint32_t i = 0; i < topoCount / 2; ++i) {
        const uint16_t tmp = topoOrder[i];
        topoOrder[i] = topoOrder[topoCount - 1 - i];
        topoOrder[topoCount - 1 - i] = tmp;
    }

    // --- Find free pool slot ---
    int slotIdx = -1;
    for (int i = 1; i < MAX_GRAPHS; ++i) {  // start at 1 (slot 0 = null handle)
        if (!m_occupied[i]) {
            slotIdx = i;
            break;
        }
    }
    if (slotIdx < 0) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: graph pool is full (MAX_GRAPHS=%d): %s",
                      MAX_GRAPHS, canonBuf);
        return GraphHandle{0};
    }

    // --- Allocate and fill GraphAsset (only after ALL checks pass) ---
    GraphAsset* asset = new(std::nothrow) GraphAsset();
    if (asset == nullptr) {
        FFE_LOG_ERROR("VisualScripting",
                      "loadGraph: out of memory allocating GraphAsset: %s", canonBuf);
        return GraphHandle{0};
    }

    asset->nodeCount       = nodeCount;
    asset->connectionCount = connCount;
    asset->execOrderCount  = topoCount;

    // Store canonical path.
    const std::size_t pathCopyLen =
        canonPath.size() < sizeof(asset->sourcePath) - 1u
        ? canonPath.size()
        : sizeof(asset->sourcePath) - 1u;
    std::memcpy(asset->sourcePath, canonPath.c_str(), pathCopyLen);
    asset->sourcePath[pathCopyLen] = '\0';

    // Copy execOrder.
    std::memcpy(asset->execOrder, topoOrder, topoCount * sizeof(uint16_t));

    // Fill NodeInstance array and compute portScratchOffsets.
    uint32_t scratchOff = 0;
    for (uint32_t ni = 0; ni < nodeCount; ++ni) {
        const NodeDef* def = findNodeDefById(tempNodes[ni].typeId);
        // def is non-null (validated above).

        NodeInstance& inst = asset->nodes[ni];
        inst.id     = tempNodes[ni].id;
        inst.typeId = tempNodes[ni].typeId;

        // Initialise defaults from JSON "defaults" object.
        for (uint8_t pi = 0; pi < def->portCount; ++pi) {
            inst.defaults[pi].type     = def->ports[pi].type;
            inst.defaults[pi].data     = {};
        }

        // Parse "defaults" for this node if present.
        const json& nodeJsonRef = nodesJson[ni];
        if (nodeJsonRef.contains("defaults") && nodeJsonRef["defaults"].is_object()) {
            const json& defs = nodeJsonRef["defaults"];
            for (uint8_t pi = 0; pi < def->portCount; ++pi) {
                const char* portName = def->ports[pi].name;
                if (!defs.contains(portName)) continue;
                const json& val = defs[portName];

                switch (def->ports[pi].type) {
                    case PortType::FLOAT:
                        if (val.is_number()) {
                            inst.defaults[pi].data.f = val.get<float>();
                        }
                        break;
                    case PortType::BOOL:
                        if (val.is_boolean()) {
                            inst.defaults[pi].data.b = val.get<bool>();
                        } else if (val.is_number()) {
                            inst.defaults[pi].data.b = val.get<int>() != 0;
                        }
                        break;
                    case PortType::ENTITY:
                        if (val.is_number_integer()) {
                            inst.defaults[pi].data.entity =
                                static_cast<uint32_t>(val.get<int>());
                        }
                        break;
                    case PortType::VEC2:
                        // Not handled from simple scalar defaults in M2.
                        break;
                    case PortType::VEC3:
                        // Not handled from simple scalar defaults in M2.
                        break;
                    default:
                        break;
                }
            }
        }

        // Compute portScratchOffset.
        asset->portScratchOffset[ni] = scratchOff;
        scratchOff += static_cast<uint32_t>(def->portCount);
    }
    asset->totalPortCount = scratchOff;

    // Fill Connection array.
    for (uint32_t ci = 0; ci < connCount; ++ci) {
        asset->connections[ci] = {
            tempConns[ci].srcNode,
            tempConns[ci].srcPort,
            tempConns[ci].dstNode,
            tempConns[ci].dstPort
        };
    }

    // Store in pool.
    m_pool[slotIdx]     = asset;
    m_occupied[slotIdx] = true;
    s_occupied[slotIdx] = true;
    ++m_count;

    FFE_LOG_INFO("VisualScripting",
                 "loadGraph: loaded %u nodes, %u connections from '%s' -> slot %d",
                 nodeCount, connCount, canonBuf, slotIdx);

    return GraphHandle{static_cast<uint32_t>(slotIdx)};
}

// ---------------------------------------------------------------------------
// unloadGraph
// ---------------------------------------------------------------------------

void VisualScriptingSystem::unloadGraph(GraphHandle handle) {
    if (handle.id == 0 || handle.id >= static_cast<uint32_t>(MAX_GRAPHS)) return;
    const int idx = static_cast<int>(handle.id);
    if (!m_occupied[idx]) return;

    delete m_pool[idx];
    m_pool[idx]     = nullptr;
    m_occupied[idx] = false;
    s_occupied[idx] = false;
    --m_count;
}

// ---------------------------------------------------------------------------
// getGraphCount
// ---------------------------------------------------------------------------

int VisualScriptingSystem::getGraphCount() const {
    return m_count;
}

// ---------------------------------------------------------------------------
// execute — per-frame hot path
// ---------------------------------------------------------------------------

void VisualScriptingSystem::execute(World& world, const float dt) {
    m_nodeCallsThisFrame  = 0;
    m_pendingDestroyCount = 0;

    bool capHit = false;

    // Iterate all entities with GraphComponent.
    auto graphView = world.view<GraphComponent>();
    for (auto entityHandle : graphView) {
        const EntityId entity = static_cast<EntityId>(entityHandle);
        GraphComponent& gc = world.getComponent<GraphComponent>(entity);

        if (!gc.active) continue;

        const uint32_t handleId = gc.handle.id;
        if (handleId == 0 || handleId >= static_cast<uint32_t>(MAX_GRAPHS)) continue;
        if (!m_occupied[handleId]) continue;

        const GraphAsset* asset = m_pool[handleId];
        if (asset == nullptr) continue;

        // Scratch buffer: m_scratch holds portValues for the current graph.
        // Reused each graph — no heap allocation.
        const uint32_t scratchCount = asset->totalPortCount;
        if (scratchCount > SCRATCH_COUNT) {
            FFE_LOG_WARN("VisualScripting",
                         "execute: graph slot %u totalPortCount %u exceeds scratch "
                         "buffer %u — skipping entity",
                         handleId, scratchCount, SCRATCH_COUNT);
            continue;
        }

        // Initialise scratch from node defaults.
        for (uint32_t ni = 0; ni < asset->nodeCount; ++ni) {
            const NodeInstance& inst = asset->nodes[ni];
            const NodeDef* def = findNodeDefById(inst.typeId);
            if (def == nullptr) continue;

            const uint32_t base = asset->portScratchOffset[ni];
            for (uint8_t pi = 0; pi < def->portCount; ++pi) {
                m_scratch[base + pi] = inst.defaults[pi];
            }
        }

        // Helper: given a nodeId, find its index in asset->nodes[].
        // O(n) but called rarely (once per connection per frame).
        auto findNodeIndex = [&](NodeId id) -> int {
            for (uint32_t ni = 0; ni < asset->nodeCount; ++ni) {
                if (asset->nodes[ni].id == id) return static_cast<int>(ni);
            }
            return -1;
        };

        // Execute nodes in topological order with inline connection propagation.
        // After each producer node executes, copy its output slots to all downstream
        // input slots immediately — guaranteeing same-frame data flow.
        for (uint32_t oi = 0; oi < asset->execOrderCount; ++oi) {
            if (capHit) break;

            if (m_nodeCallsThisFrame >= MAX_NODE_CALLS_PER_FRAME) {
                FFE_LOG_WARN("VisualScripting",
                             "execute: MAX_NODE_CALLS_PER_FRAME (%u) reached — "
                             "skipping remaining graphs this frame",
                             MAX_NODE_CALLS_PER_FRAME);
                capHit = true;
                break;
            }

            const uint32_t nodeArrayIdx = asset->execOrder[oi];
            if (nodeArrayIdx >= asset->nodeCount) continue;

            const NodeInstance& inst = asset->nodes[nodeArrayIdx];
            const NodeDef* def = findNodeDefById(inst.typeId);
            if (def == nullptr) continue;

            const uint32_t base = asset->portScratchOffset[nodeArrayIdx];
            PortValue* inputsPtr  = &m_scratch[base];
            PortValue* outputsPtr = &m_scratch[base + def->inputCount];

            NodeExecContext ctx{};
            ctx.inputs      = inputsPtr;
            ctx.outputs     = outputsPtr;
            ctx.inputCount  = def->inputCount;
            ctx.outputCount = def->outputCount;
            ctx.entity      = entity;
            ctx.dt          = dt;
            ctx.world       = &world;

            ++m_nodeCallsThisFrame;
            def->execute(ctx);

            // After calling a DestroyEntity node (typeId 11), read the deferred entity.
            if (def->typeId == 11 && def->outputCount > 0) {
                const EntityId toDestroy = outputsPtr[0].data.entity;
                if (toDestroy != NULL_ENTITY &&
                    m_pendingDestroyCount < MAX_PENDING_DESTROYS) {
                    m_pendingDestroys[m_pendingDestroyCount++] = toDestroy;
                }
            }

            // Inline connection propagation: after this node executes,
            // copy its outputs to all downstream consumer input slots.
            // This guarantees same-frame data flow (producer before consumer
            // is guaranteed by topological order).
            const NodeId curNodeId = inst.id;
            for (uint32_t ci = 0; ci < asset->connectionCount; ++ci) {
                const Connection& c = asset->connections[ci];
                if (c.srcNode != curNodeId) continue;

                const int dstIdx = findNodeIndex(c.dstNode);
                if (dstIdx < 0) continue;

                // Source output slot in scratch.
                const uint32_t srcSlot = base + def->inputCount + c.srcPort;
                // Destination input slot in scratch.
                const uint32_t dstSlot =
                    asset->portScratchOffset[static_cast<uint32_t>(dstIdx)] + c.dstPort;

                m_scratch[dstSlot] = m_scratch[srcSlot];
            }
        }

        if (capHit) break;
    }

    // Flush deferred destroy requests AFTER entity iteration loop.
    for (uint32_t di = 0; di < m_pendingDestroyCount; ++di) {
        const EntityId target = m_pendingDestroys[di];
        if (world.isValid(target)) {
            world.destroyEntity(target);
        }
    }
    m_pendingDestroyCount = 0;
}

// ---------------------------------------------------------------------------
// getGraphAsset
// ---------------------------------------------------------------------------

const GraphAsset* VisualScriptingSystem::getGraphAsset(GraphHandle handle) const {
    if (handle.id == 0 || handle.id >= static_cast<uint32_t>(MAX_GRAPHS)) return nullptr;
    const int idx = static_cast<int>(handle.id);
    if (!m_occupied[idx]) return nullptr;
    return m_pool[idx];
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

const NodeDef* VisualScriptingSystem::getNodeDef(const NodeTypeId typeId) {
    return findNodeDefById(typeId);
}

int VisualScriptingSystem::getNodeTypeCount() {
    return NODE_REGISTRY_COUNT;
}

void VisualScriptingSystem::registerPlaySoundFn(const PlaySoundFn fn) {
    s_playSoundFn = fn;
}

} // namespace ffe
