// test_anti_aliasing.cpp -- Catch2 unit tests for anti-aliasing (MSAA + FXAA).
//
// All tests are CPU-only (no GL context required) — they validate the
// PostProcessConfig AA fields, MSAA sample clamping, FXAA luma calculation,
// and AA mode enum validation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "renderer/post_process.h"
#include "core/types.h"

#include <cmath>

using namespace ffe;
using namespace ffe::renderer;

// ===========================================================================
// PostProcessConfig AA default values
// ===========================================================================

TEST_CASE("AntiAliasing: default aaMode is 0 (none)", "[renderer][antialiasing]") {
    const PostProcessConfig cfg;
    CHECK(cfg.aaMode == 0);
}

TEST_CASE("AntiAliasing: default msaaSamples is 2", "[renderer][antialiasing]") {
    const PostProcessConfig cfg;
    CHECK(cfg.msaaSamples == 2);
}

TEST_CASE("AntiAliasing: aaMode can be set to 1 (MSAA)", "[renderer][antialiasing]") {
    PostProcessConfig cfg;
    cfg.aaMode = 1;
    CHECK(cfg.aaMode == 1);
}

TEST_CASE("AntiAliasing: aaMode can be set to 2 (FXAA)", "[renderer][antialiasing]") {
    PostProcessConfig cfg;
    cfg.aaMode = 2;
    CHECK(cfg.aaMode == 2);
}

// ===========================================================================
// MSAA sample count clamping
// ===========================================================================

TEST_CASE("clampMsaaSamples: 1 -> 2", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(1) == 2);
}

TEST_CASE("clampMsaaSamples: 2 -> 2", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(2) == 2);
}

TEST_CASE("clampMsaaSamples: 3 -> 2 (rounds down)", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(3) == 2);
}

TEST_CASE("clampMsaaSamples: 4 -> 4", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(4) == 4);
}

TEST_CASE("clampMsaaSamples: 5 -> 4 (rounds down)", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(5) == 4);
}

TEST_CASE("clampMsaaSamples: 8 -> 8", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(8) == 8);
}

TEST_CASE("clampMsaaSamples: 9 -> 8", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(9) == 8);
}

TEST_CASE("clampMsaaSamples: 16 -> 8", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(16) == 8);
}

TEST_CASE("clampMsaaSamples: 0 -> 2", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(0) == 2);
}

TEST_CASE("clampMsaaSamples: negative -> 2", "[renderer][antialiasing]") {
    CHECK(clampMsaaSamples(-1) == 2);
}

// ===========================================================================
// FXAA luma calculation
// ===========================================================================

TEST_CASE("fxaaLuma: black is 0", "[renderer][antialiasing]") {
    CHECK_THAT(fxaaLuma(0.0f, 0.0f, 0.0f), Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("fxaaLuma: white is approx 1.0", "[renderer][antialiasing]") {
    const f32 lum = fxaaLuma(1.0f, 1.0f, 1.0f);
    // 0.299 + 0.587 + 0.114 = 1.0
    CHECK_THAT(lum, Catch::Matchers::WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("fxaaLuma: green contributes most", "[renderer][antialiasing]") {
    const f32 lumR = fxaaLuma(1.0f, 0.0f, 0.0f);
    const f32 lumG = fxaaLuma(0.0f, 1.0f, 0.0f);
    const f32 lumB = fxaaLuma(0.0f, 0.0f, 1.0f);
    CHECK(lumG > lumR);
    CHECK(lumG > lumB);
    CHECK(lumR > lumB);
}

TEST_CASE("fxaaLuma: known value with BT.601 weights", "[renderer][antialiasing]") {
    const f32 lum = fxaaLuma(0.5f, 0.3f, 0.8f);
    const f32 expected = 0.299f * 0.5f + 0.587f * 0.3f + 0.114f * 0.8f;
    CHECK_THAT(lum, Catch::Matchers::WithinAbs(expected, 1e-6f));
}

TEST_CASE("fxaaLuma: uses different weights than luminance (BT.709)", "[renderer][antialiasing]") {
    // fxaaLuma uses BT.601 (0.299, 0.587, 0.114)
    // luminance uses BT.709 (0.2126, 0.7152, 0.0722)
    // For pure red, fxaaLuma should return higher value
    const f32 fxaaR = fxaaLuma(1.0f, 0.0f, 0.0f);
    const f32 bt709R = luminance(1.0f, 0.0f, 0.0f);
    CHECK(fxaaR > bt709R);
}

// ===========================================================================
// AA mode enum validation
// ===========================================================================

TEST_CASE("AntiAliasing: mode 0 means no anti-aliasing", "[renderer][antialiasing]") {
    PostProcessConfig cfg;
    cfg.aaMode = 0;
    CHECK(cfg.aaMode == 0);
    // Other post-process settings should be independent
    cfg.bloomEnabled = true;
    CHECK(cfg.aaMode == 0);
}

TEST_CASE("AntiAliasing: msaaSamples field is independent of aaMode", "[renderer][antialiasing]") {
    PostProcessConfig cfg;
    cfg.msaaSamples = 4;
    cfg.aaMode = 2; // FXAA — msaaSamples should still be stored
    CHECK(cfg.msaaSamples == 4);
}

TEST_CASE("AntiAliasing: config fields coexist with bloom settings", "[renderer][antialiasing]") {
    PostProcessConfig cfg;
    cfg.bloomEnabled   = true;
    cfg.bloomThreshold = 0.8f;
    cfg.toneMapper     = 2;
    cfg.aaMode         = 1;
    cfg.msaaSamples    = 4;

    CHECK(cfg.bloomEnabled == true);
    CHECK_THAT(cfg.bloomThreshold, Catch::Matchers::WithinAbs(0.8f, 1e-6f));
    CHECK(cfg.toneMapper == 2);
    CHECK(cfg.aaMode == 1);
    CHECK(cfg.msaaSamples == 4);
}
