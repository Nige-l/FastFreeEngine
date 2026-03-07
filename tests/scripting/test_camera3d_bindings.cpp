// test_camera3d_bindings.cpp — Catch2 unit tests for the 3D camera convenience
// bindings (ffe.set3DCameraFPS, ffe.set3DCameraOrbit) and the mesh texture
// binding (ffe.setMeshTexture) added in Session 42.
//
// All tests construct a minimal ffe::World, emplace the required ECS context
// objects, call setWorld(), then drive the bindings via lua_pcall through
// engine.doString(). Camera state is read back from the Camera struct that was
// emplaced in the ECS context. Material3D state is read back from the entity's
// component.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/render_system.h"
#include "renderer/camera.h"
#include "renderer/rhi_types.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

struct ScriptFixture {
    ffe::ScriptEngine engine;
    ScriptFixture() { REQUIRE(engine.init()); }
    ~ScriptFixture() { engine.shutdown(); }
};

// CameraFixture: ScriptEngine + World + Camera emplaced in ECS context.
// The Camera* pointer is stored in ECS context (as Camera*) so the bindings
// can find it the same way Application does.
struct CameraFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;
    ffe::renderer::Camera cam;   // owned here; a pointer to it lives in ctx

    CameraFixture() {
        REQUIRE(engine.init());
        // Emplace Camera* in the ECS context — same pattern as Application.
        world.registry().ctx().emplace<ffe::renderer::Camera*>(&cam);
        engine.setWorld(&world);
    }
    ~CameraFixture() { engine.shutdown(); }
};

// =============================================================================
// ffe.set3DCameraFPS — basic cases
// =============================================================================

