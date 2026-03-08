// test_terrain_streaming.cpp -- CPU-only Catch2 unit tests for terrain world streaming (M4).
//
// Tests cover the ChunkState enum, ChunkStreamState struct, TerrainStreamingConfig,
// the state-machine transitions exposed by setTerrainStreamingRadius /
// getTerrainLoadedChunkCount, and the dirty-threshold gating logic.
//
// No GL context required. All tests operate on CPU-side data only.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstdlib>

#include "renderer/terrain.h"
#include "renderer/terrain_internal.h"

// -----------------------------------------------------------------------
// ChunkState enum sanity
// -----------------------------------------------------------------------

TEST_CASE("ChunkState enum values are distinct uint8_t values", "[terrain_streaming]") {
    using S = ffe::renderer::ChunkState;
    CHECK(static_cast<uint8_t>(S::EAGER)            == 0);
    CHECK(static_cast<uint8_t>(S::UNLOADED)         == 1);
    CHECK(static_cast<uint8_t>(S::QUEUED)           == 2);
    CHECK(static_cast<uint8_t>(S::GENERATING)       == 3);
    CHECK(static_cast<uint8_t>(S::READY_TO_UPLOAD)  == 4);
    CHECK(static_cast<uint8_t>(S::LOADED)           == 5);
    CHECK(static_cast<uint8_t>(S::UNLOADING)        == 6);
}

// -----------------------------------------------------------------------
// ChunkStreamState default state
// -----------------------------------------------------------------------

TEST_CASE("ChunkStreamState default state is EAGER", "[terrain_streaming]") {
    ffe::renderer::ChunkStreamState cs;
    CHECK(cs.state.load() == ffe::renderer::ChunkState::EAGER);
}

TEST_CASE("ChunkStreamState CPU buffers are empty by default", "[terrain_streaming]") {
    ffe::renderer::ChunkStreamState cs;
    CHECK(cs.cpuVertices.empty());
    CHECK(cs.cpuIndices.empty());
}

// -----------------------------------------------------------------------
// ChunkStreamState atomic transitions (UNLOADED -> QUEUED -> GENERATING -> READY_TO_UPLOAD -> LOADED)
// -----------------------------------------------------------------------

TEST_CASE("ChunkStreamState: CAS UNLOADED -> GENERATING succeeds when expected", "[terrain_streaming]") {
    ffe::renderer::ChunkStreamState cs;
    cs.state.store(ffe::renderer::ChunkState::UNLOADED);

    // Simulate worker CAS: QUEUED -> GENERATING
    cs.state.store(ffe::renderer::ChunkState::QUEUED);
    ffe::renderer::ChunkState expected = ffe::renderer::ChunkState::QUEUED;
    const bool swapped = cs.state.compare_exchange_strong(expected, ffe::renderer::ChunkState::GENERATING);
    CHECK(swapped);
    CHECK(cs.state.load() == ffe::renderer::ChunkState::GENERATING);
}

TEST_CASE("ChunkStreamState: CAS fails when state does not match expected", "[terrain_streaming]") {
    ffe::renderer::ChunkStreamState cs;
    cs.state.store(ffe::renderer::ChunkState::UNLOADED);

    // Worker tries CAS QUEUED -> GENERATING but state is UNLOADED (stale/cancelled)
    ffe::renderer::ChunkState expected = ffe::renderer::ChunkState::QUEUED;
    const bool swapped = cs.state.compare_exchange_strong(expected, ffe::renderer::ChunkState::GENERATING);
    CHECK_FALSE(swapped);
    CHECK(cs.state.load() == ffe::renderer::ChunkState::UNLOADED);
    // expected is updated by failed CAS to the actual value
    CHECK(expected == ffe::renderer::ChunkState::UNLOADED);
}

