// test_water_bindings.cpp -- Catch2 unit tests for the water Lua bindings.
//
// Tests verify binding registration and basic behavior.
// These run in headless mode -- no GL context, no actual water rendering.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/render_system.h"
#include "renderer/water.h"

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World (with WaterConfig in context)
// ---------------------------------------------------------------------------

struct WaterBindingFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    WaterBindingFixture() {
        // Emplace WaterConfig into ECS context (normally done by Application::initSubsystems)
        world.registry().ctx().emplace<ffe::renderer::WaterConfig>();
        REQUIRE(engine.init());
        engine.setWorld(&world);
    }
    ~WaterBindingFixture() { engine.shutdown(); }
};

// =============================================================================
// ffe.createWater -- binding registration
// =============================================================================

TEST_CASE("createWater binding is callable", "[scripting][water]") {
    WaterBindingFixture fix;
    CHECK(fix.engine.doString("local e = ffe.createWater(5.0)"));
}

TEST_CASE("createWater returns a valid entity ID", "[scripting][water]") {
    WaterBindingFixture fix;
    // Create a dummy entity first so the water entity gets ID > 0
    // (EnTT's first entity in a fresh registry is ID 0, which is valid but
    //  indistinguishable from the nil-world error return)
    fix.world.createEntity();
    REQUIRE(fix.engine.doString(
        "local e = ffe.createWater(5.0)\n"
        "assert(type(e) == 'number', 'expected number, got ' .. type(e))\n"
        "assert(e > 0, 'expected positive entity ID, got ' .. tostring(e))"));
}

TEST_CASE("createWater enables WaterConfig", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.createWater(3.0)"));
    const auto& cfg = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    CHECK(cfg.enabled);
    CHECK(cfg.waterLevel == 3.0f);
}

// =============================================================================
// ffe.setWaterLevel -- binding registration
// =============================================================================

TEST_CASE("setWaterLevel binding is callable", "[scripting][water]") {
    WaterBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setWaterLevel(10.0)"));
}

TEST_CASE("setWaterLevel changes waterLevel", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setWaterLevel(7.5)"));
    const auto& cfg = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    CHECK(cfg.waterLevel == 7.5f);
}

// =============================================================================
// ffe.setWaterColor -- binding registration
// =============================================================================

TEST_CASE("setWaterColor binding is callable", "[scripting][water]") {
    WaterBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setWaterColor(0.2, 0.5, 0.8, 0.7)"));
}

TEST_CASE("setWaterColor changes shallowColor", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setWaterColor(0.2, 0.5, 0.8, 0.7)"));
    const auto& cfg = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    CHECK(cfg.shallowColor.x == 0.2f);
    CHECK(cfg.shallowColor.y == 0.5f);
    CHECK(cfg.shallowColor.z == 0.8f);
    CHECK(cfg.shallowColor.w == 0.7f);
}

TEST_CASE("setWaterColor auto-derives deepColor", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setWaterColor(1.0, 1.0, 1.0, 0.5)"));
    const auto& cfg = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    // Deep = shallow * 0.3 for RGB, alpha * 1.5 for A
    CHECK(cfg.deepColor.x == Catch::Approx(0.3f));
    CHECK(cfg.deepColor.y == Catch::Approx(0.3f));
    CHECK(cfg.deepColor.z == Catch::Approx(0.3f));
    CHECK(cfg.deepColor.w == Catch::Approx(0.75f));
}

// =============================================================================
// ffe.setWaterDeepColor -- binding registration
// =============================================================================

TEST_CASE("setWaterDeepColor binding is callable", "[scripting][water]") {
    WaterBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setWaterDeepColor(0.1, 0.2, 0.3)"));
}

TEST_CASE("setWaterDeepColor changes deepColor RGB", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setWaterDeepColor(0.1, 0.2, 0.3)"));
    const auto& cfg = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    CHECK(cfg.deepColor.x == 0.1f);
    CHECK(cfg.deepColor.y == 0.2f);
    CHECK(cfg.deepColor.z == 0.3f);
}

// =============================================================================
// ffe.setWaterOpacity -- binding registration
// =============================================================================

TEST_CASE("setWaterOpacity binding is callable", "[scripting][water]") {
    WaterBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setWaterOpacity(0.8)"));
}

TEST_CASE("setWaterOpacity changes shallowColor.a", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setWaterOpacity(0.9)"));
    const auto& cfg = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    CHECK(cfg.shallowColor.w == Catch::Approx(0.9f));
}

// =============================================================================
// ffe.setWaterWaveSpeed -- binding registration
// =============================================================================

TEST_CASE("setWaterWaveSpeed binding is callable", "[scripting][water]") {
    WaterBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setWaterWaveSpeed(0.05)"));
}

TEST_CASE("setWaterWaveSpeed changes waveSpeed", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setWaterWaveSpeed(0.1)"));
    const auto& cfg = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    CHECK(cfg.waveSpeed == Catch::Approx(0.1f));
}

// =============================================================================
// ffe.setWaterWaveScale -- binding registration
// =============================================================================

TEST_CASE("setWaterWaveScale binding is callable", "[scripting][water]") {
    WaterBindingFixture fix;
    CHECK(fix.engine.doString("ffe.setWaterWaveScale(0.04)"));
}

TEST_CASE("setWaterWaveScale changes waveScale", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setWaterWaveScale(0.08)"));
    const auto& cfg = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    CHECK(cfg.waveScale == Catch::Approx(0.08f));
}

// =============================================================================
// ffe.removeWater -- binding registration
// =============================================================================

TEST_CASE("removeWater binding is callable", "[scripting][water]") {
    WaterBindingFixture fix;
    CHECK(fix.engine.doString("ffe.removeWater()"));
}

TEST_CASE("removeWater disables WaterConfig after createWater", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.createWater(5.0)"));
    const auto& cfg1 = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    CHECK(cfg1.enabled);

    REQUIRE(fix.engine.doString("ffe.removeWater()"));
    const auto& cfg2 = fix.world.registry().ctx().get<ffe::renderer::WaterConfig>();
    CHECK_FALSE(cfg2.enabled);
}

TEST_CASE("removeWater destroys Water entities", "[scripting][water]") {
    WaterBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.createWater(5.0)"));

    auto waterView = fix.world.registry().view<ffe::renderer::Water>();
    CHECK(waterView.begin() != waterView.end());

    REQUIRE(fix.engine.doString("ffe.removeWater()"));

    auto waterView2 = fix.world.registry().view<ffe::renderer::Water>();
    CHECK(waterView2.begin() == waterView2.end());
}
