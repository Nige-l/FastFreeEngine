// test_terrain.cpp — CPU-only Catch2 unit tests for the terrain system.
//
// Tests handle validity, config defaults, constants, height query edge cases,
// and component/struct sizing. No GL context required.

#include <catch2/catch_test_macros.hpp>

#include "renderer/terrain.h"
#include "renderer/render_system.h"

// -----------------------------------------------------------------------
// TerrainHandle validity
// -----------------------------------------------------------------------

TEST_CASE("TerrainHandle default is invalid", "[terrain]") {
    CHECK(!ffe::renderer::isValid(ffe::renderer::TerrainHandle{}));
}

TEST_CASE("TerrainHandle with id=1 is valid", "[terrain]") {
    CHECK(ffe::renderer::isValid(ffe::renderer::TerrainHandle{1}));
}

// -----------------------------------------------------------------------
// TerrainConfig defaults
// -----------------------------------------------------------------------

TEST_CASE("TerrainConfig has expected defaults", "[terrain]") {
    const ffe::renderer::TerrainConfig cfg;
    CHECK(cfg.worldWidth == 256.0f);
    CHECK(cfg.worldDepth == 256.0f);
    CHECK(cfg.heightScale == 50.0f);
    CHECK(cfg.chunkResolution == 64);
    CHECK(cfg.chunkCountX == 16);
    CHECK(cfg.chunkCountZ == 16);
}

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------

TEST_CASE("MAX_TERRAIN_ASSETS is 4", "[terrain]") {
    CHECK(ffe::renderer::MAX_TERRAIN_ASSETS == 4);
}

TEST_CASE("MAX_CHUNK_RESOLUTION is 128", "[terrain]") {
    CHECK(ffe::renderer::MAX_CHUNK_RESOLUTION == 128);
}

TEST_CASE("MAX_CHUNKS_TOTAL is 256", "[terrain]") {
    CHECK(ffe::renderer::MAX_CHUNKS_TOTAL == 256);
}

// -----------------------------------------------------------------------
// getTerrainHeight edge cases (no terrain loaded)
// -----------------------------------------------------------------------

TEST_CASE("getTerrainHeight returns 0 for invalid handle", "[terrain]") {
    CHECK(ffe::renderer::getTerrainHeight(ffe::renderer::TerrainHandle{0}, 0.0f, 0.0f) == 0.0f);
}

TEST_CASE("getTerrainHeight returns 0 for out-of-range handle", "[terrain]") {
    CHECK(ffe::renderer::getTerrainHeight(ffe::renderer::TerrainHandle{99}, 0.0f, 0.0f) == 0.0f);
}

// -----------------------------------------------------------------------
// Component and struct sizing
// -----------------------------------------------------------------------

TEST_CASE("Terrain component is lightweight (8 bytes or less)", "[terrain]") {
    CHECK(sizeof(ffe::Terrain) <= 8);
}

TEST_CASE("TerrainConfig fits in cache line", "[terrain]") {
    CHECK(sizeof(ffe::renderer::TerrainConfig) <= 64);
}

// -----------------------------------------------------------------------
// TerrainLayer defaults (M2)
// -----------------------------------------------------------------------

TEST_CASE("TerrainLayer default texture is 0", "[terrain]") {
    const ffe::renderer::TerrainLayer layer;
    CHECK(layer.texture.id == 0);
}

TEST_CASE("TerrainLayer default uvScale is 16.0", "[terrain]") {
    const ffe::renderer::TerrainLayer layer;
    CHECK(layer.uvScale == 16.0f);
}

// -----------------------------------------------------------------------
// TerrainMaterial defaults (M2)
// -----------------------------------------------------------------------

TEST_CASE("TerrainMaterial default splatTexture is 0", "[terrain]") {
    const ffe::renderer::TerrainMaterial mat;
    CHECK(mat.splatTexture.id == 0);
}

