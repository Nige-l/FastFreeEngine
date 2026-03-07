// test_water.cpp -- CPU-only Catch2 unit tests for the water rendering system.
//
// Tests WaterConfig defaults, WaterVertex layout, Water component sizing,
// constants validation, reflection camera Y-flip math, and shader enum.
// No GL context required.

#include <catch2/catch_test_macros.hpp>

#include "renderer/water.h"
#include "renderer/shader_library.h"
#include "renderer/camera.h"

#include <cmath>

// -----------------------------------------------------------------------
// WaterConfig defaults
// -----------------------------------------------------------------------

TEST_CASE("WaterConfig default enabled is false", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK_FALSE(cfg.enabled);
}

TEST_CASE("WaterConfig default waterLevel is 0.0", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK(cfg.waterLevel == 0.0f);
}

TEST_CASE("WaterConfig default shallowColor", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK(cfg.shallowColor.x == 0.1f);
    CHECK(cfg.shallowColor.y == 0.4f);
    CHECK(cfg.shallowColor.z == 0.6f);
    CHECK(cfg.shallowColor.w == 0.6f);
}

TEST_CASE("WaterConfig default deepColor", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK(cfg.deepColor.x == 0.0f);
    CHECK(cfg.deepColor.y == 0.1f);
    CHECK(cfg.deepColor.z == 0.3f);
    CHECK(cfg.deepColor.w == 0.9f);
}

TEST_CASE("WaterConfig default maxDepth is 10.0", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK(cfg.maxDepth == 10.0f);
}

TEST_CASE("WaterConfig default waveSpeed is 0.03", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK(cfg.waveSpeed == 0.03f);
}

TEST_CASE("WaterConfig default waveScale is 0.02", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK(cfg.waveScale == 0.02f);
}

TEST_CASE("WaterConfig default fresnelPower is 2.0", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK(cfg.fresnelPower == 2.0f);
}

TEST_CASE("WaterConfig default fresnelBias is 0.1", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK(cfg.fresnelBias == 0.1f);
}

TEST_CASE("WaterConfig default reflectionDistortion is 0.02", "[water]") {
    const ffe::renderer::WaterConfig cfg;
    CHECK(cfg.reflectionDistortion == 0.02f);
}

// -----------------------------------------------------------------------
// WaterConfig field assignments
// -----------------------------------------------------------------------

TEST_CASE("WaterConfig fields can be modified", "[water]") {
    ffe::renderer::WaterConfig cfg;
    cfg.enabled = true;
    cfg.waterLevel = 5.0f;
    cfg.shallowColor = {0.2f, 0.5f, 0.7f, 0.8f};
    cfg.deepColor = {0.05f, 0.15f, 0.35f, 0.95f};
    cfg.maxDepth = 20.0f;
    cfg.waveSpeed = 0.05f;
    cfg.waveScale = 0.04f;
    cfg.fresnelPower = 3.0f;
    cfg.fresnelBias = 0.2f;
    cfg.reflectionDistortion = 0.05f;

    CHECK(cfg.enabled);
    CHECK(cfg.waterLevel == 5.0f);
    CHECK(cfg.shallowColor.x == 0.2f);
    CHECK(cfg.maxDepth == 20.0f);
    CHECK(cfg.waveSpeed == 0.05f);
    CHECK(cfg.waveScale == 0.04f);
    CHECK(cfg.fresnelPower == 3.0f);
    CHECK(cfg.fresnelBias == 0.2f);
    CHECK(cfg.reflectionDistortion == 0.05f);
}

// -----------------------------------------------------------------------
// Struct sizes
// -----------------------------------------------------------------------

TEST_CASE("Water component is 4 bytes", "[water]") {
    CHECK(sizeof(ffe::renderer::Water) == 4);
}

TEST_CASE("WaterVertex is 20 bytes", "[water]") {
    CHECK(sizeof(ffe::renderer::WaterVertex) == 20);
}

TEST_CASE("WaterConfig is reasonable size (< 128 bytes)", "[water]") {
    CHECK(sizeof(ffe::renderer::WaterConfig) < 128);
}

// -----------------------------------------------------------------------
// Constants validation
// -----------------------------------------------------------------------

TEST_CASE("DEFAULT_WATER_LEVEL is 0.0", "[water]") {
    CHECK(ffe::renderer::DEFAULT_WATER_LEVEL == 0.0f);
}

TEST_CASE("DEFAULT_WAVE_SPEED is 0.03", "[water]") {
    CHECK(ffe::renderer::DEFAULT_WAVE_SPEED == 0.03f);
}

TEST_CASE("DEFAULT_WAVE_SCALE is 0.02", "[water]") {
    CHECK(ffe::renderer::DEFAULT_WAVE_SCALE == 0.02f);
}

TEST_CASE("DEFAULT_FRESNEL_POWER is 2.0", "[water]") {
    CHECK(ffe::renderer::DEFAULT_FRESNEL_POWER == 2.0f);
}

TEST_CASE("DEFAULT_FRESNEL_BIAS is 0.1", "[water]") {
    CHECK(ffe::renderer::DEFAULT_FRESNEL_BIAS == 0.1f);
}

TEST_CASE("DEFAULT_MAX_DEPTH is 10.0", "[water]") {
    CHECK(ffe::renderer::DEFAULT_MAX_DEPTH == 10.0f);
}