TEST_CASE("set3DCameraFPS: yaw=0 pitch=0 places target directly on +Z axis",
          "[scripting][camera3d][fps]") {
    CameraFixture fix;
    // position (0,0,0), yaw=0, pitch=0 → forward = (sin0, sin0, cos0) = (0,0,1)
    // target should be (0,0,1)
    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(0, 0, 0, 0, 0)"));
    CHECK(fix.cam.position.x == Catch::Approx(0.0f));
    CHECK(fix.cam.position.y == Catch::Approx(0.0f));
    CHECK(fix.cam.position.z == Catch::Approx(0.0f));
    CHECK(fix.cam.target.x == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(fix.cam.target.y == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(fix.cam.target.z == Catch::Approx(1.0f).margin(1e-5f));
}

TEST_CASE("set3DCameraFPS: position is preserved correctly",
          "[scripting][camera3d][fps]") {
    CameraFixture fix;
    // position (3,5,-2), yaw=0, pitch=0
    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(3, 5, -2, 0, 0)"));
    CHECK(fix.cam.position.x == Catch::Approx(3.0f));
    CHECK(fix.cam.position.y == Catch::Approx(5.0f));
    CHECK(fix.cam.position.z == Catch::Approx(-2.0f));
    // target = position + (0,0,1)
    CHECK(fix.cam.target.x == Catch::Approx(3.0f).margin(1e-5f));
    CHECK(fix.cam.target.y == Catch::Approx(5.0f).margin(1e-5f));
    CHECK(fix.cam.target.z == Catch::Approx(-1.0f).margin(1e-5f));
}

TEST_CASE("set3DCameraFPS: yaw=90 pitch=0 looks down +X axis",
          "[scripting][camera3d][fps]") {
    CameraFixture fix;
    // yaw=90°: forward = (cos0*sin90, sin0, cos0*cos90) = (1,0,0)
    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(0, 0, 0, 90, 0)"));
    CHECK(fix.cam.target.x == Catch::Approx(1.0f).margin(1e-5f));
    CHECK(fix.cam.target.y == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(fix.cam.target.z == Catch::Approx(0.0f).margin(1e-5f));
}

TEST_CASE("set3DCameraFPS: pitch=45 gives correct forward.y and xz magnitude",
          "[scripting][camera3d][fps]") {
    CameraFixture fix;
    // pitch=45, yaw=0: forward.y = sin(45) ≈ 0.7071, forward.xz = cos(45) ≈ 0.7071
    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(0, 0, 0, 0, 45)"));
    const float fy = fix.cam.target.y - fix.cam.position.y;
    const float fx = fix.cam.target.x - fix.cam.position.x;
    const float fz = fix.cam.target.z - fix.cam.position.z;
    const float xz_mag = std::sqrt(fx*fx + fz*fz);
    CHECK(fy      == Catch::Approx(0.7071f).margin(1e-4f));
    CHECK(xz_mag  == Catch::Approx(0.7071f).margin(1e-4f));
}

TEST_CASE("set3DCameraFPS: pitch clamped at 89 — pitch=91 same as pitch=89",
          "[scripting][camera3d][fps]") {
    CameraFixture fix;
    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(0, 0, 0, 0, 89)"));
    const float fy89 = fix.cam.target.y - fix.cam.position.y;

    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(0, 0, 0, 0, 91)"));
    const float fy91 = fix.cam.target.y - fix.cam.position.y;

    CHECK(fy89 == Catch::Approx(fy91).margin(1e-6f));
}

TEST_CASE("set3DCameraFPS: pitch clamped at -89 — pitch=-91 same as pitch=-89",
          "[scripting][camera3d][fps]") {
    CameraFixture fix;
    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(0, 0, 0, 0, -89)"));
    const float fyn89 = fix.cam.target.y - fix.cam.position.y;

    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(0, 0, 0, 0, -91)"));
    const float fyn91 = fix.cam.target.y - fix.cam.position.y;

    CHECK(fyn89 == Catch::Approx(fyn91).margin(1e-6f));
}

TEST_CASE("set3DCameraFPS: up vector is not modified",
          "[scripting][camera3d][fps]") {
    CameraFixture fix;
    fix.cam.up = {0.0f, 1.0f, 0.0f}; // default
    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(1, 2, 3, 45, 30)"));
    CHECK(fix.cam.up.x == Catch::Approx(0.0f));
    CHECK(fix.cam.up.y == Catch::Approx(1.0f));
    CHECK(fix.cam.up.z == Catch::Approx(0.0f));
}

TEST_CASE("set3DCameraFPS: no Camera in ECS context is a no-op",
          "[scripting][camera3d][fps]") {
    ScriptFixture fix;
    ffe::World world;
    // Do NOT emplace Camera* — binding must not crash
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(0, 0, 0, 0, 0)"));
}

TEST_CASE("set3DCameraFPS: no World is a no-op",
          "[scripting][camera3d][fps]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.set3DCameraFPS(0, 0, 0, 0, 0)"));
}

// =============================================================================
// ffe.set3DCameraOrbit — basic cases
// =============================================================================

TEST_CASE("set3DCameraOrbit: basic — radius=5 yaw=0 pitch=0 places camera on +Z axis",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    // orbit target=(0,0,0), radius=5, yaw=0, pitch=0
    // cx = 0 + 5*cos0*sin0 = 0
    // cy = 0 + 5*sin0      = 0
    // cz = 0 + 5*cos0*cos0 = 5
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, 5, 0, 0)"));
    CHECK(fix.cam.position.x == Catch::Approx(0.0f).margin(1e-4f));
    CHECK(fix.cam.position.y == Catch::Approx(0.0f).margin(1e-4f));
    CHECK(fix.cam.position.z == Catch::Approx(5.0f).margin(1e-4f));
    CHECK(fix.cam.target.x   == Catch::Approx(0.0f).margin(1e-4f));
    CHECK(fix.cam.target.y   == Catch::Approx(0.0f).margin(1e-4f));
    CHECK(fix.cam.target.z   == Catch::Approx(0.0f).margin(1e-4f));
}