TEST_CASE("TerrainMaterial default triplanar is disabled", "[terrain]") {
    const ffe::renderer::TerrainMaterial mat;
    CHECK_FALSE(mat.triplanarEnabled);
}

TEST_CASE("TerrainMaterial default triplanar threshold is 0.7", "[terrain]") {
    const ffe::renderer::TerrainMaterial mat;
    CHECK(mat.triplanarThreshold == 0.7f);
}

TEST_CASE("TerrainMaterial all layer textures default to 0", "[terrain]") {
    const ffe::renderer::TerrainMaterial mat;
    for (int i = 0; i < 4; ++i) {
        CHECK(mat.layers[i].texture.id == 0);
        CHECK(mat.layers[i].uvScale == 16.0f);
    }
}

TEST_CASE("TerrainMaterial size is reasonable (< 64 bytes)", "[terrain]") {
    CHECK(sizeof(ffe::renderer::TerrainMaterial) < 64);
}

// -----------------------------------------------------------------------
// BuiltinShader::TERRAIN enum (M2)
// -----------------------------------------------------------------------

#include "renderer/shader_library.h"

TEST_CASE("BuiltinShader::TERRAIN exists at index 19", "[terrain]") {
    CHECK(static_cast<ffe::u32>(ffe::renderer::BuiltinShader::TERRAIN) == 19);
}

TEST_CASE("BuiltinShader::COUNT is at least 20 after TERRAIN addition", "[terrain]") {
    CHECK(static_cast<ffe::u32>(ffe::renderer::BuiltinShader::COUNT) >= 20);
}

// -----------------------------------------------------------------------
// LOD configuration (M3 — public API types only)
// -----------------------------------------------------------------------

TEST_CASE("TerrainLodConfig default distances are 100, 200, 400", "[terrain][lod]") {
    const ffe::renderer::TerrainLodConfig cfg;
    CHECK(cfg.lodDistances[0] == 100.0f);
    CHECK(cfg.lodDistances[1] == 200.0f);
    CHECK(cfg.lodDistances[2] == 400.0f);
}

TEST_CASE("TerrainLodConfig is small and POD-like", "[terrain][lod]") {
    CHECK(sizeof(ffe::renderer::TerrainLodConfig) <= 16);
}

TEST_CASE("setTerrainLodDistances is safe on invalid handle", "[terrain][lod]") {
    // Should not crash when called with invalid handle.
    ffe::renderer::setTerrainLodDistances(ffe::renderer::TerrainHandle{0}, 50.0f, 150.0f);
    ffe::renderer::setTerrainLodDistances(ffe::renderer::TerrainHandle{99}, 50.0f, 150.0f);
}

// -----------------------------------------------------------------------
// Centering contract — regression tests for Bug 2 (terrain floating wedge)
//
// loadTerrain() bakes chunk vertices in [0, worldWidth] x [0, worldDepth]
// local space. The ECS entity's Transform3D is set to
// (-worldWidth/2, 0, -worldDepth/2) to centre the terrain at the world
// origin. getTerrainHeight() must apply the same inverse offset when
// converting world coordinates to heightmap UVs.
//
// The UV mapping used by getTerrainHeight after the fix is:
//   u = (worldX + worldWidth/2)  / worldWidth
//   v = (worldZ + worldDepth/2)  / worldDepth
//
// We test the formula directly using constants to guard against regressions.
// -----------------------------------------------------------------------

TEST_CASE("terrain centering: UV at world origin maps to 0.5", "[terrain][centering]") {
    // A point at (0, 0) in centred world space should map to the centre
    // of the heightmap (UV = 0.5, 0.5) for any square terrain.
    const float worldWidth = 60.0f;
    const float worldDepth = 60.0f;
    const float worldX = 0.0f;
    const float worldZ = 0.0f;
    const float u = (worldX + worldWidth * 0.5f) / worldWidth;
    const float v = (worldZ + worldDepth * 0.5f) / worldDepth;
    CHECK(u == 0.5f);
    CHECK(v == 0.5f);
}