TEST_CASE("DEFAULT_REFLECTION_DISTORTION is 0.02", "[water]") {
    CHECK(ffe::renderer::DEFAULT_REFLECTION_DISTORTION == 0.02f);
}

TEST_CASE("DEFAULT_WATER_EXTENT is 1000.0", "[water]") {
    CHECK(ffe::renderer::DEFAULT_WATER_EXTENT == 1000.0f);
}

// -----------------------------------------------------------------------
// Reflection camera Y-flip math
// -----------------------------------------------------------------------

TEST_CASE("computeReflectionCamera flips position.y across water level", "[water]") {
    ffe::renderer::Camera cam;
    cam.position = {0.0f, 10.0f, 0.0f};
    cam.target   = {0.0f, 0.0f, -5.0f};
    cam.up       = {0.0f, 1.0f, 0.0f};

    const ffe::renderer::Camera refl = ffe::renderer::computeReflectionCamera(cam, 5.0f);

    // position.y should be: 2 * 5 - 10 = 0
    CHECK(refl.position.y == 0.0f);
    CHECK(refl.position.x == 0.0f);
    CHECK(refl.position.z == 0.0f);
}

TEST_CASE("computeReflectionCamera flips target.y across water level", "[water]") {
    ffe::renderer::Camera cam;
    cam.position = {0.0f, 8.0f, 0.0f};
    cam.target   = {0.0f, 2.0f, -5.0f};
    cam.up       = {0.0f, 1.0f, 0.0f};

    const ffe::renderer::Camera refl = ffe::renderer::computeReflectionCamera(cam, 3.0f);

    // target.y should be: 2 * 3 - 2 = 4
    CHECK(refl.target.y == 4.0f);
    CHECK(refl.target.z == -5.0f);
}

TEST_CASE("computeReflectionCamera inverts up.y", "[water]") {
    ffe::renderer::Camera cam;
    cam.position = {0.0f, 5.0f, 0.0f};
    cam.target   = {0.0f, 0.0f, -1.0f};
    cam.up       = {0.0f, 1.0f, 0.0f};

    const ffe::renderer::Camera refl = ffe::renderer::computeReflectionCamera(cam, 0.0f);

    CHECK(refl.up.y == -1.0f);
    CHECK(refl.up.x == 0.0f);
    CHECK(refl.up.z == 0.0f);
}

TEST_CASE("computeReflectionCamera preserves XZ components", "[water]") {
    ffe::renderer::Camera cam;
    cam.position = {10.0f, 20.0f, 30.0f};
    cam.target   = {5.0f, 3.0f, 15.0f};
    cam.up       = {0.0f, 1.0f, 0.0f};

    const ffe::renderer::Camera refl = ffe::renderer::computeReflectionCamera(cam, 10.0f);

    CHECK(refl.position.x == 10.0f);
    CHECK(refl.position.z == 30.0f);
    CHECK(refl.target.x == 5.0f);
    CHECK(refl.target.z == 15.0f);
}

TEST_CASE("computeReflectionCamera at water level is identity for Y", "[water]") {
    ffe::renderer::Camera cam;
    cam.position = {0.0f, 5.0f, 0.0f};
    cam.target   = {0.0f, 5.0f, -1.0f};
    cam.up       = {0.0f, 1.0f, 0.0f};

    // Water level at same height as camera
    const ffe::renderer::Camera refl = ffe::renderer::computeReflectionCamera(cam, 5.0f);

    // 2 * 5 - 5 = 5 (unchanged)
    CHECK(refl.position.y == 5.0f);
    CHECK(refl.target.y == 5.0f);
}

TEST_CASE("computeReflectionCamera with negative water level", "[water]") {
    ffe::renderer::Camera cam;
    cam.position = {0.0f, 0.0f, 0.0f};
    cam.target   = {0.0f, -2.0f, -1.0f};
    cam.up       = {0.0f, 1.0f, 0.0f};

    const ffe::renderer::Camera refl = ffe::renderer::computeReflectionCamera(cam, -5.0f);

    // position.y: 2 * (-5) - 0 = -10
    CHECK(refl.position.y == -10.0f);
    // target.y: 2 * (-5) - (-2) = -8
    CHECK(refl.target.y == -8.0f);
}

// -----------------------------------------------------------------------
// Water quad vertex layout
// -----------------------------------------------------------------------

TEST_CASE("WaterVertex position offset is 0", "[water]") {
    CHECK(offsetof(ffe::renderer::WaterVertex, position) == 0);
}

TEST_CASE("WaterVertex texCoord offset is 12", "[water]") {
    CHECK(offsetof(ffe::renderer::WaterVertex, texCoord) == 12);
}

// -----------------------------------------------------------------------
// BuiltinShader::WATER enum
// -----------------------------------------------------------------------

TEST_CASE("BuiltinShader::WATER exists at index 20", "[water]") {
    CHECK(static_cast<ffe::u32>(ffe::renderer::BuiltinShader::WATER) == 20);
}

TEST_CASE("BuiltinShader::COUNT is 21 after WATER addition", "[water]") {
    CHECK(static_cast<ffe::u32>(ffe::renderer::BuiltinShader::COUNT) == 21);
}
