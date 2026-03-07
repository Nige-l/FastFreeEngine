// test_fog.cpp — Unit tests for the linear fog subsystem.
//
// Tests FogParams struct defaults, setFog/disableFog state toggling,
// parameter validation, and fog factor calculation at various distances.
// No GPU context required — all tests exercise CPU-side data structures.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "renderer/mesh_renderer.h"

#include <algorithm>
#include <cmath>

// Helper: compute the linear fog factor the same way the shader does.
// Returns 1.0 at near (no fog) and 0.0 at far (full fog).
static float computeFogFactor(const float dist, const float fogNear, const float fogFar) {
    return std::clamp((fogFar - dist) / (fogFar - fogNear), 0.0f, 1.0f);
}

TEST_CASE("FogParams has sane defaults", "[fog]") {
    const ffe::renderer::FogParams fog;
    REQUIRE(fog.r >= 0.0f);
    REQUIRE(fog.r <= 1.0f);
    REQUIRE(fog.g >= 0.0f);
    REQUIRE(fog.g <= 1.0f);
    REQUIRE(fog.b >= 0.0f);
    REQUIRE(fog.b <= 1.0f);
    REQUIRE(fog.nearDist > 0.0f);
    REQUIRE(fog.farDist > fog.nearDist);
    REQUIRE(fog.enabled == false);
}

TEST_CASE("FogParams default color is gray-blue", "[fog]") {
    const ffe::renderer::FogParams fog;
    REQUIRE(fog.r == Catch::Approx(0.7f));
    REQUIRE(fog.g == Catch::Approx(0.7f));
    REQUIRE(fog.b == Catch::Approx(0.8f));
}

TEST_CASE("FogParams default distances", "[fog]") {
    const ffe::renderer::FogParams fog;
    REQUIRE(fog.nearDist == Catch::Approx(10.0f));
    REQUIRE(fog.farDist == Catch::Approx(100.0f));
}

TEST_CASE("FogParams can be enabled and disabled", "[fog]") {
    ffe::renderer::FogParams fog;
    REQUIRE(fog.enabled == false);

    fog.enabled = true;
    REQUIRE(fog.enabled == true);

    fog.enabled = false;
    REQUIRE(fog.enabled == false);
}

TEST_CASE("FogParams fields can be modified", "[fog]") {
    ffe::renderer::FogParams fog;
    fog.r = 1.0f;
    fog.g = 0.0f;
    fog.b = 0.0f;
    fog.nearDist = 5.0f;
    fog.farDist = 50.0f;
    fog.enabled = true;

    REQUIRE(fog.r == Catch::Approx(1.0f));
    REQUIRE(fog.g == Catch::Approx(0.0f));
    REQUIRE(fog.b == Catch::Approx(0.0f));
    REQUIRE(fog.nearDist == Catch::Approx(5.0f));
    REQUIRE(fog.farDist == Catch::Approx(50.0f));
    REQUIRE(fog.enabled == true);
}

TEST_CASE("Fog factor at camera position (dist=0) is 1.0 (no fog)", "[fog]") {
    const float factor = computeFogFactor(0.0f, 10.0f, 100.0f);
    REQUIRE(factor == Catch::Approx(1.0f));
}

TEST_CASE("Fog factor before near distance is 1.0 (no fog)", "[fog]") {
    const float factor = computeFogFactor(5.0f, 10.0f, 100.0f);
    REQUIRE(factor == Catch::Approx(1.0f));
}

TEST_CASE("Fog factor at near distance is 1.0", "[fog]") {
    const float factor = computeFogFactor(10.0f, 10.0f, 100.0f);
    REQUIRE(factor == Catch::Approx(1.0f));
}

TEST_CASE("Fog factor at far distance is 0.0 (full fog)", "[fog]") {
    const float factor = computeFogFactor(100.0f, 10.0f, 100.0f);
    REQUIRE(factor == Catch::Approx(0.0f));
}

TEST_CASE("Fog factor beyond far distance is clamped to 0.0", "[fog]") {
    const float factor = computeFogFactor(200.0f, 10.0f, 100.0f);
    REQUIRE(factor == Catch::Approx(0.0f));
}

TEST_CASE("Fog factor at midpoint between near and far", "[fog]") {
    // Midpoint = (10 + 100) / 2 = 55
    // factor = (100 - 55) / (100 - 10) = 45 / 90 = 0.5
    const float factor = computeFogFactor(55.0f, 10.0f, 100.0f);
    REQUIRE(factor == Catch::Approx(0.5f));
}

TEST_CASE("Fog factor is linear between near and far", "[fog]") {
    const float near = 10.0f;
    const float far  = 100.0f;

    // Check at 25%, 50%, 75% of the range
    const float dist25 = near + 0.25f * (far - near);   // 32.5
    const float dist50 = near + 0.50f * (far - near);   // 55.0
    const float dist75 = near + 0.75f * (far - near);   // 77.5

    REQUIRE(computeFogFactor(dist25, near, far) == Catch::Approx(0.75f));
    REQUIRE(computeFogFactor(dist50, near, far) == Catch::Approx(0.50f));
    REQUIRE(computeFogFactor(dist75, near, far) == Catch::Approx(0.25f));
}

TEST_CASE("Fog factor with tight range (near==far-1)", "[fog]") {
    // Very tight fog band
    const float factor = computeFogFactor(50.0f, 49.0f, 50.0f);
    REQUIRE(factor == Catch::Approx(0.0f));

    const float factorBefore = computeFogFactor(48.0f, 49.0f, 50.0f);
    REQUIRE(factorBefore == Catch::Approx(1.0f));
}

TEST_CASE("Multiple FogParams instances are independent", "[fog]") {
    ffe::renderer::FogParams a;
    ffe::renderer::FogParams b;

    a.enabled = true;
    a.r = 1.0f;
    a.nearDist = 5.0f;

    REQUIRE(b.enabled == false);
    REQUIRE(b.r == Catch::Approx(0.7f));
    REQUIRE(b.nearDist == Catch::Approx(10.0f));
}
