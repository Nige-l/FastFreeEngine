// test_shadow_map.cpp — Unit tests for the shadow map subsystem.
//
// Tests ShadowConfig defaults, computeLightSpaceMatrix correctness,
// and destroyShadowMap safety. GL-dependent tests (createShadowMap) are
// not included here because the test executable runs headless (no GL context);
// createShadowMap returns {0,0,0} in headless mode, which is itself tested.

#include <catch2/catch_test_macros.hpp>
#include "renderer/shadow_map.h"

#include <cmath>

TEST_CASE("ShadowConfig has sane defaults", "[shadow]") {
    const ffe::ShadowConfig cfg;
    REQUIRE(cfg.resolution == 1024);
    REQUIRE(cfg.bias > 0.0f);
    REQUIRE(cfg.bias < 0.1f);
    REQUIRE(cfg.areaWidth > 0.0f);
    REQUIRE(cfg.areaHeight > 0.0f);
    REQUIRE(cfg.nearPlane > 0.0f);
    REQUIRE(cfg.nearPlane < cfg.farPlane);
    REQUIRE(cfg.enabled == false);
}

TEST_CASE("computeLightSpaceMatrix produces finite matrix", "[shadow]") {
    const ffe::ShadowConfig cfg;
    const glm::mat4 mat = ffe::computeLightSpaceMatrix(glm::vec3(1.0f, -1.0f, 0.5f), cfg);

    // Verify all elements are finite (no NaN, no Inf).
    bool allFinite = true;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(mat[c][r])) {
                allFinite = false;
            }
        }
    }
    REQUIRE(allFinite);
    REQUIRE(mat != glm::mat4(0.0f));
}

TEST_CASE("computeLightSpaceMatrix handles zero direction gracefully", "[shadow]") {
    const ffe::ShadowConfig cfg;
    // Zero vector should trigger the fallback inside computeLightSpaceMatrix.
    const glm::mat4 mat = ffe::computeLightSpaceMatrix(glm::vec3(0.0f, 0.0f, 0.0f), cfg);

    bool allFinite = true;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(mat[c][r])) {
                allFinite = false;
            }
        }
    }
    REQUIRE(allFinite);
    REQUIRE(mat != glm::mat4(0.0f));
}

TEST_CASE("computeLightSpaceMatrix varies with different directions", "[shadow]") {
    const ffe::ShadowConfig cfg;
    const glm::mat4 matA = ffe::computeLightSpaceMatrix(glm::vec3(1.0f, -1.0f, 0.0f), cfg);
    const glm::mat4 matB = ffe::computeLightSpaceMatrix(glm::vec3(0.0f, -1.0f, 1.0f), cfg);
    REQUIRE(matA != matB);
}

TEST_CASE("computeLightSpaceMatrix varies with different configs", "[shadow]") {
    ffe::ShadowConfig cfgA;
    cfgA.areaWidth  = 10.0f;
    cfgA.areaHeight = 10.0f;

    ffe::ShadowConfig cfgB;
    cfgB.areaWidth  = 40.0f;
    cfgB.areaHeight = 40.0f;

    const glm::vec3 dir{0.5f, -1.0f, 0.3f};
    const glm::mat4 matA = ffe::computeLightSpaceMatrix(dir, cfgA);
    const glm::mat4 matB = ffe::computeLightSpaceMatrix(dir, cfgB);
    REQUIRE(matA != matB);
}

TEST_CASE("destroyShadowMap on zero-init is no-op", "[shadow]") {
    ffe::ShadowMap sm{};
    ffe::destroyShadowMap(sm); // Must not crash
    REQUIRE(sm.fbo == 0);
    REQUIRE(sm.depthTexture == 0);
    REQUIRE(sm.resolution == 0);
}

TEST_CASE("createShadowMap with invalid resolution returns zero", "[shadow]") {
    // In headless mode (no GL context), createShadowMap returns zero for any input.
    // But with resolution <= 0 it should also return zero regardless.
    const ffe::ShadowMap sm0 = ffe::createShadowMap(0);
    REQUIRE(sm0.fbo == 0);
    REQUIRE(sm0.depthTexture == 0);

    const ffe::ShadowMap smNeg = ffe::createShadowMap(-1);
    REQUIRE(smNeg.fbo == 0);
    REQUIRE(smNeg.depthTexture == 0);
}

TEST_CASE("createShadowMap in headless mode returns zero struct", "[shadow]") {
    // No GL context in the test environment — should safely return zero.
    const ffe::ShadowMap sm = ffe::createShadowMap(1024);
    REQUIRE(sm.fbo == 0);
    REQUIRE(sm.depthTexture == 0);
    REQUIRE(sm.resolution == 0);
}

TEST_CASE("ShadowMap zero-initialisation is consistent", "[shadow]") {
    const ffe::ShadowMap sm{};
    REQUIRE(sm.fbo == 0);
    REQUIRE(sm.depthTexture == 0);
    REQUIRE(sm.resolution == 0);
}
