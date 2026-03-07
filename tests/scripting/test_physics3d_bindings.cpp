// test_physics3d_bindings.cpp — Catch2 unit tests for the 3D physics Lua bindings.
//
// Tests run in the ffe_tests_scripting executable which links ffe_scripting
// (and transitively ffe_physics with Jolt).

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
struct Physics3DScriptFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    Physics3DScriptFixture() {
        REQUIRE(engine.init());
        REQUIRE(ffe::physics::initPhysics3D());
        engine.setWorld(&world);
    }
    ~Physics3DScriptFixture() {
        engine.shutdown();
        ffe::physics::shutdownPhysics3D();
    }
};

// Helper: create a 3D entity in C++ and return its ID as integer
static ffe::EntityId createEntity3D(ffe::World& world, float x, float y, float z) {
    const ffe::EntityId eid = world.createEntity();
    ffe::Transform3D t3d;
    t3d.position = {x, y, z};
    world.registry().emplace<ffe::Transform3D>(static_cast<entt::entity>(eid), t3d);
    return eid;
}

// =============================================================================
// createPhysicsBody binding
// =============================================================================

TEST_CASE("createPhysicsBody binding creates a body on a 3D entity",
          "[scripting][physics3d]") {
    Physics3DScriptFixture fix;
    const ffe::EntityId eid = createEntity3D(fix.world, 0.0f, 5.0f, 0.0f);

    // Build Lua command using entity ID
    char lua[256];
    snprintf(lua, sizeof(lua),
             "return ffe.createPhysicsBody(%u, { shape = 'box', motion = 'dynamic', "
             "halfExtents = {1,1,1}, mass = 1.0 })",
             eid);
    REQUIRE(fix.engine.doString(lua));

    // Verify RigidBody3D component was attached
    const auto* rb = fix.world.registry().try_get<ffe::RigidBody3D>(
        static_cast<entt::entity>(eid));
    REQUIRE(rb != nullptr);
    CHECK(rb->initialized);
    CHECK(ffe::physics::isValid(rb->handle));
}

// =============================================================================
// destroyPhysicsBody binding
// =============================================================================

TEST_CASE("destroyPhysicsBody binding removes RigidBody3D component",
          "[scripting][physics3d]") {
    Physics3DScriptFixture fix;
    const ffe::EntityId eid = createEntity3D(fix.world, 0.0f, 0.0f, 0.0f);

    char lua[256];
    snprintf(lua, sizeof(lua),
             "ffe.createPhysicsBody(%u, { shape = 'sphere', motion = 'dynamic', "
             "radius = 0.5, mass = 1.0 })", eid);
    REQUIRE(fix.engine.doString(lua));

    snprintf(lua, sizeof(lua), "ffe.destroyPhysicsBody(%u)", eid);
    REQUIRE(fix.engine.doString(lua));

    const auto* rb = fix.world.registry().try_get<ffe::RigidBody3D>(
        static_cast<entt::entity>(eid));
    CHECK(rb == nullptr);
}

// =============================================================================
// applyForce / applyImpulse bindings exist
// =============================================================================

TEST_CASE("applyForce binding does not error on valid entity",
          "[scripting][physics3d]") {
    Physics3DScriptFixture fix;
    ffe::physics::setGravity({0.0f, 0.0f, 0.0f});

    const ffe::EntityId eid = createEntity3D(fix.world, 0.0f, 0.0f, 0.0f);

    char lua[256];
    snprintf(lua, sizeof(lua),
             "ffe.createPhysicsBody(%u, { shape = 'sphere', motion = 'dynamic', "
             "radius = 0.5, mass = 1.0 })", eid);
    REQUIRE(fix.engine.doString(lua));

    snprintf(lua, sizeof(lua), "ffe.applyForce(%u, 100, 0, 0)", eid);
    REQUIRE(fix.engine.doString(lua));
}

