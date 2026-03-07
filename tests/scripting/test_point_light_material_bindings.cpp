// test_point_light_material_bindings.cpp — Catch2 unit tests for the point light
// and material Lua bindings added in Session 44.
//
// Tests validate: ffe.addPointLight, ffe.removePointLight, ffe.setPointLightPosition,
// ffe.setPointLightColor, ffe.setPointLightRadius, ffe.setMeshSpecular,
// ffe.setMeshNormalMap, ffe.setMeshSpecularMap.
//
// All tests construct a minimal ffe::World, emplace the required ECS context
// objects, call setWorld(), then drive the bindings via lua_pcall through
// engine.doString(). State is read back from ECS.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/render_system.h"
#include "renderer/mesh_renderer.h"
#include "renderer/rhi_types.h"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

struct ScriptFixture {
    ffe::ScriptEngine engine;
    ScriptFixture() { REQUIRE(engine.init()); }
    ~ScriptFixture() { engine.shutdown(); }
};

// LightingFixture: ScriptEngine + World + SceneLighting3D emplaced in ECS context.
struct LightingFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    LightingFixture() {
        REQUIRE(engine.init());
        world.registry().ctx().emplace<ffe::renderer::SceneLighting3D>();
        engine.setWorld(&world);
    }
    ~LightingFixture() { engine.shutdown(); }

    ffe::renderer::SceneLighting3D& lighting() {
        return *world.registry().ctx().find<ffe::renderer::SceneLighting3D>();
    }
};

// =============================================================================
// ffe.addPointLight
// =============================================================================

TEST_CASE("addPointLight: sets position, color, radius, and active flag",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.addPointLight(0, 1, 2, 3, 0.5, 0.6, 0.7, 15)"));

    const auto& pl = fix.lighting().pointLights[0];
    CHECK(pl.active == true);
    CHECK(pl.position.x == Catch::Approx(1.0f));
    CHECK(pl.position.y == Catch::Approx(2.0f));
    CHECK(pl.position.z == Catch::Approx(3.0f));
    CHECK(pl.color.r == Catch::Approx(0.5f));
    CHECK(pl.color.g == Catch::Approx(0.6f));
    CHECK(pl.color.b == Catch::Approx(0.7f));
    CHECK(pl.radius == Catch::Approx(15.0f));
}

TEST_CASE("addPointLight: slot 7 (max valid index) works",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.addPointLight(7, 0, 0, 0, 1, 1, 1, 5)"));
    CHECK(fix.lighting().pointLights[7].active == true);
}

TEST_CASE("addPointLight: slot 8 is out of range and is a no-op",
          "[scripting][pointlight]") {
    LightingFixture fix;
    // Should not crash — index 8 is out of range
    REQUIRE(fix.engine.doString("ffe.addPointLight(8, 0, 0, 0, 1, 1, 1, 5)"));
    // All slots should remain inactive
    for (ffe::u32 i = 0; i < ffe::renderer::MAX_POINT_LIGHTS; ++i) {
        CHECK(fix.lighting().pointLights[i].active == false);
    }
}

TEST_CASE("addPointLight: negative index is a no-op",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.addPointLight(-1, 0, 0, 0, 1, 1, 1, 5)"));
    for (ffe::u32 i = 0; i < ffe::renderer::MAX_POINT_LIGHTS; ++i) {
        CHECK(fix.lighting().pointLights[i].active == false);
    }
}

TEST_CASE("addPointLight: no World set is a no-op",
          "[scripting][pointlight]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.addPointLight(0, 0, 0, 0, 1, 1, 1, 10)"));
}

// =============================================================================
// ffe.removePointLight
// =============================================================================

TEST_CASE("removePointLight: deactivates a previously active light",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.addPointLight(2, 0, 0, 0, 1, 1, 1, 10)"));
    CHECK(fix.lighting().pointLights[2].active == true);

    REQUIRE(fix.engine.doString("ffe.removePointLight(2)"));
    CHECK(fix.lighting().pointLights[2].active == false);
}

