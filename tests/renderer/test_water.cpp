// test_water.cpp -- CPU-only Catch2 unit tests for the water rendering system.
//
// Tests WaterConfig defaults, WaterVertex layout, Water component sizing,
// constants validation, reflection camera Y-flip math, and shader enum.
// Also tests Phase 9 M6 WaterManager types (WaterHandle, WaterPlane, WaterConfig
// defaults from water.h) — all CPU-only, no GL context required.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

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

TEST_CASE("BuiltinShader::COUNT is 22 after VEGETATION addition", "[water]") {
    CHECK(static_cast<ffe::u32>(ffe::renderer::BuiltinShader::COUNT) == 22);
}

// -----------------------------------------------------------------------
// Phase 9 M6 — WaterManager types (water.h)
// CPU-only tests. No GL context required.
// These test WaterHandle, WaterPlane, WaterConfig from the WaterManager API.
// -----------------------------------------------------------------------

TEST_CASE("WaterHandle default id is 0", "[water][wm]") {
    const ffe::renderer::WaterHandle h;
    CHECK(h.id == 0u);
}

TEST_CASE("WaterHandle default isValid returns false", "[water][wm]") {
    const ffe::renderer::WaterHandle h;
    CHECK_FALSE(h.isValid());
}

TEST_CASE("WaterHandle with id 0 isValid returns false", "[water][wm]") {
    const ffe::renderer::WaterHandle h{0u};
    CHECK_FALSE(h.isValid());
}

TEST_CASE("WaterHandle with id 1 isValid returns true", "[water][wm]") {
    const ffe::renderer::WaterHandle h{1u};
    CHECK(h.isValid());
}

TEST_CASE("WaterHandle is 4 bytes", "[water][wm]") {
    STATIC_CHECK(sizeof(ffe::renderer::WaterHandle) == 4u);
}

TEST_CASE("WaterPlane is 20 bytes", "[water][wm]") {
    STATIC_CHECK(sizeof(ffe::renderer::WaterPlane) == 20u);
}

TEST_CASE("WaterPlane width and depth are independent fields", "[water][wm]") {
    ffe::renderer::WaterPlane plane{0.0f, 0.0f, 0.0f, 100.0f, 200.0f};
    CHECK(plane.width == 100.0f);
    CHECK(plane.depth == 200.0f);
    // Mutating one does not affect the other.
    plane.width = 50.0f;
    CHECK(plane.width == 50.0f);
    CHECK(plane.depth == 200.0f);
}

TEST_CASE("MAX_WATER_SURFACES is at least 4", "[water][wm]") {
    STATIC_CHECK(ffe::renderer::MAX_WATER_SURFACES >= 4u);
}

TEST_CASE("MAX_WATER_SURFACES is exactly 8", "[water][wm]") {
    STATIC_CHECK(ffe::renderer::MAX_WATER_SURFACES == 8u);
}

// WaterSurfaceConfig (WaterManager variant) defaults
TEST_CASE("WaterManager WaterConfig default waveSpeed is 0.3", "[water][wm]") {
    const ffe::renderer::WaterSurfaceConfig cfg;
    CHECK(cfg.waveSpeed == 0.3f);
}

TEST_CASE("WaterManager WaterConfig default waveScale is 2.0", "[water][wm]") {
    const ffe::renderer::WaterSurfaceConfig cfg;
    CHECK(cfg.waveScale == 2.0f);
}

TEST_CASE("WaterManager WaterConfig default waveAmplitude is 0.05", "[water][wm]") {
    const ffe::renderer::WaterSurfaceConfig cfg;
    CHECK(cfg.waveAmplitude == 0.05f);
}

TEST_CASE("WaterManager WaterConfig default fresnelPower is 3.0", "[water][wm]") {
    const ffe::renderer::WaterSurfaceConfig cfg;
    CHECK(cfg.fresnelPower == 3.0f);
}

TEST_CASE("WaterManager WaterConfig fresnelPower default is positive", "[water][wm]") {
    const ffe::renderer::WaterSurfaceConfig cfg;
    CHECK(cfg.fresnelPower > 0.0f);
}

TEST_CASE("WaterManager WaterConfig default reflectionStrength is 0.6", "[water][wm]") {
    const ffe::renderer::WaterSurfaceConfig cfg;
    CHECK(cfg.reflectionStrength == 0.6f);
}

