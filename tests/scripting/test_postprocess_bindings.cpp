// test_postprocess_bindings.cpp -- Catch2 unit tests for post-processing Lua bindings:
//   ffe.enableBloom, ffe.disableBloom, ffe.setToneMapping,
//   ffe.setGammaCorrection, ffe.enablePostProcessing, ffe.disablePostProcessing
//
// Tests verify binding registration, argument validation, and ECS state changes.
// No GL context is needed -- we only test the config struct in the ECS registry.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/post_process.h"

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World, NO PostProcessConfig by default.
// Tests that need one either call enablePostProcessing or emplace manually.
// ---------------------------------------------------------------------------

struct PostProcessBindingFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    PostProcessBindingFixture() {
        REQUIRE(engine.init());
        engine.setWorld(&world);
    }
    ~PostProcessBindingFixture() { engine.shutdown(); }
};

// Helper: emplace a PostProcessConfig with defaults into the fixture's world.
static void emplaceConfig(ffe::World& world) {
    world.registry().ctx().emplace<ffe::renderer::PostProcessConfig>();
}

// =============================================================================
// ffe.enablePostProcessing / ffe.disablePostProcessing
// =============================================================================

TEST_CASE("enablePostProcessing creates config when absent",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    // No config initially.
    CHECK(fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>() == nullptr);

    REQUIRE(fix.engine.doString("ffe.enablePostProcessing()"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    REQUIRE(cfg != nullptr);
    // Should have default values.
    CHECK(cfg->bloomEnabled == false);
    CHECK(cfg->toneMapper == 0);
    CHECK(cfg->gammaEnabled == true);
}

TEST_CASE("enablePostProcessing is no-op when config already exists",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    emplaceConfig(fix.world);
    auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    cfg->bloomEnabled = true; // mutate to detect overwrites

    REQUIRE(fix.engine.doString("ffe.enablePostProcessing()"));

    cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    REQUIRE(cfg != nullptr);
    CHECK(cfg->bloomEnabled == true); // preserved, not reset
}

TEST_CASE("disablePostProcessing removes config",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    emplaceConfig(fix.world);
    CHECK(fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>() != nullptr);

    REQUIRE(fix.engine.doString("ffe.disablePostProcessing()"));

    CHECK(fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>() == nullptr);
}

TEST_CASE("disablePostProcessing is safe when no config exists",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    CHECK(fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>() == nullptr);
    REQUIRE(fix.engine.doString("ffe.disablePostProcessing()"));
    CHECK(fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>() == nullptr);
}

// =============================================================================
// ffe.enableBloom
// =============================================================================

TEST_CASE("enableBloom sets config values and creates config if absent",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    // No config yet -- enableBloom should create one.
    REQUIRE(fix.engine.doString("ffe.enableBloom(0.8, 1.2)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    REQUIRE(cfg != nullptr);
    CHECK(cfg->bloomEnabled == true);
    CHECK(cfg->bloomThreshold == Catch::Approx(0.8f));
    CHECK(cfg->bloomIntensity == Catch::Approx(1.2f));
}

TEST_CASE("enableBloom uses defaults when no arguments given",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.enableBloom()"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    REQUIRE(cfg != nullptr);
    CHECK(cfg->bloomEnabled == true);
    CHECK(cfg->bloomThreshold == Catch::Approx(1.0f));
    CHECK(cfg->bloomIntensity == Catch::Approx(0.5f));
}

TEST_CASE("enableBloom rejects NaN values",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    emplaceConfig(fix.world);
    REQUIRE(fix.engine.doString("ffe.enableBloom(0/0, 1.0)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    CHECK(cfg->bloomEnabled == false); // unchanged from default
}

// =============================================================================
// ffe.disableBloom
// =============================================================================

TEST_CASE("disableBloom clears bloomEnabled",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    emplaceConfig(fix.world);
    auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    cfg->bloomEnabled = true;

    REQUIRE(fix.engine.doString("ffe.disableBloom()"));

    CHECK(cfg->bloomEnabled == false);
}

TEST_CASE("disableBloom is safe when no config exists",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.disableBloom()"));
    // No crash, no config created.
    CHECK(fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>() == nullptr);
}

// =============================================================================
// ffe.setToneMapping
// =============================================================================

TEST_CASE("setToneMapping sets mode and enables gamma",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    emplaceConfig(fix.world);
    auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    cfg->gammaEnabled = false;

    REQUIRE(fix.engine.doString("ffe.setToneMapping(1)")); // Reinhard

    CHECK(cfg->toneMapper == 1);
    CHECK(cfg->gammaEnabled == true); // auto-enabled
}

TEST_CASE("setToneMapping mode 2 selects ACES",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    emplaceConfig(fix.world);

    REQUIRE(fix.engine.doString("ffe.setToneMapping(2)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    CHECK(cfg->toneMapper == 2);
}

TEST_CASE("setToneMapping mode 0 does not force gamma on",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    emplaceConfig(fix.world);
    auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    cfg->gammaEnabled = false;

    REQUIRE(fix.engine.doString("ffe.setToneMapping(0)")); // none

    CHECK(cfg->toneMapper == 0);
    CHECK(cfg->gammaEnabled == false); // not forced on
}

TEST_CASE("setToneMapping rejects invalid mode",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    emplaceConfig(fix.world);

    REQUIRE(fix.engine.doString("ffe.setToneMapping(5)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    CHECK(cfg->toneMapper == 0); // unchanged
}

TEST_CASE("setToneMapping creates config if absent",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setToneMapping(2)"));

    const auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();
    REQUIRE(cfg != nullptr);
    CHECK(cfg->toneMapper == 2);
}

// =============================================================================
// ffe.setGammaCorrection
// =============================================================================

TEST_CASE("setGammaCorrection toggles gamma flag",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    emplaceConfig(fix.world);
    auto* cfg = fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>();

    // Default is true, set to false.
    REQUIRE(fix.engine.doString("ffe.setGammaCorrection(false)"));
    CHECK(cfg->gammaEnabled == false);

    REQUIRE(fix.engine.doString("ffe.setGammaCorrection(true)"));
    CHECK(cfg->gammaEnabled == true);
}

TEST_CASE("setGammaCorrection is no-op when no config exists",
          "[scripting][postprocess]") {
    PostProcessBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setGammaCorrection(true)"));
    // No crash, no config created.
    CHECK(fix.world.registry().ctx().find<ffe::renderer::PostProcessConfig>() == nullptr);
}