TEST_CASE("removePointLight: removing an already inactive light is a no-op",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.removePointLight(0)"));
    CHECK(fix.lighting().pointLights[0].active == false);
}

TEST_CASE("removePointLight: out-of-range index is a no-op",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.removePointLight(8)"));
}

// =============================================================================
// ffe.setPointLightPosition
// =============================================================================

TEST_CASE("setPointLightPosition: updates position of an existing light",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.addPointLight(1, 0, 0, 0, 1, 1, 1, 10)"));
    REQUIRE(fix.engine.doString("ffe.setPointLightPosition(1, 5, 6, 7)"));

    const auto& pl = fix.lighting().pointLights[1];
    CHECK(pl.position.x == Catch::Approx(5.0f));
    CHECK(pl.position.y == Catch::Approx(6.0f));
    CHECK(pl.position.z == Catch::Approx(7.0f));
}

TEST_CASE("setPointLightPosition: out-of-range index is a no-op",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setPointLightPosition(8, 1, 2, 3)"));
}

// =============================================================================
// ffe.setPointLightColor
// =============================================================================

TEST_CASE("setPointLightColor: updates color of an existing light",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.addPointLight(0, 0, 0, 0, 1, 1, 1, 10)"));
    REQUIRE(fix.engine.doString("ffe.setPointLightColor(0, 0.2, 0.3, 0.4)"));

    const auto& pl = fix.lighting().pointLights[0];
    CHECK(pl.color.r == Catch::Approx(0.2f));
    CHECK(pl.color.g == Catch::Approx(0.3f));
    CHECK(pl.color.b == Catch::Approx(0.4f));
}

TEST_CASE("setPointLightColor: out-of-range index is a no-op",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setPointLightColor(-1, 1, 1, 1)"));
}

// =============================================================================
// ffe.setPointLightRadius
// =============================================================================

TEST_CASE("setPointLightRadius: updates radius of an existing light",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.addPointLight(0, 0, 0, 0, 1, 1, 1, 10)"));
    REQUIRE(fix.engine.doString("ffe.setPointLightRadius(0, 25)"));

    CHECK(fix.lighting().pointLights[0].radius == Catch::Approx(25.0f));
}

TEST_CASE("setPointLightRadius: radius is clamped to minimum 0.001",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.addPointLight(0, 0, 0, 0, 1, 1, 1, 10)"));
    REQUIRE(fix.engine.doString("ffe.setPointLightRadius(0, 0)"));

    CHECK(fix.lighting().pointLights[0].radius >= 0.001f);
}

TEST_CASE("setPointLightRadius: out-of-range index is a no-op",
          "[scripting][pointlight]") {
    LightingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setPointLightRadius(8, 5)"));
}

// =============================================================================
// ffe.setMeshSpecular
// =============================================================================

TEST_CASE("setMeshSpecular: sets specular color and shininess on entity",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshSpecular(_eid, 0.5, 0.6, 0.7, 64)"));

    const ffe::Material3D* mat =
        world.registry().try_get<ffe::Material3D>(static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->specularColor.r == Catch::Approx(0.5f));
    CHECK(mat->specularColor.g == Catch::Approx(0.6f));
    CHECK(mat->specularColor.b == Catch::Approx(0.7f));
    CHECK(mat->shininess == Catch::Approx(64.0f));
}

TEST_CASE("setMeshSpecular: creates Material3D if not present",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshSpecular(_eid, 1, 1, 1, 128)"));

    REQUIRE(world.hasComponent<ffe::Material3D>(eid));
    const ffe::Material3D& mat = world.getComponent<ffe::Material3D>(eid);
    CHECK(mat.shininess == Catch::Approx(128.0f));
}

TEST_CASE("setMeshSpecular: shininess clamped to minimum 1.0",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshSpecular(_eid, 1, 1, 1, 0)"));

    const ffe::Material3D& mat = world.getComponent<ffe::Material3D>(eid);
    CHECK(mat.shininess >= 1.0f);
}

