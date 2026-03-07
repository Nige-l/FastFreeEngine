// test_skybox_bindings.cpp — Catch2 unit tests for the skybox Lua bindings:
//   ffe.loadSkybox, ffe.unloadSkybox, ffe.setSkyboxEnabled
//
// Tests verify binding registration, argument validation, and state toggling.
// Cubemap loading is expected to fail gracefully in headless mode (no GL context,
// no asset root) — we verify the binding returns false rather than crashing.

#include <catch2/catch_test_macros.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/mesh_renderer.h"

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World + SkyboxConfig* emplaced in ECS context.
// ---------------------------------------------------------------------------

struct SkyboxBindingFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;
    ffe::renderer::SkyboxConfig skyboxCfg;

    SkyboxBindingFixture() {
        REQUIRE(engine.init());
        world.registry().ctx().emplace<ffe::renderer::SkyboxConfig*>(&skyboxCfg);
        engine.setWorld(&world);
    }
    ~SkyboxBindingFixture() { engine.shutdown(); }
};

// =============================================================================
// ffe.loadSkybox — binding registration and graceful failure
// =============================================================================

TEST_CASE("loadSkybox binding exists and returns false with missing files",
          "[scripting][skybox]") {
    SkyboxBindingFixture fix;
    // loadSkybox should fail because there's no GL context and no asset root,
    // but it should not crash. The binding returns a boolean.
    REQUIRE(fix.engine.doString(
        "local ok = ffe.loadSkybox('r.png','l.png','t.png','b.png','f.png','k.png')\n"
        "assert(ok == false, 'expected loadSkybox to return false in headless mode')\n"
    ));
    // SkyboxConfig should remain unchanged.
    CHECK(fix.skyboxCfg.cubemapTexture == 0);
    CHECK(fix.skyboxCfg.enabled == false);
}

TEST_CASE("loadSkybox rejects non-string arguments",
          "[scripting][skybox]") {
    SkyboxBindingFixture fix;
    // Passing a number as the first argument should be rejected.
    REQUIRE(fix.engine.doString(
        "local ok = ffe.loadSkybox(123,'l.png','t.png','b.png','f.png','k.png')\n"
        "assert(ok == false, 'expected loadSkybox to reject non-string arg')\n"
    ));
    CHECK(fix.skyboxCfg.enabled == false);
}

// =============================================================================
// ffe.unloadSkybox — safe no-op when nothing loaded
// =============================================================================

TEST_CASE("unloadSkybox is safe when no skybox is loaded",
          "[scripting][skybox]") {
    SkyboxBindingFixture fix;
    // Should not crash; cubemapTexture is already 0.
    REQUIRE(fix.engine.doString("ffe.unloadSkybox()"));
    CHECK(fix.skyboxCfg.cubemapTexture == 0);
    CHECK(fix.skyboxCfg.enabled == false);
}

// =============================================================================
// ffe.setSkyboxEnabled — toggle without loading
// =============================================================================

TEST_CASE("setSkyboxEnabled toggles the enabled flag",
          "[scripting][skybox]") {
    SkyboxBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setSkyboxEnabled(true)"));
    CHECK(fix.skyboxCfg.enabled == true);

    REQUIRE(fix.engine.doString("ffe.setSkyboxEnabled(false)"));
    CHECK(fix.skyboxCfg.enabled == false);
}

TEST_CASE("setSkyboxEnabled does not affect cubemapTexture",
          "[scripting][skybox]") {
    SkyboxBindingFixture fix;
    fix.skyboxCfg.cubemapTexture = 42; // simulate a loaded texture
    REQUIRE(fix.engine.doString("ffe.setSkyboxEnabled(false)"));
    CHECK(fix.skyboxCfg.enabled == false);
    CHECK(fix.skyboxCfg.cubemapTexture == 42); // not cleared
}