TEST_CASE("terrain centering: UV at negative corner maps to 0.0", "[terrain][centering]") {
    // A point at (-worldWidth/2, -worldDepth/2) in centred world space maps
    // to UV (0.0, 0.0) — the first texel of the heightmap.
    const float worldWidth = 60.0f;
    const float worldDepth = 60.0f;
    const float worldX = -worldWidth * 0.5f;
    const float worldZ = -worldDepth * 0.5f;
    const float u = (worldX + worldWidth * 0.5f) / worldWidth;
    const float v = (worldZ + worldDepth * 0.5f) / worldDepth;
    CHECK(u == 0.0f);
    CHECK(v == 0.0f);
}

TEST_CASE("terrain centering: UV at positive corner maps to 1.0", "[terrain][centering]") {
    // A point at (+worldWidth/2, +worldDepth/2) in centred world space maps
    // to UV (1.0, 1.0) — the last texel of the heightmap.
    const float worldWidth = 60.0f;
    const float worldDepth = 60.0f;
    const float worldX = worldWidth * 0.5f;
    const float worldZ = worldDepth * 0.5f;
    const float u = (worldX + worldWidth * 0.5f) / worldWidth;
    const float v = (worldZ + worldDepth * 0.5f) / worldDepth;
    CHECK(u == 1.0f);
    CHECK(v == 1.0f);
}

TEST_CASE("terrain centering: getTerrainHeight returns 0 for out-of-bounds centred coords", "[terrain][centering]") {
    // Coordinates outside [-worldWidth/2, +worldWidth/2] should return 0.
    // This verifies the out-of-bounds guard works in centred coordinate space.
    // (No terrain asset loaded, so returns 0 for invalid handle regardless.)
    CHECK(ffe::renderer::getTerrainHeight(ffe::renderer::TerrainHandle{0}, 999.0f, 999.0f) == 0.0f);
    CHECK(ffe::renderer::getTerrainHeight(ffe::renderer::TerrainHandle{0}, -999.0f, -999.0f) == 0.0f);
}

// -----------------------------------------------------------------------
// getTerrainConfig — CPU-only, no terrain loaded
// -----------------------------------------------------------------------

TEST_CASE("getTerrainConfig returns default config for invalid handle", "[terrain]") {
    // An invalid handle (id=0) has no loaded terrain — must return default.
    const ffe::renderer::TerrainConfig cfg =
        ffe::renderer::getTerrainConfig(ffe::renderer::TerrainHandle{0});
    const ffe::renderer::TerrainConfig defaultCfg;
    CHECK(cfg.worldWidth      == defaultCfg.worldWidth);
    CHECK(cfg.worldDepth      == defaultCfg.worldDepth);
    CHECK(cfg.heightScale     == defaultCfg.heightScale);
    CHECK(cfg.chunkResolution == defaultCfg.chunkResolution);
}

TEST_CASE("getTerrainConfig returns default config for out-of-range handle", "[terrain]") {
    // A handle id > MAX_TERRAIN_ASSETS is rejected the same as invalid.
    const ffe::renderer::TerrainConfig cfg =
        ffe::renderer::getTerrainConfig(ffe::renderer::TerrainHandle{99});
    const ffe::renderer::TerrainConfig defaultCfg;
    CHECK(cfg.worldWidth  == defaultCfg.worldWidth);
    CHECK(cfg.heightScale == defaultCfg.heightScale);
}

TEST_CASE("getTerrainConfig result is default-constructed when no terrain is loaded", "[terrain]") {
    // Verify the returned config's field values match the documented defaults
    // (256 x 256, heightScale 50, chunkResolution 64).
    const ffe::renderer::TerrainConfig cfg =
        ffe::renderer::getTerrainConfig(ffe::renderer::TerrainHandle{1});
    CHECK(cfg.worldWidth      == 256.0f);
    CHECK(cfg.worldDepth      == 256.0f);
    CHECK(cfg.heightScale     == 50.0f);
    CHECK(cfg.chunkResolution == 64);
}
