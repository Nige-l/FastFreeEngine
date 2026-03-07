// test_pbr_material.cpp -- Catch2 unit tests for PBRMaterial component struct.
//
// All tests are CPU-only (no GL context required) -- they validate data
// structures, defaults, and ECS component state.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "renderer/pbr_material.h"
#include "renderer/rhi_types.h"
#include "core/ecs.h"
#include "core/types.h"

using namespace ffe;
using namespace ffe::renderer;
using namespace ffe::rhi;

// ===========================================================================
// PBRMaterial default values
// ===========================================================================

TEST_CASE("PBRMaterial: default albedo is white (1,1,1,1)", "[renderer][pbr]") {
    const PBRMaterial mat;
    CHECK_THAT(mat.albedo.r, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    CHECK_THAT(mat.albedo.g, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    CHECK_THAT(mat.albedo.b, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    CHECK_THAT(mat.albedo.a, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("PBRMaterial: default metallic is 0.0", "[renderer][pbr]") {
    const PBRMaterial mat;
    CHECK_THAT(mat.metallic, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("PBRMaterial: default roughness is 0.5", "[renderer][pbr]") {
    const PBRMaterial mat;
    CHECK_THAT(mat.roughness, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("PBRMaterial: default normalScale is 1.0", "[renderer][pbr]") {
    const PBRMaterial mat;
    CHECK_THAT(mat.normalScale, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("PBRMaterial: default ao is 1.0", "[renderer][pbr]") {
    const PBRMaterial mat;
    CHECK_THAT(mat.ao, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("PBRMaterial: default emissiveFactor is (0,0,0)", "[renderer][pbr]") {
    const PBRMaterial mat;
    CHECK_THAT(mat.emissiveFactor.r, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(mat.emissiveFactor.g, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(mat.emissiveFactor.b, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

// ===========================================================================
// PBRMaterial sizeof
// ===========================================================================

TEST_CASE("PBRMaterial: sizeof is 72 bytes", "[renderer][pbr]") {
    CHECK(sizeof(PBRMaterial) == 72);
}

// ===========================================================================
// Texture handle defaults
// ===========================================================================

TEST_CASE("PBRMaterial: all texture handles default to 0 (no texture)", "[renderer][pbr]") {
    const PBRMaterial mat;
    CHECK(mat.albedoMap.id == 0u);
    CHECK(mat.metallicRoughnessMap.id == 0u);
    CHECK(mat.normalMap.id == 0u);
    CHECK(mat.aoMap.id == 0u);
    CHECK(mat.emissiveMap.id == 0u);
    CHECK(!isValid(mat.albedoMap));
    CHECK(!isValid(mat.metallicRoughnessMap));
    CHECK(!isValid(mat.normalMap));
    CHECK(!isValid(mat.aoMap));
    CHECK(!isValid(mat.emissiveMap));
}

// ===========================================================================
// Fields can be set and read back
// ===========================================================================

TEST_CASE("PBRMaterial: scalar fields can be set and read back", "[renderer][pbr]") {
    PBRMaterial mat;
    mat.albedo = {0.2f, 0.3f, 0.4f, 0.8f};
    mat.metallic = 0.9f;
    mat.roughness = 0.1f;
    mat.normalScale = 2.5f;
    mat.ao = 0.7f;
    mat.emissiveFactor = {1.0f, 0.5f, 0.25f};

    CHECK_THAT(mat.albedo.r, Catch::Matchers::WithinAbs(0.2f, 1e-6f));
    CHECK_THAT(mat.albedo.g, Catch::Matchers::WithinAbs(0.3f, 1e-6f));
    CHECK_THAT(mat.albedo.b, Catch::Matchers::WithinAbs(0.4f, 1e-6f));
    CHECK_THAT(mat.albedo.a, Catch::Matchers::WithinAbs(0.8f, 1e-6f));
    CHECK_THAT(mat.metallic, Catch::Matchers::WithinAbs(0.9f, 1e-6f));
    CHECK_THAT(mat.roughness, Catch::Matchers::WithinAbs(0.1f, 1e-6f));
    CHECK_THAT(mat.normalScale, Catch::Matchers::WithinAbs(2.5f, 1e-6f));
    CHECK_THAT(mat.ao, Catch::Matchers::WithinAbs(0.7f, 1e-6f));
    CHECK_THAT(mat.emissiveFactor.r, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    CHECK_THAT(mat.emissiveFactor.g, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
    CHECK_THAT(mat.emissiveFactor.b, Catch::Matchers::WithinAbs(0.25f, 1e-6f));
}

// ===========================================================================
// Texture handles independently settable
// ===========================================================================

TEST_CASE("PBRMaterial: albedoMap can be set independently", "[renderer][pbr]") {
    PBRMaterial mat;
    mat.albedoMap = TextureHandle{10};
    CHECK(mat.albedoMap.id == 10u);
    CHECK(isValid(mat.albedoMap));
    CHECK(mat.metallicRoughnessMap.id == 0u);
    CHECK(mat.normalMap.id == 0u);
    CHECK(mat.aoMap.id == 0u);
    CHECK(mat.emissiveMap.id == 0u);
}

TEST_CASE("PBRMaterial: metallicRoughnessMap can be set independently", "[renderer][pbr]") {
    PBRMaterial mat;
    mat.metallicRoughnessMap = TextureHandle{20};
    CHECK(mat.metallicRoughnessMap.id == 20u);
    CHECK(isValid(mat.metallicRoughnessMap));
    CHECK(mat.albedoMap.id == 0u);
    CHECK(mat.normalMap.id == 0u);
}

TEST_CASE("PBRMaterial: normalMap can be set independently", "[renderer][pbr]") {
    PBRMaterial mat;
    mat.normalMap = TextureHandle{30};
    CHECK(mat.normalMap.id == 30u);
    CHECK(isValid(mat.normalMap));
    CHECK(mat.albedoMap.id == 0u);
    CHECK(mat.metallicRoughnessMap.id == 0u);
}

TEST_CASE("PBRMaterial: aoMap can be set independently", "[renderer][pbr]") {
    PBRMaterial mat;
    mat.aoMap = TextureHandle{40};
    CHECK(mat.aoMap.id == 40u);
    CHECK(isValid(mat.aoMap));
    CHECK(mat.albedoMap.id == 0u);
}

TEST_CASE("PBRMaterial: emissiveMap can be set independently", "[renderer][pbr]") {
    PBRMaterial mat;
    mat.emissiveMap = TextureHandle{50};
    CHECK(mat.emissiveMap.id == 50u);
    CHECK(isValid(mat.emissiveMap));
    CHECK(mat.albedoMap.id == 0u);
}

// ===========================================================================
// ECS integration
// ===========================================================================

TEST_CASE("PBRMaterial: ECS emplace preserves all fields", "[renderer][pbr]") {
    World world;
    const EntityId eid = world.createEntity();

    PBRMaterial mat;
    mat.albedo = {0.1f, 0.2f, 0.3f, 0.9f};
    mat.metallic = 0.8f;
    mat.roughness = 0.3f;
    mat.normalScale = 1.5f;
    mat.ao = 0.6f;
    mat.emissiveFactor = {0.5f, 0.4f, 0.3f};
    mat.albedoMap = TextureHandle{11};
    mat.metallicRoughnessMap = TextureHandle{22};
    mat.normalMap = TextureHandle{33};
    mat.aoMap = TextureHandle{44};
    mat.emissiveMap = TextureHandle{55};
    world.registry().emplace<PBRMaterial>(static_cast<entt::entity>(eid), mat);

    const PBRMaterial& rb = world.getComponent<PBRMaterial>(eid);
    CHECK_THAT(rb.albedo.r, Catch::Matchers::WithinAbs(0.1f, 1e-6f));
    CHECK_THAT(rb.metallic, Catch::Matchers::WithinAbs(0.8f, 1e-6f));
    CHECK_THAT(rb.roughness, Catch::Matchers::WithinAbs(0.3f, 1e-6f));
    CHECK_THAT(rb.normalScale, Catch::Matchers::WithinAbs(1.5f, 1e-6f));
    CHECK_THAT(rb.ao, Catch::Matchers::WithinAbs(0.6f, 1e-6f));
    CHECK_THAT(rb.emissiveFactor.r, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
    CHECK(rb.albedoMap.id == 11u);
    CHECK(rb.metallicRoughnessMap.id == 22u);
    CHECK(rb.normalMap.id == 33u);
    CHECK(rb.aoMap.id == 44u);
    CHECK(rb.emissiveMap.id == 55u);
}

TEST_CASE("PBRMaterial: get_or_emplace creates with defaults", "[renderer][pbr]") {
    World world;
    const EntityId eid = world.createEntity();

    PBRMaterial& mat = world.registry().get_or_emplace<PBRMaterial>(
        static_cast<entt::entity>(eid));
    CHECK_THAT(mat.metallic, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(mat.roughness, Catch::Matchers::WithinAbs(0.5f, 1e-6f));
    CHECK(mat.albedoMap.id == 0u);
}
