// test_post_process.cpp -- Catch2 unit tests for the post-processing pipeline.
//
// All tests are CPU-only (no GL context required) — they validate the
// PostProcessConfig struct, tone mapping math, gamma correction, and
// bloom threshold logic.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "renderer/post_process.h"
#include "core/types.h"

#include <cmath>

using namespace ffe;
using namespace ffe::renderer;

// ===========================================================================
// PostProcessConfig default values
// ===========================================================================

TEST_CASE("PostProcessConfig: default bloomEnabled is false", "[renderer][postprocess]") {
    const PostProcessConfig cfg;
    CHECK(cfg.bloomEnabled == false);
}

TEST_CASE("PostProcessConfig: default bloomThreshold is 1.0", "[renderer][postprocess]") {
    const PostProcessConfig cfg;
    CHECK_THAT(cfg.bloomThreshold, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("PostProcessConfig: default bloomIntensity is 0.5", "[renderer][postprocess]") {
    const PostProcessConfig cfg;
    CHECK_THAT(cfg.bloomIntensity, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("PostProcessConfig: default toneMapper is 0 (none)", "[renderer][postprocess]") {
    const PostProcessConfig cfg;
    CHECK(cfg.toneMapper == 0);
}

TEST_CASE("PostProcessConfig: default gammaEnabled is true", "[renderer][postprocess]") {
    const PostProcessConfig cfg;
    CHECK(cfg.gammaEnabled == true);
}

TEST_CASE("PostProcessConfig: default gamma is 2.2", "[renderer][postprocess]") {
    const PostProcessConfig cfg;
    CHECK_THAT(cfg.gamma, Catch::Matchers::WithinAbs(2.2f, 1e-6f));
}

// ===========================================================================
// PostProcessConfig field modification
// ===========================================================================

TEST_CASE("PostProcessConfig: fields can be set and read back", "[renderer][postprocess]") {
    PostProcessConfig cfg;
    cfg.bloomEnabled   = true;
    cfg.bloomThreshold = 0.8f;
    cfg.bloomIntensity = 0.3f;
    cfg.toneMapper     = 2;
    cfg.gammaEnabled   = false;
    cfg.gamma          = 1.8f;

    CHECK(cfg.bloomEnabled == true);
    CHECK_THAT(cfg.bloomThreshold, Catch::Matchers::WithinAbs(0.8f, 1e-6f));
    CHECK_THAT(cfg.bloomIntensity, Catch::Matchers::WithinAbs(0.3f, 1e-6f));
    CHECK(cfg.toneMapper == 2);
    CHECK(cfg.gammaEnabled == false);
    CHECK_THAT(cfg.gamma, Catch::Matchers::WithinAbs(1.8f, 1e-6f));
}

// ===========================================================================
// Tone mapping: Reinhard
// ===========================================================================

TEST_CASE("tonemapReinhard: maps 0 to 0", "[renderer][postprocess]") {
    f32 r = 0.0f, g = 0.0f, b = 0.0f;
    tonemapReinhard(r, g, b);
    CHECK_THAT(r, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(g, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(b, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("tonemapReinhard: maps 1 to 0.5", "[renderer][postprocess]") {
    f32 r = 1.0f, g = 1.0f, b = 1.0f;
    tonemapReinhard(r, g, b);
    CHECK_THAT(r, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
    CHECK_THAT(g, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
    CHECK_THAT(b, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("tonemapReinhard: large values approach 1.0", "[renderer][postprocess]") {
    f32 r = 100.0f, g = 100.0f, b = 100.0f;
    tonemapReinhard(r, g, b);
    // 100 / (100 + 1) = 0.990099...
    CHECK(r > 0.99f);
    CHECK(r < 1.0f);
}

TEST_CASE("tonemapReinhard: formula c/(1+c) verified per-channel", "[renderer][postprocess]") {
    f32 r = 2.0f, g = 0.5f, b = 4.0f;
    tonemapReinhard(r, g, b);
    CHECK_THAT(r, Catch::Matchers::WithinAbs(2.0f / 3.0f, 1e-6f));
    CHECK_THAT(g, Catch::Matchers::WithinAbs(0.5f / 1.5f, 1e-6f));
    CHECK_THAT(b, Catch::Matchers::WithinAbs(4.0f / 5.0f, 1e-6f));
}

// ===========================================================================
// Tone mapping: ACES
// ===========================================================================

TEST_CASE("tonemapACES: maps 0 to approx 0", "[renderer][postprocess]") {
    f32 r = 0.0f, g = 0.0f, b = 0.0f;
    tonemapACES(r, g, b);
    // ACES(0) = 0.03 / 0.14 = 0.2143 — NOT zero due to the constant terms.
    // Actually let's compute: (0*(2.51*0+0.03))/(0*(2.43*0+0.59)+0.14) = 0/0.14 = 0.
    CHECK_THAT(r, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("tonemapACES: maps 1 to known value", "[renderer][postprocess]") {
    f32 r = 1.0f, g = 1.0f, b = 1.0f;
    tonemapACES(r, g, b);
    // ACES(1) = (1*(2.51+0.03))/(1*(2.43+0.59)+0.14) = 2.54/3.16 = 0.8038...
    const f32 expected = 2.54f / 3.16f;
    CHECK_THAT(r, Catch::Matchers::WithinAbs(expected, 1e-4f));
}

TEST_CASE("tonemapACES: large values approach 2.51/2.43", "[renderer][postprocess]") {
    f32 r = 1000.0f, g = 1000.0f, b = 1000.0f;
    tonemapACES(r, g, b);
    // As x -> inf, ACES -> 2.51/2.43 = 1.0329...
    CHECK(r > 1.02f);
    CHECK(r < 1.04f);
}

// ===========================================================================
// Gamma correction
// ===========================================================================

TEST_CASE("gammaCorrect: pow(1.0, 1/2.2) = 1.0", "[renderer][postprocess]") {
    f32 r = 1.0f, g = 1.0f, b = 1.0f;
    gammaCorrect(r, g, b, 2.2f);
    CHECK_THAT(r, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("gammaCorrect: pow(0.0, 1/2.2) = 0.0", "[renderer][postprocess]") {
    f32 r = 0.0f, g = 0.0f, b = 0.0f;
    gammaCorrect(r, g, b, 2.2f);
    CHECK_THAT(r, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("gammaCorrect: known value pow(0.5, 1/2.2)", "[renderer][postprocess]") {
    f32 r = 0.5f, g = 0.5f, b = 0.5f;
    gammaCorrect(r, g, b, 2.2f);
    const f32 expected = std::pow(0.5f, 1.0f / 2.2f);
    CHECK_THAT(r, Catch::Matchers::WithinAbs(expected, 1e-5f));
}

TEST_CASE("gammaCorrect: gamma 1.0 is identity", "[renderer][postprocess]") {
    f32 r = 0.42f, g = 0.73f, b = 0.15f;
    gammaCorrect(r, g, b, 1.0f);
    CHECK_THAT(r, Catch::Matchers::WithinAbs(0.42f, 1e-5f));
    CHECK_THAT(g, Catch::Matchers::WithinAbs(0.73f, 1e-5f));
    CHECK_THAT(b, Catch::Matchers::WithinAbs(0.15f, 1e-5f));
}

// ===========================================================================
// Luminance calculation (for bloom threshold)
// ===========================================================================

TEST_CASE("luminance: black is 0", "[renderer][postprocess]") {
    CHECK_THAT(luminance(0.0f, 0.0f, 0.0f), Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("luminance: white is approx 1.0", "[renderer][postprocess]") {
    const f32 lum = luminance(1.0f, 1.0f, 1.0f);
    // 0.2126 + 0.7152 + 0.0722 = 1.0
    CHECK_THAT(lum, Catch::Matchers::WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("luminance: green contributes most", "[renderer][postprocess]") {
    // Pure green should have highest luminance among primaries
    const f32 lumR = luminance(1.0f, 0.0f, 0.0f);
    const f32 lumG = luminance(0.0f, 1.0f, 0.0f);
    const f32 lumB = luminance(0.0f, 0.0f, 1.0f);
    CHECK(lumG > lumR);
    CHECK(lumG > lumB);
    CHECK(lumR > lumB);
}

TEST_CASE("luminance: ITU BT.709 weights verified", "[renderer][postprocess]") {
    const f32 lum = luminance(0.5f, 0.3f, 0.8f);
    const f32 expected = 0.2126f * 0.5f + 0.7152f * 0.3f + 0.0722f * 0.8f;
    CHECK_THAT(lum, Catch::Matchers::WithinAbs(expected, 1e-6f));
}

// ===========================================================================
// Pipeline state (headless — no GL context)
// ===========================================================================

TEST_CASE("isPostProcessingInitialised: false before init", "[renderer][postprocess]") {
    // In a test environment without GL, initPostProcessing was never called,
    // so isPostProcessingInitialised should return false.
    CHECK(isPostProcessingInitialised() == false);
}

TEST_CASE("shutdownPostProcessing: safe to call when not initialised", "[renderer][postprocess]") {
    // Should not crash or assert.
    shutdownPostProcessing();
    CHECK(isPostProcessingInitialised() == false);
}

TEST_CASE("resizePostProcessing: no-op when not initialised", "[renderer][postprocess]") {
    // Should not crash or assert.
    resizePostProcessing(1920, 1080);
    CHECK(isPostProcessingInitialised() == false);
}

TEST_CASE("getSceneFBOHandle: returns 0 when not initialised", "[renderer][postprocess]") {
    CHECK(getSceneFBOHandle() == 0u);
}
