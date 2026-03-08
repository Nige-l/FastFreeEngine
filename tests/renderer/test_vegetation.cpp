// test_vegetation.cpp — CPU-only Catch2 unit tests for the vegetation system (Phase 9 M5).
//
// Tests exercise types, constants, and struct layout defined in vegetation.h.
// No OpenGL context is required. No GPU calls (addPatch, renderGrass, addTree,
// clearTrees, init, shutdown) are made — those require a live GL context.
//
// Reference: docs/architecture/adr-phase9-m5-vegetation.md

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>  // offsetof

#include "renderer/vegetation.h"

// Bring types into scope for brevity.
using ffe::renderer::GrassInstance;
using ffe::renderer::MAX_GRASS_INSTANCES;
using ffe::renderer::MAX_TREES;
using ffe::renderer::MAX_VEGETATION_PATCHES;
using ffe::renderer::TreeInstance;
using ffe::renderer::VegetationConfig;
using ffe::renderer::VegetationHandle;
using ffe::renderer::isValid;

// -----------------------------------------------------------------------
// VegetationHandle
// -----------------------------------------------------------------------

TEST_CASE("VegetationHandle default id is 0", "[vegetation]") {
    const VegetationHandle h;
    CHECK(h.id == 0u);
}

TEST_CASE("VegetationHandle default is invalid", "[vegetation]") {
    const VegetationHandle h;
    CHECK_FALSE(isValid(h));
}

TEST_CASE("VegetationHandle with non-zero id is valid", "[vegetation]") {
    const VegetationHandle h{1u};
    CHECK(isValid(h));
}

TEST_CASE("VegetationHandle equality: two handles with same id compare equal", "[vegetation]") {
    const VegetationHandle a{42u};
    const VegetationHandle b{42u};
    // VegetationHandle is a plain struct — compare members directly.
    CHECK(a.id == b.id);
}

TEST_CASE("VegetationHandle inequality: different ids do not compare equal", "[vegetation]") {
    const VegetationHandle a{1u};
    const VegetationHandle b{2u};
    CHECK(a.id != b.id);
}

// -----------------------------------------------------------------------
// GrassInstance — size and layout
// -----------------------------------------------------------------------

TEST_CASE("GrassInstance is 24 bytes", "[vegetation]") {
    STATIC_CHECK(sizeof(GrassInstance) == 24u);
}

TEST_CASE("GrassInstance position field is at offset 0", "[vegetation]") {
    STATIC_CHECK(offsetof(GrassInstance, position) == 0u);
}

TEST_CASE("GrassInstance scale field is at offset 12", "[vegetation]") {
    STATIC_CHECK(offsetof(GrassInstance, scale) == 12u);
}

TEST_CASE("GrassInstance rotation field is at offset 16", "[vegetation]") {
    STATIC_CHECK(offsetof(GrassInstance, rotation) == 16u);
}

TEST_CASE("GrassInstance _pad field is at offset 20", "[vegetation]") {
    STATIC_CHECK(offsetof(GrassInstance, _pad) == 20u);
}

// -----------------------------------------------------------------------
// TreeInstance — size and layout
// -----------------------------------------------------------------------

TEST_CASE("TreeInstance is 16 bytes", "[vegetation]") {
    STATIC_CHECK(sizeof(TreeInstance) == 16u);
}

TEST_CASE("TreeInstance x field is at offset 0", "[vegetation]") {
    STATIC_CHECK(offsetof(TreeInstance, x) == 0u);
}

TEST_CASE("TreeInstance y field is at offset 4", "[vegetation]") {
    STATIC_CHECK(offsetof(TreeInstance, y) == 4u);
}

TEST_CASE("TreeInstance z field is at offset 8", "[vegetation]") {
    STATIC_CHECK(offsetof(TreeInstance, z) == 8u);
}

TEST_CASE("TreeInstance scale field is at offset 12", "[vegetation]") {
    STATIC_CHECK(offsetof(TreeInstance, scale) == 12u);
}

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------

TEST_CASE("MAX_VEGETATION_PATCHES is 32", "[vegetation]") {
    STATIC_CHECK(MAX_VEGETATION_PATCHES == 32u);
}

TEST_CASE("MAX_GRASS_INSTANCES is 256", "[vegetation]") {
    STATIC_CHECK(MAX_GRASS_INSTANCES == 256u);
}

TEST_CASE("MAX_TREES is 512", "[vegetation]") {
    STATIC_CHECK(MAX_TREES == 512u);
}

// -----------------------------------------------------------------------
// VegetationConfig defaults
// -----------------------------------------------------------------------

TEST_CASE("VegetationConfig default density is positive and within [1, 256]", "[vegetation]") {
    const VegetationConfig cfg;
    CHECK(cfg.density >= 1u);
    CHECK(cfg.density <= MAX_GRASS_INSTANCES);
}

TEST_CASE("VegetationConfig default grassHeight is positive", "[vegetation]") {
    const VegetationConfig cfg;
    CHECK(cfg.grassHeight > 0.0f);
}

TEST_CASE("VegetationConfig default grassWidth is positive", "[vegetation]") {
    const VegetationConfig cfg;
    CHECK(cfg.grassWidth > 0.0f);
}

TEST_CASE("VegetationConfig default fadeStartDist is less than fadeEndDist", "[vegetation]") {
    const VegetationConfig cfg;
    CHECK(cfg.fadeStartDist < cfg.fadeEndDist);
}

TEST_CASE("VegetationConfig default fadeStartDist is positive", "[vegetation]") {
    const VegetationConfig cfg;
    CHECK(cfg.fadeStartDist > 0.0f);
}

TEST_CASE("VegetationConfig default fadeEndDist is positive", "[vegetation]") {
    const VegetationConfig cfg;
    CHECK(cfg.fadeEndDist > 0.0f);
}

TEST_CASE("VegetationConfig default textureHandle id is 0 (no texture / solid green fallback)", "[vegetation]") {
    const VegetationConfig cfg;
    CHECK(cfg.textureHandle.id == 0u);
}

TEST_CASE("VegetationConfig density field can be set to 256 without clamping in the struct", "[vegetation]") {
    // The struct itself does not clamp — clamping happens inside addPatch().
    // This test confirms the struct accepts 256 as-is.
    VegetationConfig cfg;
    cfg.density = MAX_GRASS_INSTANCES;
    CHECK(cfg.density == 256u);
}

TEST_CASE("VegetationConfig density field can be set above 256 (clamping is VegetationSystem's responsibility)", "[vegetation]") {
    // The struct is POD — no hidden enforcement. Clamping to [1, 256] happens
    // inside VegetationSystem::addPatch(), not in the struct itself.
    VegetationConfig cfg;
    cfg.density = 300u;
    CHECK(cfg.density == 300u);
}
