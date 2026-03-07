// test_pbr_bindings.cpp -- Catch2 unit tests for PBR material Lua bindings.
//
// Tests validate: ffe.setPBRMaterial, ffe.setPBRTexture, ffe.removePBRMaterial.
//
// All tests construct a minimal ffe::World, call setWorld(), then drive the
// bindings via lua_pcall through engine.doString(). State is read back from ECS.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/pbr_material.h"
#include "renderer/rhi_types.h"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

struct PBRScriptFixture {
    ffe::ScriptEngine engine;
    PBRScriptFixture() { REQUIRE(engine.init()); }
    ~PBRScriptFixture() { engine.shutdown(); }
};

struct PBRWorldFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    PBRWorldFixture() {
        REQUIRE(engine.init());
        engine.setWorld(&world);
    }
    ~PBRWorldFixture() { engine.shutdown(); }

    ffe::EntityId makeEntity() {
        const ffe::EntityId eid = world.createEntity();
        engine.doString(
            ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
        return eid;
    }
};

// ===========================================================================
// ffe.setPBRMaterial — full params
// ===========================================================================

TEST_CASE("setPBRMaterial: full params table sets all fields",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString(
        "ffe.setPBRMaterial(_eid, {"
        "  albedo = {0.2, 0.3, 0.4, 0.8},"
        "  metallic = 0.9,"
        "  roughness = 0.7,"
        "  normalScale = 2.0,"
        "  ao = 0.6,"
        "  emissive = {1.0, 0.5, 0.25}"
        "})"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->albedo.r == Catch::Approx(0.2f));
    CHECK(mat->albedo.g == Catch::Approx(0.3f));
    CHECK(mat->albedo.b == Catch::Approx(0.4f));
    CHECK(mat->albedo.a == Catch::Approx(0.8f));
    CHECK(mat->metallic == Catch::Approx(0.9f));
    CHECK(mat->roughness == Catch::Approx(0.7f));
    CHECK(mat->normalScale == Catch::Approx(2.0f));
    CHECK(mat->ao == Catch::Approx(0.6f));
    CHECK(mat->emissiveFactor.r == Catch::Approx(1.0f));
    CHECK(mat->emissiveFactor.g == Catch::Approx(0.5f));
    CHECK(mat->emissiveFactor.b == Catch::Approx(0.25f));
}

// ===========================================================================
// ffe.setPBRMaterial — empty table uses defaults
// ===========================================================================

TEST_CASE("setPBRMaterial: empty table uses defaults",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString("ffe.setPBRMaterial(_eid, {})"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->albedo.r == Catch::Approx(1.0f));
    CHECK(mat->albedo.g == Catch::Approx(1.0f));
    CHECK(mat->albedo.b == Catch::Approx(1.0f));
    CHECK(mat->albedo.a == Catch::Approx(1.0f));
    CHECK(mat->metallic == Catch::Approx(0.0f));
    CHECK(mat->roughness == Catch::Approx(0.5f));
    CHECK(mat->normalScale == Catch::Approx(1.0f));
    CHECK(mat->ao == Catch::Approx(1.0f));
    CHECK(mat->emissiveFactor.r == Catch::Approx(0.0f));
    CHECK(mat->emissiveFactor.g == Catch::Approx(0.0f));
    CHECK(mat->emissiveFactor.b == Catch::Approx(0.0f));
}

// ===========================================================================
// ffe.setPBRMaterial — partial params uses defaults for missing
// ===========================================================================

TEST_CASE("setPBRMaterial: partial params uses defaults for missing fields",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString(
        "ffe.setPBRMaterial(_eid, { metallic = 0.5, roughness = 0.8 })"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    // Explicitly set fields
    CHECK(mat->metallic == Catch::Approx(0.5f));
    CHECK(mat->roughness == Catch::Approx(0.8f));
    // Defaults for omitted fields
    CHECK(mat->albedo.r == Catch::Approx(1.0f));
    CHECK(mat->albedo.a == Catch::Approx(1.0f));
    CHECK(mat->normalScale == Catch::Approx(1.0f));
    CHECK(mat->ao == Catch::Approx(1.0f));
    CHECK(mat->emissiveFactor.r == Catch::Approx(0.0f));
}

// ===========================================================================
// ffe.setPBRMaterial — clamping
// ===========================================================================

TEST_CASE("setPBRMaterial: metallic clamped to 0-1",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString(
        "ffe.setPBRMaterial(_eid, { metallic = 5.0 })"));
    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->metallic == Catch::Approx(1.0f));
}

TEST_CASE("setPBRMaterial: roughness clamped to 0.04-1.0",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString(
        "ffe.setPBRMaterial(_eid, { roughness = 0.0 })"));
    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->roughness == Catch::Approx(0.04f));
}

