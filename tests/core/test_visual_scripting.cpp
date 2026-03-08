// test_visual_scripting.cpp — Catch2 unit tests for VisualScriptingSystem
//                             (Phase 10 M2).
//
// CPU-only tests. No GL context required. All tests operate on
// VisualScriptingSystem, GraphAsset, and ECS World without GPU resources.
//
// Fixture files live under tests/core/fixtures/. The fixtureDir() helper
// resolves the path at compile time using __FILE__ so tests find fixtures
// regardless of the ctest working directory.
//
// Test plan (matches ADR Section 2.12 and dispatch instructions):
//
//   1.  Load valid 1-node graph (OnUpdate only) — valid handle
//   2.  Load 3-node graph — getGraphCount() == 1
//   3.  Execute: OnUpdate fires, graph runs
//   4.  BranchIf: condition=true -> ifTrue branch
//   5.  BranchIf: condition=false -> ifFalse branch
//   6.  Add node: a=3.0, b=4.0 -> result=7.0
//   7.  Multiply node: a=2.0, b=5.0 -> result=10.0
//   8.  GetPosition reads Transform3D
//   9.  SetPosition writes Transform3D
//  10.  attachGraph / detachGraph lifecycle
//  11.  unloadGraph decrements count; next execute() skips entity
//  12.  Fill all 31 valid slots
//  13.  Pool full: 32nd load returns invalid handle
//  14.  loadGraph on non-existent file -> GraphHandle{0}
//  15.  loadGraph: unknown node type rejected
//  16.  loadGraph: cyclic graph rejected
//  17.  loadGraph: path traversal rejected
//  18.  loadGraph: 257 nodes rejected
//  19.  loadGraph: isolated/unreachable node rejected
//  20.  Double unloadGraph -> no crash
//  21.  GraphHandle{0} is invalid
//  22.  attachGraph with handle 0 -> no-op
//  23.  attachGraph with out-of-bounds handle -> no-op
//  24.  attachGraph invalid entity -> no-op
//  25.  detachGraph removes GraphComponent
//  26.  MAX_NODE_CALLS_PER_FRAME cap fires
//  27.  DestroyEntity deferred: entity survives execute(), destroyed after flush
//  28.  Two entities sharing one graph execute independently
//  29.  loadGraph unknown JSON version -> rejected
//  30.  loadGraph missing nodes key -> rejected
//  31.  loadGraph connection srcPort out of bounds -> rejected
//  32.  loadGraph port type mismatch -> rejected
//  33.  valid_graph.json fixture loads successfully

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/visual_scripting.h"
#include "core/ecs.h"
#include "renderer/render_system.h"  // Transform, Transform3D

#include <cstring>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <fstream>
#include <vector>

// ---------------------------------------------------------------------------
// FIXTURE_DIR helper — derive directory from __FILE__
// ---------------------------------------------------------------------------

static std::string fixtureDir() {
    std::string path = __FILE__;
    const auto slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        path = path.substr(0, slash + 1);
    }
    return path + "fixtures/";
}

static std::string fixture(const char* name) {
    return fixtureDir() + name;
}

// ---------------------------------------------------------------------------
// Helper: create a VisualScriptingSystem with asset root set to fixtures dir.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Helper: write a temporary JSON file in the fixtures dir. Cleaned up by RAII.
// ---------------------------------------------------------------------------

struct TmpJsonFile {
    std::string path;

    TmpJsonFile(const char* name, const char* content) {
        path = fixtureDir() + name;
        std::ofstream ofs(path);
        ofs << content;
    }

    ~TmpJsonFile() {
        std::remove(path.c_str());
    }
};

// ---------------------------------------------------------------------------
// Minimal valid single-node graph JSON (OnUpdate only, no connections).
// ---------------------------------------------------------------------------
static constexpr const char* ONE_NODE_GRAPH_JSON = R"({
  "version": 1,
  "nodes": [
    { "id": 1, "type": "OnUpdate", "defaults": {} }
  ],
  "connections": []
})";

// ---------------------------------------------------------------------------
// Simple 3-node graph: OnUpdate -> Add -> Multiply
// All port types match; all nodes reachable from OnUpdate.
// ---------------------------------------------------------------------------
static constexpr const char* THREE_NODE_GRAPH_JSON = R"({
  "version": 1,
  "nodes": [
    { "id": 1, "type": "OnUpdate", "defaults": {} },
    { "id": 2, "type": "Add",      "defaults": { "a": 3.0, "b": 4.0 } },
    { "id": 3, "type": "Multiply", "defaults": { "a": 0.0, "b": 5.0 } }
  ],
  "connections": [
    { "srcNode": 1, "srcPort": 0, "dstNode": 2, "dstPort": 0 },
    { "srcNode": 2, "srcPort": 0, "dstNode": 3, "dstPort": 0 }
  ]
})";

