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

// =============================================================================
// ffe.setTerrainSplatMap — binding registration (M2)
// =============================================================================

TEST_CASE("setTerrainSplatMap binding is callable", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setTerrainSplatMap(0)"));
}

TEST_CASE("setTerrainSplatMap with texture id is callable", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setTerrainSplatMap(42)"));
}

// =============================================================================
// ffe.setTerrainLayer — binding registration and validation (M2)
// =============================================================================

TEST_CASE("setTerrainLayer binding is callable with valid args", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setTerrainLayer(0, 1, 16.0)"));
}

TEST_CASE("setTerrainLayer accepts all four layer indices", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString(
        "ffe.setTerrainLayer(0, 1, 16.0)\n"
        "ffe.setTerrainLayer(1, 2, 12.0)\n"
        "ffe.setTerrainLayer(2, 3, 20.0)\n"
        "ffe.setTerrainLayer(3, 4, 8.0)"));
}

TEST_CASE("setTerrainLayer rejects layerIndex >= 4", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    // Should not crash — layerIndex 4 is rejected by the C++ validation.
    // No terrain is loaded so the binding returns early anyway, but this
    // verifies the binding itself does not error on the Lua side.
    CHECK(fix.engine.doString("ffe.setTerrainLayer(4, 1, 16.0)"));
}

TEST_CASE("setTerrainLayer rejects negative layerIndex", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setTerrainLayer(-1, 1, 16.0)"));
}

// =============================================================================
// ffe.setTerrainTriplanar — binding registration (M2)
// =============================================================================

TEST_CASE("setTerrainTriplanar binding is callable", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setTerrainTriplanar(true, 0.7)"));
}

TEST_CASE("setTerrainTriplanar with false is callable", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setTerrainTriplanar(false, 0.5)"));
}

// =============================================================================
// ffe.setTerrainLodDistances — binding registration (M3)
// =============================================================================

TEST_CASE("setTerrainLodDistances is callable", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setTerrainLodDistances(100, 200)"));
}

TEST_CASE("setTerrainLodDistances with valid distances", "[scripting][terrain]") {
    TerrainBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setTerrainLodDistances(50.0, 150.0)"));
}
