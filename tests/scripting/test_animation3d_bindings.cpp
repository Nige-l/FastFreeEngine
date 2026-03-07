// test_animation3d_bindings.cpp — Catch2 unit tests for the 3D skeletal
// animation Lua bindings:
//   ffe.playAnimation3D, ffe.stopAnimation3D, ffe.setAnimationSpeed3D,
//   ffe.getAnimationProgress3D, ffe.isAnimation3DPlaying, ffe.getAnimationCount3D,
//   ffe.crossfadeAnimation3D, ffe.setRootMotion3D, ffe.getRootMotionDelta3D
//
// Tests verify binding registration, argument validation, and correct behavior.
// These run in headless mode — no GL context, no actual meshes loaded.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/render_system.h"
#include "renderer/mesh_loader.h"

using Catch::Approx;

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World
// ---------------------------------------------------------------------------

struct Anim3DBindingFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    Anim3DBindingFixture() {
        REQUIRE(engine.init());
        engine.setWorld(&world);
    }
    ~Anim3DBindingFixture() { engine.shutdown(); }
};

// =============================================================================
// ffe.playAnimation3D — binding registration
// =============================================================================

TEST_CASE("playAnimation3D binding exists and rejects invalid entity",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    // Should not crash with invalid entity ID — just logs a warning.
    REQUIRE(fix.engine.doString("ffe.playAnimation3D(999999, 0, true)"));
}

TEST_CASE("playAnimation3D rejects non-number first argument",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    // Should not crash — logs error and returns.
    REQUIRE(fix.engine.doString("ffe.playAnimation3D('bad', 0, true)"));
}

