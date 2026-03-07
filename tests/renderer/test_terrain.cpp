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