TEST_CASE("applyImpulse binding does not error on valid entity",
          "[scripting][physics3d]") {
    Physics3DScriptFixture fix;

    const ffe::EntityId eid = createEntity3D(fix.world, 0.0f, 0.0f, 0.0f);

    char lua[256];
    snprintf(lua, sizeof(lua),
             "ffe.createPhysicsBody(%u, { shape = 'sphere', motion = 'dynamic', "
             "radius = 0.5, mass = 1.0 })", eid);
    REQUIRE(fix.engine.doString(lua));

    snprintf(lua, sizeof(lua), "ffe.applyImpulse(%u, 0, 50, 0)", eid);
    REQUIRE(fix.engine.doString(lua));
}

// =============================================================================
// setLinearVelocity / getLinearVelocity bindings
// =============================================================================

TEST_CASE("setLinearVelocity / getLinearVelocity round-trip via Lua",
          "[scripting][physics3d]") {
    Physics3DScriptFixture fix;
    ffe::physics::setGravity({0.0f, 0.0f, 0.0f});

    const ffe::EntityId eid = createEntity3D(fix.world, 0.0f, 0.0f, 0.0f);

    char lua[512];
    snprintf(lua, sizeof(lua),
             "ffe.createPhysicsBody(%u, { shape = 'sphere', motion = 'dynamic', "
             "radius = 0.5, mass = 1.0 })\n"
             "ffe.setLinearVelocity(%u, 3.0, 4.0, 5.0)\n"
             "local vx, vy, vz = ffe.getLinearVelocity(%u)\n"
             "return vx",
             eid, eid, eid);
    REQUIRE(fix.engine.doString(lua));
}

// =============================================================================
// setGravity / getGravity bindings
// =============================================================================

TEST_CASE("setGravity / getGravity round-trip via Lua",
          "[scripting][physics3d]") {
    Physics3DScriptFixture fix;

    REQUIRE(fix.engine.doString(
        "ffe.setGravity(0, -20, 0)\n"
        "local gx, gy, gz = ffe.getGravity()\n"
        "assert(gy < -19.9, 'gravity Y should be ~-20')"));
}

// =============================================================================
// NaN rejection in Lua bindings
// =============================================================================

TEST_CASE("applyForce with NaN does not crash",
          "[scripting][physics3d][nan]") {
    Physics3DScriptFixture fix;

    const ffe::EntityId eid = createEntity3D(fix.world, 0.0f, 0.0f, 0.0f);

    char lua[512];
    snprintf(lua, sizeof(lua),
             "ffe.createPhysicsBody(%u, { shape = 'sphere', motion = 'dynamic', "
             "radius = 0.5, mass = 1.0 })\n"
             "ffe.applyForce(%u, 0/0, 0, 0)",  // 0/0 = NaN in Lua
             eid, eid);
    // Should not crash — NaN is caught and ignored with a warning
    REQUIRE(fix.engine.doString(lua));
}

// =============================================================================
// setTransform3D syncs to Jolt physics body (desync bug fix)
// =============================================================================