TEST_CASE("playAnimation3D warns on entity without Mesh component",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();
    // Entity has no Mesh component — should warn, not crash.
    const std::string lua = "ffe.playAnimation3D(" + std::to_string(eid) + ", 0, true)";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

// =============================================================================
// ffe.stopAnimation3D — binding registration and no-op behavior
// =============================================================================

TEST_CASE("stopAnimation3D binding exists and is safe on non-animated entity",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();
    const std::string lua = "ffe.stopAnimation3D(" + std::to_string(eid) + ")";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

TEST_CASE("stopAnimation3D sets playing to false", "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    // Manually add AnimationState
    auto& animState = fix.world.addComponent<ffe::AnimationState>(eid);
    animState.playing = true;

    const std::string lua = "ffe.stopAnimation3D(" + std::to_string(eid) + ")";
    REQUIRE(fix.engine.doString(lua.c_str()));

    const auto& got = fix.world.getComponent<ffe::AnimationState>(eid);
    REQUIRE(got.playing == false);
}

// =============================================================================
// ffe.setAnimationSpeed3D — binding registration
// =============================================================================

TEST_CASE("setAnimationSpeed3D binding exists and sets speed", "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    auto& animState = fix.world.addComponent<ffe::AnimationState>(eid);
    animState.speed = 1.0f;

    const std::string lua = "ffe.setAnimationSpeed3D(" + std::to_string(eid) + ", 2.5)";
    REQUIRE(fix.engine.doString(lua.c_str()));

    REQUIRE(fix.world.getComponent<ffe::AnimationState>(eid).speed == Approx(2.5f));
}

TEST_CASE("setAnimationSpeed3D clamps negative to zero", "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    auto& animState = fix.world.addComponent<ffe::AnimationState>(eid);
    animState.speed = 1.0f;

    const std::string lua = "ffe.setAnimationSpeed3D(" + std::to_string(eid) + ", -5.0)";
    REQUIRE(fix.engine.doString(lua.c_str()));

    REQUIRE(fix.world.getComponent<ffe::AnimationState>(eid).speed == Approx(0.0f));
}

// =============================================================================
// ffe.getAnimationProgress3D — binding registration
// =============================================================================

TEST_CASE("getAnimationProgress3D returns 0 for entity without animation",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    const std::string lua =
        "local p = ffe.getAnimationProgress3D(" + std::to_string(eid) + ")\n"
        "assert(p == 0.0, 'expected 0.0, got ' .. tostring(p))";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

TEST_CASE("getAnimationProgress3D returns 0 for invalid entity",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "local p = ffe.getAnimationProgress3D(999999)\n"
        "assert(p == 0.0)"));
}

// =============================================================================
// ffe.isAnimation3DPlaying — binding registration
// =============================================================================

TEST_CASE("isAnimation3DPlaying returns false for entity without AnimationState",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    const std::string lua =
        "local p = ffe.isAnimation3DPlaying(" + std::to_string(eid) + ")\n"
        "assert(p == false, 'expected false')";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

TEST_CASE("isAnimation3DPlaying returns true when playing", "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    auto& animState = fix.world.addComponent<ffe::AnimationState>(eid);
    animState.playing = true;

    const std::string lua =
        "local p = ffe.isAnimation3DPlaying(" + std::to_string(eid) + ")\n"
        "assert(p == true, 'expected true')";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

// =============================================================================
// ffe.getAnimationCount3D — binding registration
// =============================================================================

TEST_CASE("getAnimationCount3D returns 0 for entity without Mesh",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    const std::string lua =
        "local c = ffe.getAnimationCount3D(" + std::to_string(eid) + ")\n"
        "assert(c == 0, 'expected 0, got ' .. tostring(c))";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

TEST_CASE("getAnimationCount3D returns 0 for entity with Mesh but no skeleton",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    // Add a Mesh component with an invalid (but non-zero) handle.
    // getMeshAnimations will return nullptr for handles that aren't loaded.
    auto& meshComp = fix.world.addComponent<ffe::Mesh>(eid);
    meshComp.meshHandle = ffe::renderer::MeshHandle{1};

    const std::string lua =
        "local c = ffe.getAnimationCount3D(" + std::to_string(eid) + ")\n"
        "assert(c == 0, 'expected 0, got ' .. tostring(c))";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

TEST_CASE("getAnimationCount3D returns 0 for invalid entity",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "local c = ffe.getAnimationCount3D(999999)\n"
        "assert(c == 0)"));
}

// =============================================================================
// ffe.crossfadeAnimation3D — binding registration and behavior
// =============================================================================

TEST_CASE("crossfadeAnimation3D binding exists and is callable",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    // Should not crash with invalid entity — just logs a warning.
    REQUIRE(fix.engine.doString("ffe.crossfadeAnimation3D(999999, 0, 0.5)"));
}

TEST_CASE("crossfadeAnimation3D rejects non-number arguments",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    // Non-number entityId — logs error, returns.
    REQUIRE(fix.engine.doString("ffe.crossfadeAnimation3D('bad', 0, 0.5)"));
}

TEST_CASE("crossfadeAnimation3D warns on entity without AnimationState",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();
    const std::string lua = "ffe.crossfadeAnimation3D(" + std::to_string(eid) + ", 0, 0.5)";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

TEST_CASE("crossfadeAnimation3D sets blend fields on AnimationState",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    // Manually set up AnimationState as if clip 0 is playing
    auto& animState = fix.world.addComponent<ffe::AnimationState>(eid);
    animState.clipIndex = 0;
    animState.time      = 1.5f;
    animState.playing   = true;

    // Crossfade to clip 1 over 0.3 seconds
    const std::string lua = "ffe.crossfadeAnimation3D(" + std::to_string(eid) + ", 1, 0.3)";
    REQUIRE(fix.engine.doString(lua.c_str()));

    const auto& got = fix.world.getComponent<ffe::AnimationState>(eid);
    REQUIRE(got.blending == true);
    REQUIRE(got.blendFromClip == 0);
    REQUIRE(got.blendFromTime == Approx(1.5f));
    REQUIRE(got.blendDuration == Approx(0.3f));
    REQUIRE(got.blendElapsed == Approx(0.0f));
    REQUIRE(got.clipIndex == 1);
    REQUIRE(got.time == Approx(0.0f));
}

TEST_CASE("crossfadeAnimation3D rejects duration <= 0",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();
    fix.world.addComponent<ffe::AnimationState>(eid);

    // Duration of 0 should be rejected — blending should NOT be set
    const std::string lua = "ffe.crossfadeAnimation3D(" + std::to_string(eid) + ", 0, 0.0)";
    REQUIRE(fix.engine.doString(lua.c_str()));

    const auto& got = fix.world.getComponent<ffe::AnimationState>(eid);
    REQUIRE(got.blending == false);
}

// =============================================================================
// ffe.setRootMotion3D — binding registration and behavior
// =============================================================================

TEST_CASE("setRootMotion3D binding exists and is callable",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    // Should not crash with invalid entity.
    REQUIRE(fix.engine.doString("ffe.setRootMotion3D(999999, true)"));
}

TEST_CASE("setRootMotion3D enables RootMotionDelta component",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    const std::string lua = "ffe.setRootMotion3D(" + std::to_string(eid) + ", true)";
    REQUIRE(fix.engine.doString(lua.c_str()));

    const auto* rm = fix.world.registry().try_get<ffe::RootMotionDelta>(
        static_cast<entt::entity>(eid));
    REQUIRE(rm != nullptr);
    REQUIRE(rm->enabled == true);
}

TEST_CASE("setRootMotion3D disables RootMotionDelta component",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    // Enable first
    auto& rm = fix.world.registry().emplace<ffe::RootMotionDelta>(
        static_cast<entt::entity>(eid));
    rm.enabled = true;

    // Disable via binding
    const std::string lua = "ffe.setRootMotion3D(" + std::to_string(eid) + ", false)";
    REQUIRE(fix.engine.doString(lua.c_str()));

    const auto* got = fix.world.registry().try_get<ffe::RootMotionDelta>(
        static_cast<entt::entity>(eid));
    REQUIRE(got != nullptr);
    REQUIRE(got->enabled == false);
}

// =============================================================================
// ffe.getRootMotionDelta3D — binding registration and behavior
// =============================================================================

TEST_CASE("getRootMotionDelta3D returns 3 values (0,0,0 default)",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    const std::string lua =
        "local dx, dy, dz = ffe.getRootMotionDelta3D(" + std::to_string(eid) + ")\n"
        "assert(dx == 0, 'expected dx=0, got ' .. tostring(dx))\n"
        "assert(dy == 0, 'expected dy=0, got ' .. tostring(dy))\n"
        "assert(dz == 0, 'expected dz=0, got ' .. tostring(dz))";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

TEST_CASE("getRootMotionDelta3D returns 0,0,0 for disabled component",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    auto& rm = fix.world.registry().emplace<ffe::RootMotionDelta>(
        static_cast<entt::entity>(eid));
    rm.enabled = false;
    rm.translationDelta = {5.0f, 6.0f, 7.0f};

    const std::string lua =
        "local dx, dy, dz = ffe.getRootMotionDelta3D(" + std::to_string(eid) + ")\n"
        "assert(dx == 0, 'expected 0 when disabled')\n"
        "assert(dy == 0)\n"
        "assert(dz == 0)";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

TEST_CASE("getRootMotionDelta3D returns actual delta when enabled",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    const ffe::EntityId eid = fix.world.createEntity();

    auto& rm = fix.world.registry().emplace<ffe::RootMotionDelta>(
        static_cast<entt::entity>(eid));
    rm.enabled = true;
    rm.translationDelta = {1.5f, -0.5f, 3.0f};

    const std::string lua =
        "local dx, dy, dz = ffe.getRootMotionDelta3D(" + std::to_string(eid) + ")\n"
        "assert(math.abs(dx - 1.5) < 0.001, 'dx mismatch: ' .. tostring(dx))\n"
        "assert(math.abs(dy - (-0.5)) < 0.001, 'dy mismatch: ' .. tostring(dy))\n"
        "assert(math.abs(dz - 3.0) < 0.001, 'dz mismatch: ' .. tostring(dz))";
    REQUIRE(fix.engine.doString(lua.c_str()));
}

TEST_CASE("getRootMotionDelta3D returns 0,0,0 for invalid entity",
          "[scripting][animation3d]") {
    Anim3DBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "local dx, dy, dz = ffe.getRootMotionDelta3D(999999)\n"
        "assert(dx == 0 and dy == 0 and dz == 0)"));
}