TEST_CASE("ChunkStreamState: full state-machine sequence via store", "[terrain_streaming]") {
    using S = ffe::renderer::ChunkState;
    ffe::renderer::ChunkStreamState cs;

    // Eager -> Unloaded (streaming enabled)
    cs.state.store(S::UNLOADED);
    CHECK(cs.state.load() == S::UNLOADED);

    // Enqueued for generation
    cs.state.store(S::QUEUED);
    CHECK(cs.state.load() == S::QUEUED);

    // Worker picks it up
    S exp = S::QUEUED;
    CHECK(cs.state.compare_exchange_strong(exp, S::GENERATING));
    CHECK(cs.state.load() == S::GENERATING);

    // Worker finishes
    cs.state.store(S::READY_TO_UPLOAD);
    CHECK(cs.state.load() == S::READY_TO_UPLOAD);

    // Main thread uploads (CAS READY_TO_UPLOAD -> LOADED)
    S exp2 = S::READY_TO_UPLOAD;
    CHECK(cs.state.compare_exchange_strong(exp2, S::LOADED));
    CHECK(cs.state.load() == S::LOADED);

    // Camera moves away -> UNLOADING -> UNLOADED
    cs.state.store(S::UNLOADING);
    CHECK(cs.state.load() == S::UNLOADING);
    cs.state.store(S::UNLOADED);
    CHECK(cs.state.load() == S::UNLOADED);
}

// -----------------------------------------------------------------------
// TerrainStreamingConfig defaults
// -----------------------------------------------------------------------

TEST_CASE("TerrainStreamingConfig defaults: radius=0 (streaming disabled)", "[terrain_streaming]") {
    const ffe::renderer::TerrainStreamingConfig cfg{};
    CHECK(cfg.radiusChunks == 0);
}

TEST_CASE("TerrainStreamingConfig lastCam initialized to sentinel (1e9)", "[terrain_streaming]") {
    const ffe::renderer::TerrainStreamingConfig cfg{};
    CHECK(cfg.lastCamX == 1e9f);
    CHECK(cfg.lastCamZ == 1e9f);
}

TEST_CASE("TerrainStreamingConfig dirtyCamThreshold defaults to 0", "[terrain_streaming]") {
    const ffe::renderer::TerrainStreamingConfig cfg{};
    CHECK(cfg.dirtyCamThreshold == 0.0f);
}

// -----------------------------------------------------------------------
// getTerrainLoadedChunkCount without GL context (no terrain loaded -- returns 0)
// -----------------------------------------------------------------------

TEST_CASE("getTerrainLoadedChunkCount returns 0 for invalid handle", "[terrain_streaming]") {
    CHECK(ffe::renderer::getTerrainLoadedChunkCount(ffe::renderer::TerrainHandle{0}) == 0);
}

TEST_CASE("getTerrainLoadedChunkCount returns 0 for out-of-range handle", "[terrain_streaming]") {
    CHECK(ffe::renderer::getTerrainLoadedChunkCount(ffe::renderer::TerrainHandle{99}) == 0);
}

// -----------------------------------------------------------------------
// setTerrainStreamingRadius with invalid handle (no-op, no crash)
// -----------------------------------------------------------------------

TEST_CASE("setTerrainStreamingRadius with invalid handle is a no-op", "[terrain_streaming]") {
    // Should not crash or assert
    ffe::renderer::setTerrainStreamingRadius(ffe::renderer::TerrainHandle{0}, 4);
    ffe::renderer::setTerrainStreamingRadius(ffe::renderer::TerrainHandle{99}, 4);
    // No return value to check; success = no crash
    CHECK(true);
}

// -----------------------------------------------------------------------
// Cancellation: QUEUED -> UNLOADED (camera moves chunk out of radius before worker picks it up)
// -----------------------------------------------------------------------

TEST_CASE("QUEUED chunk cancelled when camera leaves radius: CAS rejects QUEUED->GENERATING", "[terrain_streaming]") {
    ffe::renderer::ChunkStreamState cs;
    // Chunk was enqueued
    cs.state.store(ffe::renderer::ChunkState::QUEUED);

    // Main thread (streaming tick) cancels it by reverting to UNLOADED
    cs.state.store(ffe::renderer::ChunkState::UNLOADED);

    // Worker thread arrives and tries CAS QUEUED -> GENERATING -- should fail
    ffe::renderer::ChunkState expected = ffe::renderer::ChunkState::QUEUED;
    const bool swapped = cs.state.compare_exchange_strong(expected, ffe::renderer::ChunkState::GENERATING);
    CHECK_FALSE(swapped);
    CHECK(cs.state.load() == ffe::renderer::ChunkState::UNLOADED);
}

// -----------------------------------------------------------------------
// EAGER chunks: never touched by streaming logic
// -----------------------------------------------------------------------

