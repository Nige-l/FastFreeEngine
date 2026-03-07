// test_scene_load_binding.cpp — Catch2 unit tests for the ffe.loadSceneJSON
// Lua binding added in Session 56 (build pipeline).
//
// Tests:
//   1. ffe.loadSceneJSON exists as a callable binding
//   2. Loading a nonexistent file returns false
//   3. Loading a valid scene JSON file succeeds and populates entities
//
// All tests construct a minimal ffe::World, initialise a ScriptEngine,
// and drive the binding via engine.doString().

#include <catch2/catch_test_macros.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/render_system.h"
#include "scene/scene_serialiser.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World with script root set to a temp directory.
// ---------------------------------------------------------------------------

namespace {

struct SceneBindingFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;
    char              tmpDir[256] = {};

    SceneBindingFixture() {
        REQUIRE(engine.init());
        engine.setWorld(&world);

        // Use /tmp/ffe_test_scene_binding as the script root.
        std::snprintf(tmpDir, sizeof(tmpDir), "/tmp/ffe_test_scene_binding");
        std::filesystem::create_directories(tmpDir);
        engine.setScriptRoot(tmpDir);
    }

    ~SceneBindingFixture() {
        engine.shutdown();
        // Clean up temp files.
        std::filesystem::remove_all(tmpDir);
    }

    // Write a JSON string to a file under the script root.
    void writeSceneFile(const char* filename, const std::string& json) const {
        char path[512];
        std::snprintf(path, sizeof(path), "%s/%s", tmpDir, filename);
        std::ofstream ofs(path);
        REQUIRE(ofs.is_open());
        ofs << json;
    }
};

// Count live entities in a World using the EnTT entity storage iterator.
uint32_t countEntities(ffe::World& w) {
    uint32_t count = 0;
    for (auto [e] : w.registry().storage<entt::entity>().each()) {
        (void)e;
        ++count;
    }
    return count;
}

} // anonymous namespace

// =============================================================================
// Test: ffe.loadSceneJSON exists and is callable
// =============================================================================

TEST_CASE("loadSceneJSON: binding exists and is callable",
          "[scripting][scene][binding]") {
    SceneBindingFixture fix;

    // Calling with a nonexistent path should return false, not error.
    REQUIRE(fix.engine.doString(
        "local ok = ffe.loadSceneJSON('nonexistent.json')\n"
        "assert(ok == false, 'expected false for nonexistent file')\n"
    ));
}

// =============================================================================
// Test: loading a nonexistent file returns false
// =============================================================================

TEST_CASE("loadSceneJSON: nonexistent file returns false",
          "[scripting][scene][binding]") {
    SceneBindingFixture fix;

    REQUIRE(fix.engine.doString(
        "local result = ffe.loadSceneJSON('does_not_exist.json')\n"
        "assert(result == false)\n"
    ));
}

// =============================================================================
// Test: loading a valid scene JSON creates entities
// =============================================================================

TEST_CASE("loadSceneJSON: valid scene file loads entities",
          "[scripting][scene][binding]") {
    SceneBindingFixture fix;

    // Create a minimal valid scene JSON using the engine's serialiser.
    // First, create some entities in a temporary world and serialise them.
    ffe::World tempWorld;
    const auto e1 = tempWorld.createEntity();
    auto& tf1 = tempWorld.addComponent<ffe::Transform>(e1);
    tf1.position = {10.0f, 20.0f, 0.0f};

    const auto e2 = tempWorld.createEntity();
    auto& tf2 = tempWorld.addComponent<ffe::Transform>(e2);
    tf2.position = {30.0f, 40.0f, 0.0f};

    const std::string json = ffe::scene::serialiseToJson(tempWorld);
    REQUIRE(!json.empty());

    // Write the JSON to a file in the temp directory.
    fix.writeSceneFile("test_level.json", json);

    // The main world should start empty.
    CHECK(countEntities(fix.world) == 0);

    // Load the scene via the Lua binding.
    REQUIRE(fix.engine.doString(
        "local ok = ffe.loadSceneJSON('test_level.json')\n"
        "assert(ok == true, 'expected true for valid scene file')\n"
    ));

    // The world should now have 2 entities.
    CHECK(countEntities(fix.world) == 2);
}

// =============================================================================
// Test: loadSceneJSON clears existing entities before loading
// =============================================================================

TEST_CASE("loadSceneJSON: clears existing entities before loading",
          "[scripting][scene][binding]") {
    SceneBindingFixture fix;

    // Pre-populate the world with an entity.
    fix.world.createEntity();
    fix.world.createEntity();
    fix.world.createEntity();
    CHECK(countEntities(fix.world) == 3);

    // Create a scene with 1 entity.
    ffe::World tempWorld;
    const auto e = tempWorld.createEntity();
    auto& tf = tempWorld.addComponent<ffe::Transform>(e);
    tf.position = {1.0f, 2.0f, 3.0f};

    const std::string json = ffe::scene::serialiseToJson(tempWorld);
    fix.writeSceneFile("replace_level.json", json);

    REQUIRE(fix.engine.doString(
        "local ok = ffe.loadSceneJSON('replace_level.json')\n"
        "assert(ok == true)\n"
    ));

    // Should have only 1 entity (the 3 old ones cleared).
    CHECK(countEntities(fix.world) == 1);
}

// =============================================================================
// Test: loadSceneJSON rejects path traversal
// =============================================================================

TEST_CASE("loadSceneJSON: rejects path traversal",
          "[scripting][scene][binding][security]") {
    SceneBindingFixture fix;

    REQUIRE(fix.engine.doString(
        "local ok = ffe.loadSceneJSON('../../../etc/passwd')\n"
        "assert(ok == false, 'path traversal should be rejected')\n"
    ));
}

// =============================================================================
// Test: loadSceneJSON with no argument returns false
// =============================================================================

TEST_CASE("loadSceneJSON: no argument returns false",
          "[scripting][scene][binding]") {
    SceneBindingFixture fix;

    REQUIRE(fix.engine.doString(
        "local ok = ffe.loadSceneJSON()\n"
        "assert(ok == false, 'nil argument should return false')\n"
    ));
}