TEST_CASE("WaterManager WaterConfig default reflectionEnabled is true", "[water][wm]") {
    const ffe::renderer::WaterSurfaceConfig cfg;
    CHECK(cfg.reflectionEnabled);
}

TEST_CASE("WaterManager WaterConfig waterColor and deepColor are distinct by default", "[water][wm]") {
    const ffe::renderer::WaterSurfaceConfig cfg;
    // waterColor defaults to (0.1, 0.4, 0.6); deepColor to (0.02, 0.1, 0.2).
    CHECK(cfg.waterColor != cfg.deepColor);
}

TEST_CASE("WaterManager WaterConfig default waveSpeed accepts zero when assigned", "[water][wm]") {
    ffe::renderer::WaterSurfaceConfig cfg;
    cfg.waveSpeed = 0.0f;
    CHECK(cfg.waveSpeed == 0.0f);
}

TEST_CASE("WaterManager WaterConfig default waveAmplitude accepts zero when assigned", "[water][wm]") {
    ffe::renderer::WaterSurfaceConfig cfg;
    cfg.waveAmplitude = 0.0f;
    CHECK(cfg.waveAmplitude == 0.0f);
}

// Fresnel Schlick approximation — pure arithmetic, no GPU required.
// F(cosTheta) = F0 + (1 - F0) * (1 - cosTheta)^5
// Water F0 is approximately 0.02 at normal incidence.

TEST_CASE("Fresnel Schlick at normal incidence (cosTheta=1) equals F0", "[water][wm]") {
    // F(1) = F0 + (1-F0)*(1-1)^5 = F0 + 0 = F0
    constexpr float F0 = 0.02f;
    constexpr float cosTheta = 1.0f;
    const float oneMinusCos = 1.0f - cosTheta; // = 0
    const float term = oneMinusCos * oneMinusCos * oneMinusCos *
                       oneMinusCos * oneMinusCos;             // 0^5 = 0
    const float fresnel = F0 + (1.0f - F0) * term;
    CHECK(fresnel == Catch::Approx(F0).epsilon(1e-5f));
}

TEST_CASE("Fresnel Schlick at grazing angle (cosTheta~0) approaches 1.0", "[water][wm]") {
    // F(0) = F0 + (1-F0)*(1)^5 = F0 + (1-F0) = 1.0
    constexpr float F0 = 0.02f;
    constexpr float cosTheta = 0.0f;
    const float oneMinusCos = 1.0f - cosTheta; // = 1
    const float term = oneMinusCos * oneMinusCos * oneMinusCos *
                       oneMinusCos * oneMinusCos;             // 1^5 = 1
    const float fresnel = F0 + (1.0f - F0) * term;
    CHECK(fresnel == Catch::Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Fresnel Schlick at cosTheta=0.5 is between F0 and 1.0", "[water][wm]") {
    constexpr float F0 = 0.02f;
    constexpr float cosTheta = 0.5f;
    const float oneMinusCos = 1.0f - cosTheta; // = 0.5
    const float term = oneMinusCos * oneMinusCos * oneMinusCos *
                       oneMinusCos * oneMinusCos;             // 0.5^5 = 0.03125
    const float fresnel = F0 + (1.0f - F0) * term;
    CHECK(fresnel > F0);
    CHECK(fresnel < 1.0f);
    CHECK(fresnel == Catch::Approx(F0 + (1.0f - F0) * 0.03125f).epsilon(1e-5f));
}

// UV scroll accumulation — pure arithmetic.
TEST_CASE("UV scroll accumulates correctly: dt=0.5, speed=0.3 -> time=0.15", "[water][wm]") {
    // Simulates one tick of WaterManager::update(dt=0.5) with waveSpeed=0.3.
    // The time accumulator advances by dt each frame; the UV offset = time * waveSpeed.
    float time = 0.0f;
    const float dt = 0.5f;
    const float speed = 0.3f;
    time += dt;
    const float uvScroll = time * speed;
    CHECK(uvScroll == Catch::Approx(0.15f).epsilon(1e-5f));
}

TEST_CASE("UV scroll over multiple ticks is additive", "[water][wm]") {
    float time = 0.0f;
    const float dt = 0.1f;
    const float speed = 1.0f;
    for (int i = 0; i < 10; ++i) { time += dt; }
    // After 10 ticks of dt=0.1: time = 1.0; uvScroll = 1.0 * 1.0 = 1.0
    const float uvScroll = time * speed;
    CHECK(uvScroll == Catch::Approx(1.0f).epsilon(1e-4f));
}