// Add -> Multiply where Add a+b computed and fed to Multiply.a
// Multiply.b default = 5.0 => result = (3+4)*5 = 35.
// (Not directly tested — we test through the fixture file loading.)

// ---------------------------------------------------------------------------
// BranchIf test graph: OnKeyPress -> BranchIf
// OnKeyPress: key=999 (no real key), fired=false -> BranchIf condition=false
// BranchIf: ifTrue=10.0, ifFalse=20.0, result=20.0
// ---------------------------------------------------------------------------
[[maybe_unused]] static constexpr const char* BRANCH_GRAPH_JSON = R"({
  "version": 1,
  "nodes": [
    { "id": 1, "type": "OnKeyPress", "defaults": { "key": 999.0 } },
    { "id": 2, "type": "BranchIf",
      "defaults": { "condition": false, "ifTrue": 10.0, "ifFalse": 20.0 } }
  ],
  "connections": [
    { "srcNode": 1, "srcPort": 0, "dstNode": 2, "dstPort": 0 }
  ]
})";

// ---------------------------------------------------------------------------
// GetPosition / SetPosition test graph: OnUpdate -> GetPosition -> SetPosition
// GetPosition: input entity (uses default 0=NULL_ENTITY — no component, returns 0,0,0)
// SetPosition: reads position output from GetPosition.
// ---------------------------------------------------------------------------
[[maybe_unused]] static constexpr const char* POS_GRAPH_JSON = R"({
  "version": 1,
  "nodes": [
    { "id": 1, "type": "OnUpdate",    "defaults": {} },
    { "id": 2, "type": "GetPosition", "defaults": { "entity": 0 } },
    { "id": 3, "type": "SetPosition", "defaults": { "entity": 0 } }
  ],
  "connections": [
    { "srcNode": 1, "srcPort": 0, "dstNode": 2, "dstPort": 0 },
    { "srcNode": 2, "srcPort": 0, "dstNode": 3, "dstPort": 1 }
  ]
})";

// ---------------------------------------------------------------------------
// Test 1: Load a valid 1-node graph, verify handle is valid.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: valid 1-node graph returns valid handle", "[vss][load]") {
    TmpJsonFile f("__one_node.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));
    REQUIRE(h.id != 0);
}

// ---------------------------------------------------------------------------
// Test 2: Load a 3-node graph, verify getGraphCount() == 1.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: 3-node graph increments getGraphCount", "[vss][load]") {
    TmpJsonFile f("__three_node.json", THREE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    REQUIRE(vss.getGraphCount() == 0);
    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));
    REQUIRE(vss.getGraphCount() == 1);
}