TEST_CASE("set3DCameraOrbit: non-zero target offset is respected",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    // target=(1,2,3), radius=5, yaw=0, pitch=0 → camera at (1,2,8)
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(1, 2, 3, 5, 0, 0)"));
    CHECK(fix.cam.position.x == Catch::Approx(1.0f).margin(1e-4f));
    CHECK(fix.cam.position.y == Catch::Approx(2.0f).margin(1e-4f));
    CHECK(fix.cam.position.z == Catch::Approx(8.0f).margin(1e-4f));
    CHECK(fix.cam.target.x   == Catch::Approx(1.0f).margin(1e-4f));
    CHECK(fix.cam.target.y   == Catch::Approx(2.0f).margin(1e-4f));
    CHECK(fix.cam.target.z   == Catch::Approx(3.0f).margin(1e-4f));
}

TEST_CASE("set3DCameraOrbit: yaw=90 pitch=0 places camera on +X axis",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    // yaw=90: cx = 0 + 5*cos0*sin90 = 5, cy=0, cz = 0 + 5*cos0*cos90 = 0
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, 5, 90, 0)"));
    CHECK(fix.cam.position.x == Catch::Approx(5.0f).margin(1e-4f));
    CHECK(fix.cam.position.y == Catch::Approx(0.0f).margin(1e-4f));
    CHECK(fix.cam.position.z == Catch::Approx(0.0f).margin(1e-4f));
}

TEST_CASE("set3DCameraOrbit: radius=0 is a no-op (camera unchanged)",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    // Set a known state first
    fix.cam.position = {1.0f, 2.0f, 3.0f};
    fix.cam.target   = {0.0f, 0.0f, 0.0f};
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, 0, 0, 0)"));
    // position must be unchanged
    CHECK(fix.cam.position.x == Catch::Approx(1.0f));
    CHECK(fix.cam.position.y == Catch::Approx(2.0f));
    CHECK(fix.cam.position.z == Catch::Approx(3.0f));
}

TEST_CASE("set3DCameraOrbit: negative radius is a no-op",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    fix.cam.position = {7.0f, 8.0f, 9.0f};
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, -5, 0, 0)"));
    CHECK(fix.cam.position.x == Catch::Approx(7.0f));
    CHECK(fix.cam.position.y == Catch::Approx(8.0f));
    CHECK(fix.cam.position.z == Catch::Approx(9.0f));
}

TEST_CASE("set3DCameraOrbit: NaN radius is a no-op",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    fix.cam.position = {1.0f, 0.0f, 0.0f};
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, 0/0, 0, 0)"));
    CHECK(fix.cam.position.x == Catch::Approx(1.0f));
}

TEST_CASE("set3DCameraOrbit: Inf radius is a no-op",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    fix.cam.position = {2.0f, 0.0f, 0.0f};
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, math.huge, 0, 0)"));
    CHECK(fix.cam.position.x == Catch::Approx(2.0f));
}

TEST_CASE("set3DCameraOrbit: NaN target is a no-op",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    fix.cam.position = {3.0f, 0.0f, 0.0f};
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0/0, 0, 0, 5, 0, 0)"));
    CHECK(fix.cam.position.x == Catch::Approx(3.0f));
}

TEST_CASE("set3DCameraOrbit: pitch clamped at 85 — pitch=90 same as pitch=85",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, 5, 0, 85)"));
    const float cy85 = fix.cam.position.y;

    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, 5, 0, 90)"));
    const float cy90 = fix.cam.position.y;

    CHECK(cy85 == Catch::Approx(cy90).margin(1e-4f));
}

TEST_CASE("set3DCameraOrbit: orbit distance from target equals radius",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(1, 2, 3, 7, 30, 20)"));
    const float dx = fix.cam.position.x - fix.cam.target.x;
    const float dy = fix.cam.position.y - fix.cam.target.y;
    const float dz = fix.cam.position.z - fix.cam.target.z;
    const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    CHECK(dist == Catch::Approx(7.0f).margin(1e-3f));
}

TEST_CASE("set3DCameraOrbit: no Camera in ECS context is a no-op",
          "[scripting][camera3d][orbit]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, 5, 0, 0)"));
}