TEST_CASE("setMeshSpecular: invalid entity is a no-op",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("ffe.setMeshSpecular(99999, 1, 1, 1, 32)"));
}

TEST_CASE("setMeshSpecular: no World set is a no-op",
          "[scripting][material]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.setMeshSpecular(1, 1, 1, 1, 32)"));
}

TEST_CASE("setMeshSpecular: non-number entityId is a no-op",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("ffe.setMeshSpecular('hello', 1, 1, 1, 32)"));
}

// =============================================================================
// ffe.setMeshNormalMap
// =============================================================================

TEST_CASE("setMeshNormalMap: sets normalMapTexture on entity",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshNormalMap(_eid, 42)"));

    const ffe::Material3D* mat =
        world.registry().try_get<ffe::Material3D>(static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->normalMapTexture.id == 42u);
}

TEST_CASE("setMeshNormalMap: creates Material3D if not present",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshNormalMap(_eid, 7)"));

    REQUIRE(world.hasComponent<ffe::Material3D>(eid));
    CHECK(world.getComponent<ffe::Material3D>(eid).normalMapTexture.id == 7u);
}

TEST_CASE("setMeshNormalMap: textureId 0 clears normal map",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    auto& mat = world.registry().emplace<ffe::Material3D>(static_cast<entt::entity>(eid));
    mat.normalMapTexture = ffe::rhi::TextureHandle{50u};

    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshNormalMap(_eid, 0)"));

    CHECK(world.getComponent<ffe::Material3D>(eid).normalMapTexture.id == 0u);
}

TEST_CASE("setMeshNormalMap: negative textureId is a no-op",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    auto& mat = world.registry().emplace<ffe::Material3D>(static_cast<entt::entity>(eid));
    mat.normalMapTexture = ffe::rhi::TextureHandle{11u};

    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshNormalMap(_eid, -1)"));

    CHECK(world.getComponent<ffe::Material3D>(eid).normalMapTexture.id == 11u);
}

TEST_CASE("setMeshNormalMap: invalid entity is a no-op",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("ffe.setMeshNormalMap(99999, 5)"));
}

TEST_CASE("setMeshNormalMap: no World set is a no-op",
          "[scripting][material]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.setMeshNormalMap(1, 5)"));
}

// =============================================================================
// ffe.setMeshSpecularMap
// =============================================================================

TEST_CASE("setMeshSpecularMap: sets specularMapTexture on entity",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshSpecularMap(_eid, 88)"));

    const ffe::Material3D* mat =
        world.registry().try_get<ffe::Material3D>(static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->specularMapTexture.id == 88u);
}

TEST_CASE("setMeshSpecularMap: creates Material3D if not present",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshSpecularMap(_eid, 3)"));

    REQUIRE(world.hasComponent<ffe::Material3D>(eid));
    CHECK(world.getComponent<ffe::Material3D>(eid).specularMapTexture.id == 3u);
}

TEST_CASE("setMeshSpecularMap: textureId 0 clears specular map",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    auto& mat = world.registry().emplace<ffe::Material3D>(static_cast<entt::entity>(eid));
    mat.specularMapTexture = ffe::rhi::TextureHandle{77u};

    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshSpecularMap(_eid, 0)"));

    CHECK(world.getComponent<ffe::Material3D>(eid).specularMapTexture.id == 0u);
}

TEST_CASE("setMeshSpecularMap: negative textureId is a no-op",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    auto& mat = world.registry().emplace<ffe::Material3D>(static_cast<entt::entity>(eid));
    mat.specularMapTexture = ffe::rhi::TextureHandle{22u};

    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshSpecularMap(_eid, -1)"));

    CHECK(world.getComponent<ffe::Material3D>(eid).specularMapTexture.id == 22u);
}

TEST_CASE("setMeshSpecularMap: invalid entity is a no-op",
          "[scripting][material]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("ffe.setMeshSpecularMap(99999, 5)"));
}

TEST_CASE("setMeshSpecularMap: no World set is a no-op",
          "[scripting][material]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.setMeshSpecularMap(1, 5)"));
}