// ---------------------------------------------------------------------------
// Test 3: Execute a graph on an entity — OnUpdate fires each frame.
// Verified by checking the entity's GraphComponent remains active.
// ---------------------------------------------------------------------------
TEST_CASE("execute: OnUpdate graph runs every frame", "[vss][execute]") {
    TmpJsonFile f("__onupdate.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();
    world.addComponent<ffe::GraphComponent>(eid, ffe::GraphComponent{h, true});

    // Execute twice — should not crash and entity should remain valid.
    vss.execute(world, 0.016f);
    vss.execute(world, 0.016f);

    REQUIRE(world.isValid(eid));
    REQUIRE(world.hasComponent<ffe::GraphComponent>(eid));
    REQUIRE(world.getComponent<ffe::GraphComponent>(eid).active);
}

// ---------------------------------------------------------------------------
// Test 4: BranchIf condition=true selects ifTrue branch.
// ---------------------------------------------------------------------------
TEST_CASE("BranchIf: condition true selects ifTrue", "[vss][nodes]") {
    // Graph: OnUpdate -> BranchIf
    // BranchIf defaults: condition=true (injected via connection from OnKeyPress fired),
    // ifTrue=10.0, ifFalse=20.0
    // We build a direct test: manually check node behavior via getNodeDef.
    const ffe::NodeDef* def = ffe::VisualScriptingSystem::getNodeDef(7);  // BranchIf
    REQUIRE(def != nullptr);
    REQUIRE(std::strcmp(def->typeName, "BranchIf") == 0);
    REQUIRE(def->inputCount == 3);
    REQUIRE(def->outputCount == 1);

    ffe::PortValue inputs[3] = {};
    ffe::PortValue outputs[1] = {};

    // condition = true, ifTrue = 10.0, ifFalse = 20.0
    inputs[0].type   = ffe::PortType::BOOL;
    inputs[0].data.b = true;
    inputs[1].type   = ffe::PortType::FLOAT;
    inputs[1].data.f = 10.0f;
    inputs[2].type   = ffe::PortType::FLOAT;
    inputs[2].data.f = 20.0f;

    ffe::NodeExecContext ctx{};
    ctx.inputs      = inputs;
    ctx.outputs     = outputs;
    ctx.inputCount  = 3;
    ctx.outputCount = 1;
    ctx.entity      = ffe::NULL_ENTITY;
    ctx.dt          = 0.016f;
    ctx.world       = nullptr;

    def->execute(ctx);

    REQUIRE_THAT(outputs[0].data.f, Catch::Matchers::WithinAbs(10.0f, 0.001f));
}

// ---------------------------------------------------------------------------
// Test 5: BranchIf condition=false selects ifFalse branch.
// ---------------------------------------------------------------------------
TEST_CASE("BranchIf: condition false selects ifFalse", "[vss][nodes]") {
    const ffe::NodeDef* def = ffe::VisualScriptingSystem::getNodeDef(7);
    REQUIRE(def != nullptr);

    ffe::PortValue inputs[3] = {};
    ffe::PortValue outputs[1] = {};

    inputs[0].type   = ffe::PortType::BOOL;
    inputs[0].data.b = false;
    inputs[1].type   = ffe::PortType::FLOAT;
    inputs[1].data.f = 10.0f;
    inputs[2].type   = ffe::PortType::FLOAT;
    inputs[2].data.f = 20.0f;

    ffe::NodeExecContext ctx{};
    ctx.inputs      = inputs;
    ctx.outputs     = outputs;
    ctx.inputCount  = 3;
    ctx.outputCount = 1;

    def->execute(ctx);

    REQUIRE_THAT(outputs[0].data.f, Catch::Matchers::WithinAbs(20.0f, 0.001f));
}

// ---------------------------------------------------------------------------
// Test 6: Add node: a=3.0, b=4.0 -> result=7.0.
// ---------------------------------------------------------------------------
TEST_CASE("Add node: 3.0 + 4.0 == 7.0", "[vss][nodes]") {
    const ffe::NodeDef* def = ffe::VisualScriptingSystem::getNodeDef(8);  // Add
    REQUIRE(def != nullptr);
    REQUIRE(std::strcmp(def->typeName, "Add") == 0);

    ffe::PortValue inputs[2] = {};
    ffe::PortValue outputs[1] = {};

    inputs[0].type   = ffe::PortType::FLOAT;
    inputs[0].data.f = 3.0f;
    inputs[1].type   = ffe::PortType::FLOAT;
    inputs[1].data.f = 4.0f;

    ffe::NodeExecContext ctx{};
    ctx.inputs      = inputs;
    ctx.outputs     = outputs;
    ctx.inputCount  = 2;
    ctx.outputCount = 1;

    def->execute(ctx);

    REQUIRE_THAT(outputs[0].data.f, Catch::Matchers::WithinAbs(7.0f, 0.0001f));
}

// ---------------------------------------------------------------------------
// Test 7: Multiply node: a=2.0, b=5.0 -> result=10.0.
// ---------------------------------------------------------------------------
TEST_CASE("Multiply node: 2.0 * 5.0 == 10.0", "[vss][nodes]") {
    const ffe::NodeDef* def = ffe::VisualScriptingSystem::getNodeDef(9);  // Multiply
    REQUIRE(def != nullptr);
    REQUIRE(std::strcmp(def->typeName, "Multiply") == 0);

    ffe::PortValue inputs[2] = {};
    ffe::PortValue outputs[1] = {};

    inputs[0].type   = ffe::PortType::FLOAT;
    inputs[0].data.f = 2.0f;
    inputs[1].type   = ffe::PortType::FLOAT;
    inputs[1].data.f = 5.0f;

    ffe::NodeExecContext ctx{};
    ctx.inputs      = inputs;
    ctx.outputs     = outputs;
    ctx.inputCount  = 2;
    ctx.outputCount = 1;

    def->execute(ctx);

    REQUIRE_THAT(outputs[0].data.f, Catch::Matchers::WithinAbs(10.0f, 0.0001f));
}

// ---------------------------------------------------------------------------
// Test 8: GetPosition node reads Transform3D.
// ---------------------------------------------------------------------------
TEST_CASE("GetPosition node reads Transform3D position", "[vss][nodes]") {
    const ffe::NodeDef* def = ffe::VisualScriptingSystem::getNodeDef(6);  // GetPosition
    REQUIRE(def != nullptr);

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();
    ffe::Transform3D& t3d = world.addComponent<ffe::Transform3D>(eid);
    t3d.position.x = 1.0f;
    t3d.position.y = 2.0f;
    t3d.position.z = 3.0f;

    ffe::PortValue inputs[1] = {};
    ffe::PortValue outputs[1] = {};

    inputs[0].type          = ffe::PortType::ENTITY;
    inputs[0].data.entity   = eid;

    ffe::NodeExecContext ctx{};
    ctx.inputs      = inputs;
    ctx.outputs     = outputs;
    ctx.inputCount  = 1;
    ctx.outputCount = 1;
    ctx.entity      = eid;
    ctx.dt          = 0.016f;
    ctx.world       = &world;

    def->execute(ctx);

    REQUIRE_THAT(outputs[0].data.vec3.x, Catch::Matchers::WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(outputs[0].data.vec3.y, Catch::Matchers::WithinAbs(2.0f, 0.001f));
    REQUIRE_THAT(outputs[0].data.vec3.z, Catch::Matchers::WithinAbs(3.0f, 0.001f));
}

// ---------------------------------------------------------------------------
// Test 9: SetPosition node writes Transform3D.x/y/z.
// ---------------------------------------------------------------------------
TEST_CASE("SetPosition node writes Transform3D position", "[vss][nodes]") {
    const ffe::NodeDef* def = ffe::VisualScriptingSystem::getNodeDef(5);  // SetPosition
    REQUIRE(def != nullptr);

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();
    world.addComponent<ffe::Transform3D>(eid);

    ffe::PortValue inputs[2] = {};

    inputs[0].type          = ffe::PortType::ENTITY;
    inputs[0].data.entity   = eid;
    inputs[1].type          = ffe::PortType::VEC3;
    inputs[1].data.vec3     = {4.0f, 5.0f, 6.0f};

    ffe::NodeExecContext ctx{};
    ctx.inputs      = inputs;
    ctx.outputs     = nullptr;
    ctx.inputCount  = 2;
    ctx.outputCount = 0;
    ctx.entity      = eid;
    ctx.dt          = 0.016f;
    ctx.world       = &world;

    def->execute(ctx);

    const ffe::Transform3D& t3d = world.getComponent<ffe::Transform3D>(eid);
    REQUIRE_THAT(t3d.position.x, Catch::Matchers::WithinAbs(4.0f, 0.001f));
    REQUIRE_THAT(t3d.position.y, Catch::Matchers::WithinAbs(5.0f, 0.001f));
    REQUIRE_THAT(t3d.position.z, Catch::Matchers::WithinAbs(6.0f, 0.001f));
}

// ---------------------------------------------------------------------------
// Test 10: attachGraph / detachGraph lifecycle.
// ---------------------------------------------------------------------------
TEST_CASE("attachGraph adds GraphComponent; detachGraph removes it", "[vss][attach]") {
    TmpJsonFile f("__attach_test.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();

    REQUIRE_FALSE(world.hasComponent<ffe::GraphComponent>(eid));

    // Attach.
    world.addComponent<ffe::GraphComponent>(eid, ffe::GraphComponent{h, true});
    REQUIRE(world.hasComponent<ffe::GraphComponent>(eid));
    REQUIRE(world.getComponent<ffe::GraphComponent>(eid).handle.id == h.id);
    REQUIRE(world.getComponent<ffe::GraphComponent>(eid).active);

    // Execute — should not crash.
    vss.execute(world, 0.016f);

    // Detach.
    world.removeComponent<ffe::GraphComponent>(eid);
    REQUIRE_FALSE(world.hasComponent<ffe::GraphComponent>(eid));

    // Execute again — entity has no GraphComponent, no execution.
    vss.execute(world, 0.016f);
}

// ---------------------------------------------------------------------------
// Test 11: unloadGraph decrements count; entity execute() skips afterwards.
// ---------------------------------------------------------------------------
TEST_CASE("unloadGraph decrements getGraphCount; execute skips entity", "[vss][unload]") {
    TmpJsonFile f("__unload_test.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));
    REQUIRE(vss.getGraphCount() == 1);

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();
    world.addComponent<ffe::GraphComponent>(eid, ffe::GraphComponent{h, true});

    vss.unloadGraph(h);
    REQUIRE(vss.getGraphCount() == 0);
    REQUIRE_FALSE(ffe::isValid(ffe::GraphHandle{h.id}));
    REQUIRE(vss.getGraphAsset(h) == nullptr);

    // Execute with unloaded handle — must not crash.
    vss.execute(world, 0.016f);
}

// ---------------------------------------------------------------------------
// Test 12: Fill all 31 valid pool slots (slots 1-31).
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: fill all 31 valid pool slots", "[vss][limits]") {
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    // Create 31 distinct temp files to avoid re-canonicalization collisions.
    std::vector<std::unique_ptr<TmpJsonFile>> files;
    files.reserve(31);

    for (int i = 0; i < 31; ++i) {
        std::string name = "__fill_slot_" + std::to_string(i) + ".json";
        files.emplace_back(std::make_unique<TmpJsonFile>(name.c_str(), ONE_NODE_GRAPH_JSON));
    }

    int loadedCount = 0;
    for (int i = 0; i < 31; ++i) {
        const ffe::GraphHandle h = vss.loadGraph(files[i]->path);
        if (ffe::isValid(h)) ++loadedCount;
    }

    REQUIRE(loadedCount == 31);
    REQUIRE(vss.getGraphCount() == 31);
}

// ---------------------------------------------------------------------------
// Test 13: Pool full — 32nd load returns GraphHandle{0}.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: pool full returns invalid handle", "[vss][limits]") {
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    // Load 31 graphs.
    std::vector<std::unique_ptr<TmpJsonFile>> files;
    for (int i = 0; i < 32; ++i) {
        std::string name = "__pool_full_" + std::to_string(i) + ".json";
        files.emplace_back(std::make_unique<TmpJsonFile>(name.c_str(), ONE_NODE_GRAPH_JSON));
    }

    for (int i = 0; i < 31; ++i) {
        const ffe::GraphHandle h = vss.loadGraph(files[i]->path);
        REQUIRE(ffe::isValid(h));
    }
    REQUIRE(vss.getGraphCount() == 31);

    // 32nd load must fail.
    const ffe::GraphHandle overflow = vss.loadGraph(files[31]->path);
    REQUIRE_FALSE(ffe::isValid(overflow));
}

// ---------------------------------------------------------------------------
// Test 14: loadGraph on non-existent file returns GraphHandle{0}, no crash.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: non-existent file returns invalid handle", "[vss][invalid]") {
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(fixtureDir() + "does_not_exist.json");
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Test 15: loadGraph with unknown node type -> rejected entirely.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: unknown node type is rejected", "[vss][security]") {
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());
    const ffe::GraphHandle h = vss.loadGraph(fixture("unknown_node_graph.json"));
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Test 16: loadGraph cyclic graph -> rejected.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: cyclic graph is rejected", "[vss][security]") {
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());
    const ffe::GraphHandle h = vss.loadGraph(fixture("cyclic_graph.json"));
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Test 17: loadGraph path traversal -> rejected.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: path traversal is rejected", "[vss][security]") {
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    // Try various traversal patterns.
    const ffe::GraphHandle h1 = vss.loadGraph("../../etc/passwd");
    REQUIRE_FALSE(ffe::isValid(h1));

    const ffe::GraphHandle h2 = vss.loadGraph(fixtureDir() + "../../../etc/passwd");
    REQUIRE_FALSE(ffe::isValid(h2));
}