TEST_CASE("set3DCameraOrbit: no World is a no-op",
          "[scripting][camera3d][orbit]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, 5, 0, 0)"));
}

TEST_CASE("set3DCameraOrbit: up vector is not modified",
          "[scripting][camera3d][orbit]") {
    CameraFixture fix;
    fix.cam.up = {0.0f, 1.0f, 0.0f};
    REQUIRE(fix.engine.doString("ffe.set3DCameraOrbit(0, 0, 0, 5, 45, 30)"));
    CHECK(fix.cam.up.x == Catch::Approx(0.0f));
    CHECK(fix.cam.up.y == Catch::Approx(1.0f));
    CHECK(fix.cam.up.z == Catch::Approx(0.0f));
}

// =============================================================================
// ffe.setMeshTexture
// =============================================================================

TEST_CASE("setMeshTexture: sets Material3D.diffuseTexture on a valid entity",
          "[scripting][camera3d][texture]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    // Pre-emplace Material3D so we can check it was updated
    world.registry().emplace<ffe::Material3D>(static_cast<entt::entity>(eid));

    // Push entity ID as a Lua global so the doString call is readable
    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshTexture(_eid, 42)"));

    const ffe::Material3D* mat =
        world.registry().try_get<ffe::Material3D>(static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->diffuseTexture.id == 42u);
}

TEST_CASE("setMeshTexture: creates Material3D if not already present",
          "[scripting][camera3d][texture]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    // No Material3D emplaced — setMeshTexture must create it
    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshTexture(_eid, 7)"));

    const ffe::Material3D* mat =
        world.registry().try_get<ffe::Material3D>(static_cast<entt::entity>(eid));
    REQUIRE(mat != nullptr);
    CHECK(mat->diffuseTexture.id == 7u);
}

TEST_CASE("setMeshTexture: handle 0 clears texture (white fallback)",
          "[scripting][camera3d][texture]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    // Set a non-zero texture first
    auto& mat = world.registry().emplace<ffe::Material3D>(static_cast<entt::entity>(eid));
    mat.diffuseTexture = ffe::rhi::TextureHandle{99u};

    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshTexture(_eid, 0)"));

    const ffe::Material3D* matPtr =
        world.registry().try_get<ffe::Material3D>(static_cast<entt::entity>(eid));
    REQUIRE(matPtr != nullptr);
    CHECK(matPtr->diffuseTexture.id == 0u);
}

TEST_CASE("setMeshTexture: invalid entity ID is a no-op (no crash)",
          "[scripting][camera3d][texture]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    // 99999 is not a valid entity in a freshly constructed world
    REQUIRE(fix.engine.doString("ffe.setMeshTexture(99999, 5)"));
}

TEST_CASE("setMeshTexture: negative textureHandle is a no-op",
          "[scripting][camera3d][texture]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const ffe::EntityId eid = world.createEntity();
    auto& mat = world.registry().emplace<ffe::Material3D>(static_cast<entt::entity>(eid));
    mat.diffuseTexture = ffe::rhi::TextureHandle{11u};

    fix.engine.doString(
        ("_eid = " + std::to_string(static_cast<long long>(eid))).c_str());
    REQUIRE(fix.engine.doString("ffe.setMeshTexture(_eid, -1)"));

    // Texture should be unchanged
    const ffe::Material3D* matPtr =
        world.registry().try_get<ffe::Material3D>(static_cast<entt::entity>(eid));
    REQUIRE(matPtr != nullptr);
    CHECK(matPtr->diffuseTexture.id == 11u);
}

TEST_CASE("setMeshTexture: no World set is a no-op",
          "[scripting][camera3d][texture]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.setMeshTexture(1, 5)"));
}

TEST_CASE("setMeshTexture: non-number entityId argument is a no-op",
          "[scripting][camera3d][texture]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("ffe.setMeshTexture('hello', 5)"));
}