TEST_CASE("setPBRMaterial: ao clamped to 0-1",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString(
        "ffe.setPBRMaterial(_eid, { ao = -0.5 })"));
    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->ao == Catch::Approx(0.0f));
}

// ===========================================================================
// ffe.setPBRMaterial — no table arg still creates component with defaults
// ===========================================================================

TEST_CASE("setPBRMaterial: no table arg creates PBRMaterial with defaults",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString("ffe.setPBRMaterial(_eid)"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->metallic == Catch::Approx(0.0f));
    CHECK(mat->roughness == Catch::Approx(0.5f));
}

// ===========================================================================
// ffe.setPBRMaterial — error cases
// ===========================================================================

TEST_CASE("setPBRMaterial: invalid entity is a no-op",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    REQUIRE(fix.engine.doString("ffe.setPBRMaterial(99999, { metallic = 1.0 })"));
}

TEST_CASE("setPBRMaterial: no World set is a no-op",
          "[scripting][pbr]") {
    PBRScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.setPBRMaterial(1, { metallic = 1.0 })"));
}

TEST_CASE("setPBRMaterial: non-number entityId is a no-op",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    REQUIRE(fix.engine.doString("ffe.setPBRMaterial('hello', {})"));
}

// ===========================================================================
// ffe.setPBRTexture
// ===========================================================================

TEST_CASE("setPBRTexture: sets albedo texture slot",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 'albedo', 42)"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->albedoMap.id == 42u);
}

TEST_CASE("setPBRTexture: sets metallicRoughness texture slot",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 'metallicRoughness', 55)"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->metallicRoughnessMap.id == 55u);
}

TEST_CASE("setPBRTexture: sets normal texture slot",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 'normal', 33)"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->normalMap.id == 33u);
}

TEST_CASE("setPBRTexture: sets ao texture slot",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 'ao', 77)"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->aoMap.id == 77u);
}

TEST_CASE("setPBRTexture: sets emissive texture slot",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 'emissive', 99)"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->emissiveMap.id == 99u);
}

TEST_CASE("setPBRTexture: textureHandle 0 clears texture slot",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    // Set a texture first
    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 'albedo', 42)"));
    // Clear it
    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 'albedo', 0)"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->albedoMap.id == 0u);
}

TEST_CASE("setPBRTexture: negative textureHandle is a no-op",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    // Set a texture first
    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 'albedo', 42)"));
    // Try negative — should not change
    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 'albedo', -1)"));

    const auto* mat = fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->albedoMap.id == 42u);
}

TEST_CASE("setPBRTexture: invalid entity is a no-op",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    REQUIRE(fix.engine.doString("ffe.setPBRTexture(99999, 'albedo', 10)"));
}

TEST_CASE("setPBRTexture: non-string slot is a no-op",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();
    REQUIRE(fix.engine.doString("ffe.setPBRTexture(_eid, 123, 10)"));
    // PBRMaterial should not be created
    CHECK(fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid)) == nullptr);
}

TEST_CASE("setPBRTexture: no World set is a no-op",
          "[scripting][pbr]") {
    PBRScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.setPBRTexture(1, 'albedo', 10)"));
}

// ===========================================================================
// ffe.removePBRMaterial
// ===========================================================================

TEST_CASE("removePBRMaterial: removes PBRMaterial component",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    const ffe::EntityId eid = fix.makeEntity();

    // Add PBRMaterial first
    REQUIRE(fix.engine.doString("ffe.setPBRMaterial(_eid, { metallic = 0.5 })"));
    CHECK(fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid)) != nullptr);

    // Remove it
    REQUIRE(fix.engine.doString("ffe.removePBRMaterial(_eid)"));
    CHECK(fix.world.registry().try_get<ffe::renderer::PBRMaterial>(
        static_cast<entt::entity>(eid)) == nullptr);
}

TEST_CASE("removePBRMaterial: removing when not present is a no-op",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    (void)fix.makeEntity();
    REQUIRE(fix.engine.doString("ffe.removePBRMaterial(_eid)"));
}

TEST_CASE("removePBRMaterial: invalid entity is a no-op",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    REQUIRE(fix.engine.doString("ffe.removePBRMaterial(99999)"));
}

TEST_CASE("removePBRMaterial: no World set is a no-op",
          "[scripting][pbr]") {
    PBRScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.removePBRMaterial(1)"));
}

TEST_CASE("removePBRMaterial: non-number entityId is a no-op",
          "[scripting][pbr]") {
    PBRWorldFixture fix;
    REQUIRE(fix.engine.doString("ffe.removePBRMaterial('hello')"));
}
