// test_physics3d_collision_bindings.cpp — Catch2 unit tests for the 3D
// collision callback and raycasting Lua bindings.
//
// Runs in ffe_tests_scripting which links ffe_scripting (and transitively
// ffe_physics with Jolt).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "renderer/render_system.h"  // Transform3D
#include "physics/physics3d.h"
#include "physics/physics3d_system.h"

#include <cmath>

using Catch::Approx;

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World + 3D physics initialized
// ---------------------------------------------------------------------------
struct Collision3DScriptFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    Collision3DScriptFixture() {
        REQUIRE(engine.init());
        REQUIRE(ffe::physics::initPhysics3D());
        engine.setWorld(&world);
    }
    ~Collision3DScriptFixture() {
        engine.shutdown();
        ffe::physics::shutdownPhysics3D();
    }
};

// =============================================================================
// ffe.onCollision3D binding exists
// =============================================================================

TEST_CASE("onCollision3D binding accepts a function", "[scripting][physics3d][collision]") {
    Collision3DScriptFixture fix;

    REQUIRE(fix.engine.doString(
        "ffe.onCollision3D(function(a, b, px, py, pz, nx, ny, nz, evtType) end)"));
}

TEST_CASE("onCollision3D rejects non-function argument", "[scripting][physics3d][collision]") {
    Collision3DScriptFixture fix;

    // Should error — argument is a number, not a function.
    CHECK_FALSE(fix.engine.doString("ffe.onCollision3D(42)"));
}

// =============================================================================
// ffe.removeCollision3DCallback binding exists
// =============================================================================

TEST_CASE("removeCollision3DCallback does not error", "[scripting][physics3d][collision]") {
    Collision3DScriptFixture fix;

    // Register then remove.
    REQUIRE(fix.engine.doString(
        "ffe.onCollision3D(function() end)\n"
        "ffe.removeCollision3DCallback()"));
}

TEST_CASE("removeCollision3DCallback is safe when no callback registered",
          "[scripting][physics3d][collision]") {
    Collision3DScriptFixture fix;

    // Should not error even if no callback was ever set.
    REQUIRE(fix.engine.doString("ffe.removeCollision3DCallback()"));
}

// =============================================================================
// ffe.castRay binding
// =============================================================================

TEST_CASE("castRay binding returns 8 values on hit", "[scripting][physics3d][raycast]") {
    Collision3DScriptFixture fix;

    // Create a box at (5, 0, 0) via C++ so the ray has something to hit.
    const ffe::EntityId eid = fix.world.createEntity();
    ffe::Transform3D t3d;
    t3d.position = {5.0f, 0.0f, 0.0f};
    fix.world.registry().emplace<ffe::Transform3D>(static_cast<entt::entity>(eid), t3d);

    ffe::physics::BodyDef3D def;
    def.shapeType = ffe::physics::ShapeType3D::BOX;
    def.halfExtents = {1.0f, 1.0f, 1.0f};
    def.motionType = ffe::physics::MotionType3D::STATIC;
    def.position = {5.0f, 0.0f, 0.0f};
    const auto handle = ffe::physics::createBody(def, static_cast<ffe::u32>(eid));
    REQUIRE(ffe::physics::isValid(handle));

    ffe::RigidBody3D rb;
    rb.handle = handle;
    rb.initialized = true;
    fix.world.registry().emplace<ffe::RigidBody3D>(static_cast<entt::entity>(eid), rb);

    // Step once so Jolt finalizes broadphase.
    ffe::physics::stepPhysics3D(1.0f / 60.0f);

    // Cast a ray from origin toward the box.
    REQUIRE(fix.engine.doString(
        "local entity, hx, hy, hz, nx, ny, nz, dist = ffe.castRay(0,0,0, 1,0,0, 100)\n"
        "assert(entity ~= nil, 'expected a hit')\n"
        "assert(type(entity) == 'number', 'entity should be a number')\n"
        "assert(dist > 0, 'distance should be positive')\n"
        "assert(type(hx) == 'number', 'hitX should be a number')\n"
        "assert(type(nx) == 'number', 'normalX should be a number')\n"
    ));
}

TEST_CASE("castRay binding returns nil on miss", "[scripting][physics3d][raycast]") {
    Collision3DScriptFixture fix;

    // No bodies in the world — ray should miss.
    REQUIRE(fix.engine.doString(
        "local result = ffe.castRay(0,0,0, 1,0,0, 100)\n"
        "assert(result == nil, 'expected nil on miss')\n"
    ));
}

TEST_CASE("castRay with NaN returns nil", "[scripting][physics3d][raycast][nan]") {
    Collision3DScriptFixture fix;

    REQUIRE(fix.engine.doString(
        "local result = ffe.castRay(0/0, 0, 0, 1, 0, 0, 100)\n"
        "assert(result == nil, 'NaN origin should return nil')\n"
    ));
}

// =============================================================================
// ffe.castRayAll binding
// =============================================================================

TEST_CASE("castRayAll binding returns a table", "[scripting][physics3d][raycast]") {
    Collision3DScriptFixture fix;

    // No bodies — should return empty table.
    REQUIRE(fix.engine.doString(
        "local hits = ffe.castRayAll(0,0,0, 1,0,0, 100)\n"
        "assert(type(hits) == 'table', 'expected a table')\n"
        "assert(#hits == 0, 'expected empty table with no bodies')\n"
    ));
}

TEST_CASE("castRayAll returns table entries with correct fields", "[scripting][physics3d][raycast]") {
    Collision3DScriptFixture fix;

    // Create a box.
    ffe::physics::BodyDef3D def;
    def.shapeType = ffe::physics::ShapeType3D::BOX;
    def.halfExtents = {1.0f, 1.0f, 1.0f};
    def.motionType = ffe::physics::MotionType3D::STATIC;
    def.position = {5.0f, 0.0f, 0.0f};
    const auto handle = ffe::physics::createBody(def, 77);
    REQUIRE(ffe::physics::isValid(handle));
    ffe::physics::stepPhysics3D(1.0f / 60.0f);

    REQUIRE(fix.engine.doString(
        "local hits = ffe.castRayAll(0,0,0, 1,0,0, 100)\n"
        "assert(type(hits) == 'table', 'expected a table')\n"
        "if #hits > 0 then\n"
        "  local h = hits[1]\n"
        "  assert(type(h.entity) == 'number', 'entity field required')\n"
        "  assert(type(h.x) == 'number', 'x field required')\n"
        "  assert(type(h.y) == 'number', 'y field required')\n"
        "  assert(type(h.z) == 'number', 'z field required')\n"
        "  assert(type(h.nx) == 'number', 'nx field required')\n"
        "  assert(type(h.ny) == 'number', 'ny field required')\n"
        "  assert(type(h.nz) == 'number', 'nz field required')\n"
        "  assert(type(h.distance) == 'number', 'distance field required')\n"
        "end\n"
    ));

    ffe::physics::destroyBody(handle);
}

// =============================================================================
// ffe.dispatchCollision3DEvents binding (used internally for dispatch)
// =============================================================================

TEST_CASE("dispatchCollision3DEvents binding exists and does not error",
          "[scripting][physics3d][collision]") {
    Collision3DScriptFixture fix;

    // Should be safe to call even with no callback registered.
    REQUIRE(fix.engine.doString("ffe.dispatchCollision3DEvents()"));
}