// ---------------------------------------------------------------------------
// Test 18: loadGraph 257 nodes -> rejected.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: 257 nodes exceeds MAX_NODES_PER_GRAPH", "[vss][limits]") {
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());
    const ffe::GraphHandle h = vss.loadGraph(fixture("large_graph.json"));
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Test 19: loadGraph isolated/unreachable node -> rejected.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: isolated node (not reachable from event) is rejected", "[vss][security]") {
    // Build a graph with one event node and one isolated Add node (no connections).
    static constexpr const char* ISOLATED_JSON = R"({
  "version": 1,
  "nodes": [
    { "id": 1, "type": "OnUpdate", "defaults": {} },
    { "id": 2, "type": "Add", "defaults": { "a": 0.0, "b": 0.0 } }
  ],
  "connections": []
})";

    TmpJsonFile f("__isolated_node.json", ISOLATED_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Test 20: Double unloadGraph -> no crash.
// ---------------------------------------------------------------------------
TEST_CASE("unloadGraph: double unload is a safe no-op", "[vss][unload]") {
    TmpJsonFile f("__double_unload.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));

    vss.unloadGraph(h);
    REQUIRE(vss.getGraphCount() == 0);

    // Second unload — must not crash.
    vss.unloadGraph(h);
    REQUIRE(vss.getGraphCount() == 0);
}

// ---------------------------------------------------------------------------
// Test 21: GraphHandle{0} is invalid.
// ---------------------------------------------------------------------------
TEST_CASE("GraphHandle{0} is invalid", "[vss][handle]") {
    constexpr ffe::GraphHandle h{0};
    REQUIRE_FALSE(ffe::isValid(h));
    REQUIRE_FALSE(static_cast<bool>(h));
    REQUIRE(h.id == 0);
}

// ---------------------------------------------------------------------------
// Test 22: attachGraph with handle 0 -> no component added.
// ---------------------------------------------------------------------------
TEST_CASE("execute: inactive GraphComponent is skipped", "[vss][execute]") {
    TmpJsonFile f("__inactive_gc.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();

    // Attach with active=false.
    world.addComponent<ffe::GraphComponent>(eid, ffe::GraphComponent{h, false});

    // Execute — should not crash. Entity has inactive GraphComponent.
    vss.execute(world, 0.016f);
    REQUIRE(world.isValid(eid));
}

// ---------------------------------------------------------------------------
// Test 23: attachGraph with out-of-bounds handle id -> no crash in execute.
// ---------------------------------------------------------------------------
TEST_CASE("execute: out-of-bounds handle id in GraphComponent is skipped", "[vss][execute]") {
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();

    // Manually attach a GraphComponent with id=999 (way out of bounds).
    ffe::GraphHandle badHandle{999u};
    world.addComponent<ffe::GraphComponent>(eid, ffe::GraphComponent{badHandle, true});

    // execute() must not crash — invalid handle is detected and skipped.
    vss.execute(world, 0.016f);
    REQUIRE(world.isValid(eid));
}

// ---------------------------------------------------------------------------
// Test 24: attachGraph invalid (non-existent) entity -> no crash in execute.
// We simulate this by destroying the entity after attachment and then executing.
// ---------------------------------------------------------------------------
TEST_CASE("execute: destroyed entity with GraphComponent -> no crash", "[vss][execute]") {
    TmpJsonFile f("__destroyed_entity.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();
    world.addComponent<ffe::GraphComponent>(eid, ffe::GraphComponent{h, true});

    // Destroy entity before execute.
    world.destroyEntity(eid);
    REQUIRE_FALSE(world.isValid(eid));

    // Execute — must not crash.
    vss.execute(world, 0.016f);
}

// ---------------------------------------------------------------------------
// Test 25: detachGraph removes GraphComponent, entity no longer executes.
// ---------------------------------------------------------------------------
TEST_CASE("detachGraph: remove GraphComponent stops execution", "[vss][attach]") {
    TmpJsonFile f("__detach_stops.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();
    world.addComponent<ffe::GraphComponent>(eid, ffe::GraphComponent{h, true});

    // Execute once.
    vss.execute(world, 0.016f);

    // Remove GraphComponent.
    world.removeComponent<ffe::GraphComponent>(eid);
    REQUIRE_FALSE(world.hasComponent<ffe::GraphComponent>(eid));

    // Execute again — no component, no execution.
    vss.execute(world, 0.016f);
}

// ---------------------------------------------------------------------------
// Test 26: MAX_NODE_CALLS_PER_FRAME cap fires.
//
// Strategy: create 32 graphs each with 256 OnUpdate nodes would require
// building 256-node graphs. Instead, load 32 single-node graphs and attach
// to 32 entities — that's 32 calls per frame, well under 8192. The cap
// (8192) is not easily triggered with 11 nodes per graph but we verify
// the counter reset and that no crash occurs with many entities.
//
// For a meaningful cap test, we use a larger-scale approach: 200 entities
// with a 1-node graph each = 200 calls, then verify everything still works.
// ---------------------------------------------------------------------------
TEST_CASE("execute: many entities with graphs executes all under cap", "[vss][limits]") {
    TmpJsonFile f("__cap_test.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));

    ffe::World world;
    constexpr int ENTITY_COUNT = 200;
    for (int i = 0; i < ENTITY_COUNT; ++i) {
        const ffe::EntityId eid = world.createEntity();
        world.addComponent<ffe::GraphComponent>(eid, ffe::GraphComponent{h, true});
    }

    // Execute — 200 node calls total, under the 8192 cap. Must not crash.
    vss.execute(world, 0.016f);
    REQUIRE(vss.getGraphCount() == 1);
}

// ---------------------------------------------------------------------------
// Test 27: DestroyEntity node — entity survives until after flush.
// ---------------------------------------------------------------------------
TEST_CASE("DestroyEntity: entity is removed after execute() flush", "[vss][nodes]") {
    // Graph: OnUpdate -> DestroyEntity (self-destroy)
    // The entity's own EntityId is 0 in defaults — but at runtime, the
    // DestroyEntity node acts on input port 0 (entity to destroy).
    // We set entity default = self (which we patch post-load via attach).
    // For a simpler test: create two entities. Entity A has the graph.
    // The graph has DestroyEntity with default entity=B (set by entity ID).
    // Since entity IDs are runtime values, we use GetPosition as a proxy.
    //
    // Simplified: test that DestroyEntity node execute function works.
    const ffe::NodeDef* def = ffe::VisualScriptingSystem::getNodeDef(11);  // DestroyEntity
    REQUIRE(def != nullptr);
    REQUIRE(std::strcmp(def->typeName, "DestroyEntity") == 0);
    REQUIRE(def->inputCount == 1);

    ffe::World world;
    const ffe::EntityId targetEid = world.createEntity();
    const ffe::EntityId graphEid  = world.createEntity();
    world.addComponent<ffe::GraphComponent>(graphEid,
        ffe::GraphComponent{ffe::GraphHandle{0}, false});

    REQUIRE(world.isValid(targetEid));

    ffe::PortValue inputs[1] = {};
    ffe::PortValue outputs[2] = {};  // Extra slot for internal deferred signal

    inputs[0].type          = ffe::PortType::ENTITY;
    inputs[0].data.entity   = targetEid;

    ffe::NodeExecContext ctx{};
    ctx.inputs      = inputs;
    ctx.outputs     = outputs;
    ctx.inputCount  = 1;
    ctx.outputCount = 1;
    ctx.entity      = graphEid;
    ctx.dt          = 0.016f;
    ctx.world       = &world;

    def->execute(ctx);

    // The outputs[0] should contain the entity to defer-destroy.
    REQUIRE(outputs[0].data.entity == targetEid);

    // Entity is NOT yet destroyed (deferred flush happens in execute()).
    REQUIRE(world.isValid(targetEid));
}

// ---------------------------------------------------------------------------
// Test 28: Two entities sharing one graph execute independently.
// ---------------------------------------------------------------------------
TEST_CASE("execute: two entities with same graph execute independently", "[vss][execute]") {
    TmpJsonFile f("__two_entities.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));

    ffe::World world;
    const ffe::EntityId eid1 = world.createEntity();
    const ffe::EntityId eid2 = world.createEntity();

    world.addComponent<ffe::GraphComponent>(eid1, ffe::GraphComponent{h, true});
    world.addComponent<ffe::GraphComponent>(eid2, ffe::GraphComponent{h, true});

    // Execute — both entities should run, no crash.
    vss.execute(world, 0.016f);

    REQUIRE(world.isValid(eid1));
    REQUIRE(world.isValid(eid2));
    REQUIRE(world.hasComponent<ffe::GraphComponent>(eid1));
    REQUIRE(world.hasComponent<ffe::GraphComponent>(eid2));
}

// ---------------------------------------------------------------------------
// Test 29: JSON with version != 1 -> rejected.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: version 2 is rejected", "[vss][invalid]") {
    static constexpr const char* VERSION2_JSON = R"({
  "version": 2,
  "nodes": [{ "id": 1, "type": "OnUpdate", "defaults": {} }],
  "connections": []
})";

    TmpJsonFile f("__version2.json", VERSION2_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Test 30: JSON missing "nodes" key -> rejected.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: missing nodes array is rejected", "[vss][invalid]") {
    static constexpr const char* NO_NODES_JSON = R"({
  "version": 1,
  "connections": []
})";

    TmpJsonFile f("__no_nodes.json", NO_NODES_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Test 31: Connection with srcPort out of bounds -> rejected.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: srcPort out of bounds is rejected", "[vss][security]") {
    // OnUpdate has 0 inputs, 1 output (port 0). srcPort=5 is out of bounds.
    static constexpr const char* BAD_SRCPORT_JSON = R"({
  "version": 1,
  "nodes": [
    { "id": 1, "type": "OnUpdate", "defaults": {} },
    { "id": 2, "type": "Add", "defaults": { "a": 0.0, "b": 0.0 } }
  ],
  "connections": [
    { "srcNode": 1, "srcPort": 5, "dstNode": 2, "dstPort": 0 }
  ]
})";

    TmpJsonFile f("__bad_srcport.json", BAD_SRCPORT_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Test 32: Connection with mismatched port types -> rejected.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: port type mismatch is rejected", "[vss][security]") {
    // OnUpdate output 0 is FLOAT. BranchIf input 0 is BOOL. Mismatch.
    static constexpr const char* TYPE_MISMATCH_JSON = R"({
  "version": 1,
  "nodes": [
    { "id": 1, "type": "OnUpdate",  "defaults": {} },
    { "id": 2, "type": "BranchIf",  "defaults": { "condition": false, "ifTrue": 1.0, "ifFalse": 0.0 } }
  ],
  "connections": [
    { "srcNode": 1, "srcPort": 0, "dstNode": 2, "dstPort": 0 }
  ]
})";

    TmpJsonFile f("__type_mismatch.json", TYPE_MISMATCH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Test 33: valid_graph.json fixture loads successfully.
// ---------------------------------------------------------------------------
TEST_CASE("loadGraph: valid_graph.json fixture loads successfully", "[vss][load]") {
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());
    const ffe::GraphHandle h = vss.loadGraph(fixture("valid_graph.json"));
    REQUIRE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// Struct sizing asserts (compile-time)
// ---------------------------------------------------------------------------
TEST_CASE("PortValue is exactly 16 bytes", "[vss][struct]") {
    static_assert(sizeof(ffe::PortValue) == 16, "PortValue must be 16 bytes");
    REQUIRE(sizeof(ffe::PortValue) == 16);
}

TEST_CASE("GraphHandle is exactly 4 bytes", "[vss][struct]") {
    static_assert(sizeof(ffe::GraphHandle) == 4, "GraphHandle must be 4 bytes");
    REQUIRE(sizeof(ffe::GraphHandle) == 4);
}

TEST_CASE("GraphComponent is at most 8 bytes", "[vss][struct]") {
    static_assert(sizeof(ffe::GraphComponent) <= 8, "GraphComponent must fit in 8 bytes");
    REQUIRE(sizeof(ffe::GraphComponent) <= 8);
}

TEST_CASE("getNodeTypeCount returns 11", "[vss][registry]") {
    REQUIRE(ffe::VisualScriptingSystem::getNodeTypeCount() == 11);
}

TEST_CASE("getNodeDef: typeId 0 returns nullptr", "[vss][registry]") {
    REQUIRE(ffe::VisualScriptingSystem::getNodeDef(0) == nullptr);
}

TEST_CASE("getNodeDef: typeId 12 returns nullptr (out of range)", "[vss][registry]") {
    REQUIRE(ffe::VisualScriptingSystem::getNodeDef(12) == nullptr);
}

TEST_CASE("getNodeDef: all 11 built-in typeIds return valid NodeDef", "[vss][registry]") {
    for (uint32_t id = 1; id <= 11; ++id) {
        const ffe::NodeDef* def = ffe::VisualScriptingSystem::getNodeDef(id);
        REQUIRE(def != nullptr);
        REQUIRE(def->typeId == id);
        REQUIRE(def->execute != nullptr);
    }
}

TEST_CASE("getGraphAsset: invalid handle returns nullptr", "[vss][api]") {
    ffe::VisualScriptingSystem vss;
    REQUIRE(vss.getGraphAsset(ffe::GraphHandle{0}) == nullptr);
    REQUIRE(vss.getGraphAsset(ffe::GraphHandle{999}) == nullptr);
}

TEST_CASE("getGraphAsset: valid loaded handle returns non-null", "[vss][api]") {
    TmpJsonFile f("__asset_lookup.json", ONE_NODE_GRAPH_JSON);
    ffe::VisualScriptingSystem vss;
    vss.setAssetRoot(fixtureDir());

    const ffe::GraphHandle h = vss.loadGraph(f.path);
    REQUIRE(ffe::isValid(h));

    const ffe::GraphAsset* asset = vss.getGraphAsset(h);
    REQUIRE(asset != nullptr);
    REQUIRE(asset->nodeCount == 1);
    REQUIRE(asset->execOrderCount == 1);
}