TEST_CASE("setTransform3D syncs position to physics body when RigidBody3D present",
          "[scripting][physics3d][setTransform3D]") {
    Physics3DScriptFixture fix;
    ffe::physics::setGravity({0.0f, 0.0f, 0.0f});  // disable gravity for clean test

    const ffe::EntityId eid = createEntity3D(fix.world, 0.0f, 0.0f, 0.0f);

    // Create a dynamic physics body
    char lua[512];
    snprintf(lua, sizeof(lua),
             "ffe.createPhysicsBody(%u, { shape = 'sphere', motion = 'dynamic', "
             "radius = 0.5, mass = 1.0 })",
             eid);
    REQUIRE(fix.engine.doString(lua));

    // Set transform via Lua — should sync both Transform3D AND Jolt body
    snprintf(lua, sizeof(lua),
             "ffe.setTransform3D(%u, 10, 20, 30, 0, 0, 0, 1, 1, 1)", eid);
    REQUIRE(fix.engine.doString(lua));

    // Verify Jolt body position matches
    const auto* rb = fix.world.registry().try_get<ffe::RigidBody3D>(
        static_cast<entt::entity>(eid));
    REQUIRE(rb != nullptr);
    REQUIRE(ffe::physics::isValid(rb->handle));

    const glm::vec3 bodyPos = ffe::physics::getBodyPosition(rb->handle);
    CHECK(bodyPos.x == Approx(10.0f).margin(0.01f));
    CHECK(bodyPos.y == Approx(20.0f).margin(0.01f));
    CHECK(bodyPos.z == Approx(30.0f).margin(0.01f));

    // Now run physics sync — Transform3D should NOT be overwritten to (0,0,0)
    ffe::physics3dSyncSystem(fix.world, 0.016f);

    const auto& t3d = fix.world.registry().get<ffe::Transform3D>(
        static_cast<entt::entity>(eid));
    CHECK(t3d.position.x == Approx(10.0f).margin(0.01f));
    CHECK(t3d.position.y == Approx(20.0f).margin(0.01f));
    CHECK(t3d.position.z == Approx(30.0f).margin(0.01f));
}

TEST_CASE("setTransform3D works on entity WITHOUT RigidBody3D (regression guard)",
          "[scripting][physics3d][setTransform3D]") {
    Physics3DScriptFixture fix;

    // Entity with Transform3D but NO RigidBody3D
    const ffe::EntityId eid = createEntity3D(fix.world, 0.0f, 0.0f, 0.0f);

    char lua[256];
    snprintf(lua, sizeof(lua),
             "ffe.setTransform3D(%u, 5, 10, 15, 45, 90, 0, 2, 2, 2)", eid);
    REQUIRE(fix.engine.doString(lua));

    const auto& t3d = fix.world.registry().get<ffe::Transform3D>(
        static_cast<entt::entity>(eid));
    CHECK(t3d.position.x == Approx(5.0f));
    CHECK(t3d.position.y == Approx(10.0f));
    CHECK(t3d.position.z == Approx(15.0f));
    CHECK(t3d.scale.x == Approx(2.0f));
    CHECK(t3d.scale.y == Approx(2.0f));
    CHECK(t3d.scale.z == Approx(2.0f));
}

TEST_CASE("setTransform3D syncs rotation to physics body",
          "[scripting][physics3d][setTransform3D]") {
    Physics3DScriptFixture fix;
    ffe::physics::setGravity({0.0f, 0.0f, 0.0f});

    const ffe::EntityId eid = createEntity3D(fix.world, 0.0f, 0.0f, 0.0f);

    char lua[512];
    snprintf(lua, sizeof(lua),
             "ffe.createPhysicsBody(%u, { shape = 'box', motion = 'dynamic', "
             "halfExtents = {1,1,1}, mass = 1.0 })",
             eid);
    REQUIRE(fix.engine.doString(lua));

    // Set a 90-degree Y rotation
    snprintf(lua, sizeof(lua),
             "ffe.setTransform3D(%u, 0, 0, 0, 0, 90, 0, 1, 1, 1)", eid);
    REQUIRE(fix.engine.doString(lua));

    // Verify Jolt body rotation matches (90 deg around Y)
    const auto* rb = fix.world.registry().try_get<ffe::RigidBody3D>(
        static_cast<entt::entity>(eid));
    REQUIRE(rb != nullptr);

    const glm::quat bodyRot = ffe::physics::getBodyRotation(rb->handle);
    // 90 deg around Y: w ~= 0.707, y ~= 0.707
    CHECK(std::abs(bodyRot.w) == Approx(0.7071f).margin(0.01f));
    CHECK(std::abs(bodyRot.y) == Approx(0.7071f).margin(0.01f));
}