TEST_CASE("EAGER state is not equal to LOADED or UNLOADED", "[terrain_streaming]") {
    using S = ffe::renderer::ChunkState;
    CHECK(S::EAGER != S::LOADED);
    CHECK(S::EAGER != S::UNLOADED);
}

TEST_CASE("Only EAGER and LOADED count as drawable chunk states", "[terrain_streaming]") {
    using S = ffe::renderer::ChunkState;
    // Simulate the draw-loop gate: state == EAGER || state == LOADED
    auto isDrawable = [](S s) -> bool {
        return s == S::EAGER || s == S::LOADED;
    };
    CHECK(isDrawable(S::EAGER));
    CHECK(isDrawable(S::LOADED));
    CHECK_FALSE(isDrawable(S::UNLOADED));
    CHECK_FALSE(isDrawable(S::QUEUED));
    CHECK_FALSE(isDrawable(S::GENERATING));
    CHECK_FALSE(isDrawable(S::READY_TO_UPLOAD));
    CHECK_FALSE(isDrawable(S::UNLOADING));
}

// -----------------------------------------------------------------------
// Dirty threshold gate logic (pure math, no terrain required)
// -----------------------------------------------------------------------

TEST_CASE("Dirty threshold: camera inside threshold does not trigger tick", "[terrain_streaming]") {
    // Replicate the gate: dist2 < thresh * thresh => skip
    const float lastX = 0.0f;
    const float lastZ = 0.0f;
    const float thresh = 8.0f;  // 0.5 * chunkWorldSize = 8 (chunk = 16 world units)

    // Camera moved 3 units -- below threshold (3 < 8)
    const float camX = 3.0f;
    const float camZ = 0.0f;
    const float dx = camX - lastX;
    const float dz = camZ - lastZ;
    const float dist2 = dx * dx + dz * dz;
    CHECK(dist2 < thresh * thresh);
}

TEST_CASE("Dirty threshold: camera beyond threshold triggers tick", "[terrain_streaming]") {
    const float lastX = 0.0f;
    const float lastZ = 0.0f;
    const float thresh = 8.0f;

    // Camera moved 10 units -- beyond threshold
    const float camX = 10.0f;
    const float camZ = 0.0f;
    const float dx = camX - lastX;
    const float dz = camZ - lastZ;
    const float dist2 = dx * dx + dz * dz;
    CHECK(dist2 >= thresh * thresh);
}

// -----------------------------------------------------------------------
// Chebyshev distance for radius classification (pure math)
// -----------------------------------------------------------------------

TEST_CASE("Chebyshev distance: chunk at camera position is within radius 0", "[terrain_streaming]") {
    const int camCX = 5;
    const int camCZ = 3;
    const int cx = 5;
    const int cz = 3;
    const int chebyDist = std::max(std::abs(cx - camCX), std::abs(cz - camCZ));
    CHECK(chebyDist == 0);
    CHECK(chebyDist <= 0); // within radius 0
}

TEST_CASE("Chebyshev distance: diagonal neighbour is distance 1", "[terrain_streaming]") {
    const int camCX = 5;
    const int camCZ = 3;
    const int cx = 6;
    const int cz = 4;
    const int chebyDist = std::max(std::abs(cx - camCX), std::abs(cz - camCZ));
    CHECK(chebyDist == 1);
}

TEST_CASE("Chebyshev distance: chunk outside radius is not loaded", "[terrain_streaming]") {
    const int camCX = 5;
    const int camCZ = 3;
    const int radius = 2;

    // A chunk at (10, 3) has chebyDist = 5 -- outside radius 2
    const int cx = 10;
    const int cz = 3;
    const int chebyDist = std::max(std::abs(cx - camCX), std::abs(cz - camCZ));
    CHECK(chebyDist > radius);
}

TEST_CASE("Chebyshev distance: chunk on boundary is within radius", "[terrain_streaming]") {
    const int camCX = 5;
    const int camCZ = 3;
    const int radius = 2;

    // A chunk at (7, 3) has chebyDist = 2 -- exactly on boundary
    const int cx = 7;
    const int cz = 3;
    const int chebyDist = std::max(std::abs(cx - camCX), std::abs(cz - camCZ));
    CHECK(chebyDist <= radius);
}
