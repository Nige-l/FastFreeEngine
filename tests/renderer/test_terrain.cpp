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

TEST_CASE("BuiltinShader::COUNT is 20 after TERRAIN addition", "[terrain]") {
    CHECK(static_cast<ffe::u32>(ffe::renderer::BuiltinShader::COUNT) == 20);
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
