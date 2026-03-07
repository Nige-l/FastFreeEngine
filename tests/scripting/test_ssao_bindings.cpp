// test_ssao_bindings.cpp -- Catch2 unit tests for SSAO Lua bindings:
//   ffe.enableSSAO, ffe.disableSSAO, ffe.setSSAOIntensity
//
// Tests verify binding registration, argument validation, and ECS state changes.
// No GL context is needed -- we only test the config struct in the ECS registry.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/ssao.h"

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World, NO SSAOConfig by default.
// ---------------------------------------------------------------------------

struct SSAOBindingFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    SSAOBindingFixture() {
        REQUIRE(engine.init());
        engine.setWorld(&world);
    }
    ~SSAOBindingFixture() { engine.shutdown(); }
};

// Helper: emplace an SSAOConfig with defaults into the fixture's world.
static void emplaceConfig(ffe::World& world) {
    world.registry().ctx().emplace<ffe::renderer::SSAOConfig>();
}

// =============================================================================
// ffe.enableSSAO
// =============================================================================

TEST_CASE("enableSSAO creates config and enables with defaults",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;
    CHECK(fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>() == nullptr);

    REQUIRE(fix.engine.doString("ffe.enableSSAO()"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>();
    REQUIRE(cfg != nullptr);
    CHECK(cfg->enabled == true);
    CHECK(cfg->radius == Catch::Approx(0.5f));
    CHECK(cfg->bias == Catch::Approx(0.025f));
    CHECK(cfg->sampleCount == 32);
}

TEST_CASE("enableSSAO accepts custom parameters",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;

    REQUIRE(fix.engine.doString("ffe.enableSSAO(1.0, 0.05, 64)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>();
    REQUIRE(cfg != nullptr);
    CHECK(cfg->enabled == true);
    CHECK(cfg->radius == Catch::Approx(1.0f));
    CHECK(cfg->bias == Catch::Approx(0.05f));
    CHECK(cfg->sampleCount == 64);
}

TEST_CASE("enableSSAO clamps invalid sample count",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;

    REQUIRE(fix.engine.doString("ffe.enableSSAO(0.5, 0.025, 100)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>();
    REQUIRE(cfg != nullptr);
    CHECK(cfg->sampleCount == 64); // clamped from 100
}

TEST_CASE("enableSSAO rejects NaN radius",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;
    emplaceConfig(fix.world);

    REQUIRE(fix.engine.doString("ffe.enableSSAO(0/0, 0.025, 32)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>();
    CHECK(cfg->enabled == false); // unchanged from default
}

// =============================================================================
// ffe.disableSSAO
// =============================================================================

TEST_CASE("disableSSAO clears enabled flag",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;
    emplaceConfig(fix.world);
    auto* cfg = fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>();
    cfg->enabled = true;

    REQUIRE(fix.engine.doString("ffe.disableSSAO()"));

    CHECK(cfg->enabled == false);
}

TEST_CASE("disableSSAO is safe when no config exists",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.disableSSAO()"));
    // No crash, no config created.
    CHECK(fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>() == nullptr);
}

// =============================================================================
// ffe.setSSAOIntensity
// =============================================================================

TEST_CASE("setSSAOIntensity updates intensity",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;
    emplaceConfig(fix.world);

    REQUIRE(fix.engine.doString("ffe.setSSAOIntensity(2.5)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>();
    CHECK(cfg->intensity == Catch::Approx(2.5f));
}

TEST_CASE("setSSAOIntensity clamps to max 5.0",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;
    emplaceConfig(fix.world);

    REQUIRE(fix.engine.doString("ffe.setSSAOIntensity(10.0)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>();
    CHECK(cfg->intensity == Catch::Approx(5.0f));
}

TEST_CASE("setSSAOIntensity is no-op when no config exists",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setSSAOIntensity(2.0)"));
    // No crash, no config created.
    CHECK(fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>() == nullptr);
}

TEST_CASE("setSSAOIntensity rejects NaN",
          "[scripting][ssao]") {
    SSAOBindingFixture fix;
    emplaceConfig(fix.world);
    auto* cfg = fix.world.registry().ctx().find<ffe::renderer::SSAOConfig>();
    cfg->intensity = 1.5f;

    REQUIRE(fix.engine.doString("ffe.setSSAOIntensity(0/0)"));

    CHECK(cfg->intensity == Catch::Approx(1.5f)); // unchanged
}
