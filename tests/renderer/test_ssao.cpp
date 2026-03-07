// test_ssao.cpp -- Catch2 unit tests for Screen-Space Ambient Occlusion.
//
// All tests are CPU-only (no GL context required) — they validate the
// SSAOConfig defaults, sample count clamping, radius/bias/intensity
// validation, and config struct properties.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "renderer/ssao.h"
#include "core/types.h"

using namespace ffe;
using namespace ffe::renderer;

// ===========================================================================
// SSAOConfig default values
// ===========================================================================

TEST_CASE("SSAO: default config is disabled", "[renderer][ssao]") {
    const SSAOConfig cfg;
    CHECK(cfg.enabled == false);
}

TEST_CASE("SSAO: default radius is 0.5", "[renderer][ssao]") {
    const SSAOConfig cfg;
    CHECK_THAT(cfg.radius, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("SSAO: default bias is 0.025", "[renderer][ssao]") {
    const SSAOConfig cfg;
    CHECK_THAT(cfg.bias, Catch::Matchers::WithinAbs(0.025f, 1e-6f));
}

TEST_CASE("SSAO: default sampleCount is 32", "[renderer][ssao]") {
    const SSAOConfig cfg;
    CHECK(cfg.sampleCount == 32);
}

TEST_CASE("SSAO: default intensity is 1.0", "[renderer][ssao]") {
    const SSAOConfig cfg;
    CHECK_THAT(cfg.intensity, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
}

// ===========================================================================
// Sample count clamping
// ===========================================================================

TEST_CASE("clampSSAOSamples: 8 -> 16", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(8) == 16);
}

TEST_CASE("clampSSAOSamples: 16 -> 16", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(16) == 16);
}

TEST_CASE("clampSSAOSamples: 17 -> 32", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(17) == 32);
}

TEST_CASE("clampSSAOSamples: 32 -> 32", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(32) == 32);
}

TEST_CASE("clampSSAOSamples: 48 -> 32", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(48) == 32);
}

TEST_CASE("clampSSAOSamples: 49 -> 64", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(49) == 64);
}

TEST_CASE("clampSSAOSamples: 64 -> 64", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(64) == 64);
}

TEST_CASE("clampSSAOSamples: 128 -> 64", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(128) == 64);
}

TEST_CASE("clampSSAOSamples: 0 -> 16", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(0) == 16);
}

TEST_CASE("clampSSAOSamples: negative -> 16", "[renderer][ssao]") {
    CHECK(clampSSAOSamples(-5) == 16);
}

// ===========================================================================
// Radius clamping
// ===========================================================================

TEST_CASE("clampSSAORadius: 0.5 unchanged", "[renderer][ssao]") {
    CHECK_THAT(clampSSAORadius(0.5f), Catch::Matchers::WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("clampSSAORadius: negative -> 0.01", "[renderer][ssao]") {
    CHECK_THAT(clampSSAORadius(-1.0f), Catch::Matchers::WithinAbs(0.01f, 1e-6f));
}

TEST_CASE("clampSSAORadius: zero -> 0.01", "[renderer][ssao]") {
    CHECK_THAT(clampSSAORadius(0.0f), Catch::Matchers::WithinAbs(0.01f, 1e-6f));
}

TEST_CASE("clampSSAORadius: 10.0 -> 5.0", "[renderer][ssao]") {
    CHECK_THAT(clampSSAORadius(10.0f), Catch::Matchers::WithinAbs(5.0f, 1e-6f));
}

// ===========================================================================
// Bias clamping
// ===========================================================================

TEST_CASE("clampSSAOBias: 0.025 unchanged", "[renderer][ssao]") {
    CHECK_THAT(clampSSAOBias(0.025f), Catch::Matchers::WithinAbs(0.025f, 1e-6f));
}

TEST_CASE("clampSSAOBias: negative -> 0.0", "[renderer][ssao]") {
    CHECK_THAT(clampSSAOBias(-0.1f), Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("clampSSAOBias: 1.0 -> 0.5", "[renderer][ssao]") {
    CHECK_THAT(clampSSAOBias(1.0f), Catch::Matchers::WithinAbs(0.5f, 1e-6f));
}

// ===========================================================================
// Intensity clamping
// ===========================================================================

TEST_CASE("clampSSAOIntensity: 1.0 unchanged", "[renderer][ssao]") {
    CHECK_THAT(clampSSAOIntensity(1.0f), Catch::Matchers::WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("clampSSAOIntensity: negative -> 0.0", "[renderer][ssao]") {
    CHECK_THAT(clampSSAOIntensity(-2.0f), Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("clampSSAOIntensity: 10.0 -> 5.0", "[renderer][ssao]") {
    CHECK_THAT(clampSSAOIntensity(10.0f), Catch::Matchers::WithinAbs(5.0f, 1e-6f));
}

// ===========================================================================
// Config field independence
// ===========================================================================

TEST_CASE("SSAO: config fields are independent", "[renderer][ssao]") {
    SSAOConfig cfg;
    cfg.enabled     = true;
    cfg.radius      = 1.0f;
    cfg.bias        = 0.05f;
    cfg.sampleCount = 64;
    cfg.intensity   = 2.0f;

    CHECK(cfg.enabled == true);
    CHECK_THAT(cfg.radius, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    CHECK_THAT(cfg.bias, Catch::Matchers::WithinAbs(0.05f, 1e-6f));
    CHECK(cfg.sampleCount == 64);
    CHECK_THAT(cfg.intensity, Catch::Matchers::WithinAbs(2.0f, 1e-6f));
}
