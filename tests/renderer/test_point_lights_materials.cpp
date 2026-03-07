// test_point_lights_materials.cpp — Catch2 unit tests for point lights and
// extended Material3D (specular properties, normal/specular map binding).
//
// All tests are CPU-only (no GL context required) — they validate data
// structures, defaults, and ECS component state.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "renderer/mesh_renderer.h"
#include "renderer/render_system.h"
#include "renderer/rhi_types.h"
#include "core/ecs.h"
#include "core/types.h"

using namespace ffe;
using namespace ffe::renderer;
using namespace ffe::rhi;

// ===========================================================================
// PointLight struct defaults
// ===========================================================================

TEST_CASE("PointLight: default values are correct") {
    const PointLight pl;
    REQUIRE_THAT(pl.position.x, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(pl.position.y, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(pl.position.z, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(pl.color.r, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(pl.color.g, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(pl.color.b, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(pl.radius, Catch::Matchers::WithinAbs(10.0f, 1e-6f));
    REQUIRE(pl.active == false);
}

// ===========================================================================
// SceneLighting3D point light management
// ===========================================================================

TEST_CASE("SceneLighting3D: default has no active point lights") {
    const SceneLighting3D lighting;
    for (u32 i = 0; i < MAX_POINT_LIGHTS; ++i) {
        REQUIRE(lighting.pointLights[i].active == false);
    }
    REQUIRE(lighting.activePointLightCount == 0u);
}

TEST_CASE("SceneLighting3D: can activate and configure a point light") {
    SceneLighting3D lighting;

    lighting.pointLights[0].position = {1.0f, 2.0f, 3.0f};
    lighting.pointLights[0].color    = {0.5f, 0.8f, 0.2f};
    lighting.pointLights[0].radius   = 15.0f;
    lighting.pointLights[0].active   = true;

    REQUIRE(lighting.pointLights[0].active == true);
    REQUIRE_THAT(lighting.pointLights[0].position.x, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(lighting.pointLights[0].position.y, Catch::Matchers::WithinAbs(2.0f, 1e-6f));
    REQUIRE_THAT(lighting.pointLights[0].position.z, Catch::Matchers::WithinAbs(3.0f, 1e-6f));
    REQUIRE_THAT(lighting.pointLights[0].color.r, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(lighting.pointLights[0].radius, Catch::Matchers::WithinAbs(15.0f, 1e-6f));
}

TEST_CASE("SceneLighting3D: can deactivate a point light") {
    SceneLighting3D lighting;
    lighting.pointLights[3].active = true;
    REQUIRE(lighting.pointLights[3].active == true);

    lighting.pointLights[3].active = false;
    REQUIRE(lighting.pointLights[3].active == false);
}

TEST_CASE("SceneLighting3D: MAX_POINT_LIGHTS is 8") {
    REQUIRE(MAX_POINT_LIGHTS == 8u);
}

TEST_CASE("SceneLighting3D: all 8 slots can be activated independently") {
    SceneLighting3D lighting;
    for (u32 i = 0; i < MAX_POINT_LIGHTS; ++i) {
        lighting.pointLights[i].active = true;
        lighting.pointLights[i].position = {
            static_cast<f32>(i), static_cast<f32>(i * 2), static_cast<f32>(i * 3)
        };
    }
    for (u32 i = 0; i < MAX_POINT_LIGHTS; ++i) {
        REQUIRE(lighting.pointLights[i].active == true);
        REQUIRE_THAT(lighting.pointLights[i].position.x,
                     Catch::Matchers::WithinAbs(static_cast<f32>(i), 1e-6f));
    }
}

TEST_CASE("SceneLighting3D: directional light is preserved alongside point lights") {
    SceneLighting3D lighting;
    const glm::vec3 origDir = lighting.lightDir;

    // Activate a point light — directional light must remain unchanged
    lighting.pointLights[0].active = true;
    lighting.pointLights[0].position = {5.0f, 5.0f, 5.0f};

    REQUIRE_THAT(lighting.lightDir.x, Catch::Matchers::WithinAbs(origDir.x, 1e-6f));
    REQUIRE_THAT(lighting.lightDir.y, Catch::Matchers::WithinAbs(origDir.y, 1e-6f));
    REQUIRE_THAT(lighting.lightDir.z, Catch::Matchers::WithinAbs(origDir.z, 1e-6f));
}

// ===========================================================================
// Material3D extended properties
// ===========================================================================

TEST_CASE("Material3D: default specularColor is white") {
    const Material3D mat;
    REQUIRE_THAT(mat.specularColor.r, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(mat.specularColor.g, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(mat.specularColor.b, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("Material3D: default shininess is 32") {
    const Material3D mat;
    REQUIRE_THAT(mat.shininess, Catch::Matchers::WithinAbs(32.0f, 1e-6f));
}

TEST_CASE("Material3D: default normalMapTexture is invalid (0)") {
    const Material3D mat;
    REQUIRE(mat.normalMapTexture.id == 0u);
    REQUIRE(!isValid(mat.normalMapTexture));
}

TEST_CASE("Material3D: default specularMapTexture is invalid (0)") {
    const Material3D mat;
    REQUIRE(mat.specularMapTexture.id == 0u);
    REQUIRE(!isValid(mat.specularMapTexture));
}

TEST_CASE("Material3D: specular properties can be set") {
    Material3D mat;
    mat.specularColor = {0.5f, 0.6f, 0.7f};
    mat.shininess     = 64.0f;

    REQUIRE_THAT(mat.specularColor.r, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(mat.specularColor.g, Catch::Matchers::WithinAbs(0.6f, 1e-6f));
    REQUIRE_THAT(mat.specularColor.b, Catch::Matchers::WithinAbs(0.7f, 1e-6f));
    REQUIRE_THAT(mat.shininess, Catch::Matchers::WithinAbs(64.0f, 1e-6f));
}

TEST_CASE("Material3D: normal map texture handle can be set") {
    Material3D mat;
    mat.normalMapTexture = TextureHandle{42};
    REQUIRE(mat.normalMapTexture.id == 42u);
    REQUIRE(isValid(mat.normalMapTexture));
}

TEST_CASE("Material3D: specular map texture handle can be set") {
    Material3D mat;
    mat.specularMapTexture = TextureHandle{99};
    REQUIRE(mat.specularMapTexture.id == 99u);
    REQUIRE(isValid(mat.specularMapTexture));
}

// ===========================================================================
// ECS integration: Material3D with specular on entities
// ===========================================================================

TEST_CASE("Material3D: ECS emplace preserves specular properties") {
    World world;
    const EntityId eid = world.createEntity();

    Material3D mat;
    mat.specularColor      = {0.2f, 0.3f, 0.4f};
    mat.shininess          = 128.0f;
    mat.normalMapTexture   = TextureHandle{10};
    mat.specularMapTexture = TextureHandle{20};
    world.registry().emplace<Material3D>(static_cast<entt::entity>(eid), mat);

    const Material3D& readBack = world.getComponent<Material3D>(eid);
    REQUIRE_THAT(readBack.specularColor.r, Catch::Matchers::WithinAbs(0.2f, 1e-6f));
    REQUIRE_THAT(readBack.specularColor.g, Catch::Matchers::WithinAbs(0.3f, 1e-6f));
    REQUIRE_THAT(readBack.specularColor.b, Catch::Matchers::WithinAbs(0.4f, 1e-6f));
    REQUIRE_THAT(readBack.shininess, Catch::Matchers::WithinAbs(128.0f, 1e-6f));
    REQUIRE(readBack.normalMapTexture.id == 10u);
    REQUIRE(readBack.specularMapTexture.id == 20u);
}

TEST_CASE("Material3D: get_or_emplace creates with defaults then updates") {
    World world;
    const EntityId eid = world.createEntity();

    // get_or_emplace should create with defaults
    Material3D& mat = world.registry().get_or_emplace<Material3D>(
        static_cast<entt::entity>(eid));
    REQUIRE_THAT(mat.shininess, Catch::Matchers::WithinAbs(32.0f, 1e-6f));
    REQUIRE(mat.normalMapTexture.id == 0u);

    // Update specular
    mat.specularColor = {0.8f, 0.0f, 0.0f};
    mat.shininess     = 256.0f;

    const Material3D& readBack = world.getComponent<Material3D>(eid);
    REQUIRE_THAT(readBack.specularColor.r, Catch::Matchers::WithinAbs(0.8f, 1e-6f));
    REQUIRE_THAT(readBack.shininess, Catch::Matchers::WithinAbs(256.0f, 1e-6f));
}

// ===========================================================================
// ECS integration: SceneLighting3D in ECS context
// ===========================================================================

TEST_CASE("SceneLighting3D: ECS context point light state is accessible") {
    World world;
    auto& lighting = world.registry().ctx().emplace<SceneLighting3D>();

    lighting.pointLights[0].active   = true;
    lighting.pointLights[0].position = {10.0f, 20.0f, 30.0f};
    lighting.pointLights[0].color    = {1.0f, 0.0f, 0.0f};
    lighting.pointLights[0].radius   = 5.0f;

    const auto* litPtr = world.registry().ctx().find<SceneLighting3D>();
    REQUIRE(litPtr != nullptr);
    REQUIRE(litPtr->pointLights[0].active == true);
    REQUIRE_THAT(litPtr->pointLights[0].position.x,
                 Catch::Matchers::WithinAbs(10.0f, 1e-6f));
    REQUIRE_THAT(litPtr->pointLights[0].radius,
                 Catch::Matchers::WithinAbs(5.0f, 1e-6f));
}
