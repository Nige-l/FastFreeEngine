// test_terrain_bindings.cpp — Catch2 unit tests for the terrain Lua bindings:
//   ffe.loadTerrain, ffe.getTerrainHeight, ffe.unloadTerrain, ffe.setTerrainTexture
//
// Tests verify binding registration, argument validation, and correct behavior.
// These run in headless mode — no GL context, no actual terrain images loaded.

#include <catch2/catch_test_macros.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/render_system.h"
#include "renderer/terrain.h"

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World
// ---------------------------------------------------------------------------

struct TerrainBindingFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    TerrainBindingFixture() {
        REQUIRE(engine.init());
        engine.setWorld(&world);
    }
    ~TerrainBindingFixture() { engine.shutdown(); }
};

// =============================================================================
// ffe.loadTerrain — binding registration
// =============================================================================

TEST_CASE("loadTerrain binding is callable", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("local h = ffe.loadTerrain('nonexistent.png', 100, 100, 10)"));
}

TEST_CASE("loadTerrain returns 0 for nonexistent file", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "result = ffe.loadTerrain('nonexistent.png', 100, 100, 10)\n"
        "assert(result == 0, 'expected 0, got ' .. tostring(result))"));
}

// =============================================================================
// ffe.getTerrainHeight — binding registration
// =============================================================================

TEST_CASE("getTerrainHeight binding is callable", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("local h = ffe.getTerrainHeight(0, 0)"));
}

TEST_CASE("getTerrainHeight returns 0 with no terrain", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "local h = ffe.getTerrainHeight(0, 0)\n"
        "assert(h == 0, 'expected 0, got ' .. tostring(h))"));
}

// =============================================================================
// ffe.unloadTerrain — binding registration
// =============================================================================

TEST_CASE("unloadTerrain binding is callable", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.unloadTerrain()"));
}

// =============================================================================
// ffe.setTerrainTexture — binding registration
// =============================================================================

TEST_CASE("setTerrainTexture binding is callable", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setTerrainTexture(0)"));
}

// =============================================================================
// ffe.loadTerrain — argument validation
// =============================================================================

TEST_CASE("loadTerrain rejects non-string path", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    // Should not crash — returns 0 for non-string first arg.
    REQUIRE(fix.engine.doString(
        "local h = ffe.loadTerrain(123, 100, 100, 10)\n"
        "assert(h == 0, 'expected 0 for non-string path, got ' .. tostring(h))"));
}

TEST_CASE("loadTerrain rejects negative dimensions", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "local h = ffe.loadTerrain('test.png', -100, 100, 10)\n"
        "assert(h == 0, 'expected 0 for negative width, got ' .. tostring(h))"));
}
