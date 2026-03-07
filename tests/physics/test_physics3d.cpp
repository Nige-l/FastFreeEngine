// test_physics3d.cpp — Catch2 unit tests for the 3D rigid body physics system
// (Jolt Physics backend).
//
// Physics init/shutdown mutate global state, so these tests must run in the
// ffe_tests_physics executable (isolated from ffe_tests).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "physics/physics3d.h"

#include <cmath>
#include <limits>

using namespace ffe::physics;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helper: init/shutdown RAII guard
// ---------------------------------------------------------------------------
struct PhysicsGuard {
    PhysicsGuard() { REQUIRE(initPhysics3D()); }
    ~PhysicsGuard() { shutdownPhysics3D(); }
};

// =============================================================================
// Init / shutdown
// =============================================================================

TEST_CASE("initPhysics3D succeeds and reports initialized", "[physics3d][init]") {
    REQUIRE(initPhysics3D());
    CHECK(isPhysics3DInitialized());
    shutdownPhysics3D();
    CHECK_FALSE(isPhysics3DInitialized());
}

TEST_CASE("Double init is safe (no-op, returns true)", "[physics3d][init]") {
    REQUIRE(initPhysics3D());
    REQUIRE(initPhysics3D()); // second call — should be no-op
    shutdownPhysics3D();
}

TEST_CASE("Double shutdown is safe (no-op)", "[physics3d][init]") {
    REQUIRE(initPhysics3D());
    shutdownPhysics3D();
    shutdownPhysics3D(); // second call — should be no-op
}

// =============================================================================
// Body creation — box, sphere, capsule
// =============================================================================

TEST_CASE("Create box body returns valid handle", "[physics3d][body]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    def.halfExtents = {1.0f, 1.0f, 1.0f};
    const BodyHandle3D h = createBody(def);
    CHECK(isValid(h));
    destroyBody(h);
}

TEST_CASE("Create sphere body returns valid handle", "[physics3d][body]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::SPHERE;
    def.radius = 0.5f;
    const BodyHandle3D h = createBody(def);
    CHECK(isValid(h));
    destroyBody(h);
}

TEST_CASE("Create capsule body returns valid handle", "[physics3d][body]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::CAPSULE;
    def.radius = 0.3f;
    def.height = 0.5f;
    const BodyHandle3D h = createBody(def);
    CHECK(isValid(h));
    destroyBody(h);
}

// =============================================================================
// Dynamic body falls under gravity
// =============================================================================

TEST_CASE("Dynamic body falls under gravity after stepping", "[physics3d][simulation]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::SPHERE;
    def.radius = 0.5f;
    def.motionType = MotionType3D::DYNAMIC;
    def.position = {0.0f, 10.0f, 0.0f};
    def.mass = 1.0f;

    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));

    // Step a few times
    for (int i = 0; i < 10; ++i) {
        stepPhysics3D(1.0f / 60.0f);
    }

    const glm::vec3 pos = getBodyPosition(h);
    CHECK(pos.y < 10.0f); // should have fallen
    destroyBody(h);
}

// =============================================================================
// Static body does not move
// =============================================================================

TEST_CASE("Static body does not move under gravity", "[physics3d][simulation]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    def.halfExtents = {5.0f, 0.5f, 5.0f};
    def.motionType = MotionType3D::STATIC;
    def.position = {0.0f, 0.0f, 0.0f};

    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));

    for (int i = 0; i < 10; ++i) {
        stepPhysics3D(1.0f / 60.0f);
    }

    const glm::vec3 pos = getBodyPosition(h);
    CHECK(pos.y == Approx(0.0f).margin(0.001f));
    destroyBody(h);
}

// =============================================================================
// Apply impulse changes velocity immediately
// =============================================================================

TEST_CASE("applyImpulse changes velocity", "[physics3d][forces]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::SPHERE;
    def.radius = 0.5f;
    def.motionType = MotionType3D::DYNAMIC;
    def.position = {0.0f, 10.0f, 0.0f};
    def.mass = 1.0f;

    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));

    // Apply upward impulse
    applyImpulse(h, {0.0f, 100.0f, 0.0f});

    // Step once to let Jolt process it
    stepPhysics3D(1.0f / 60.0f);

    const glm::vec3 vel = getLinearVelocity(h);
    CHECK(vel.y > 0.0f); // should have upward velocity component
    destroyBody(h);
}

// =============================================================================
// Apply force changes velocity after step
// =============================================================================

TEST_CASE("applyForce changes velocity after step", "[physics3d][forces]") {
    PhysicsGuard pg;

    // Disable gravity so we only see the effect of the force
    setGravity({0.0f, 0.0f, 0.0f});

    BodyDef3D def;
    def.shapeType = ShapeType3D::SPHERE;
    def.radius = 0.5f;
    def.motionType = MotionType3D::DYNAMIC;
    def.position = {0.0f, 0.0f, 0.0f};
    def.mass = 1.0f;

    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));

    applyForce(h, {100.0f, 0.0f, 0.0f});
    stepPhysics3D(1.0f / 60.0f);

    const glm::vec3 vel = getLinearVelocity(h);
    CHECK(vel.x > 0.0f); // force should have accelerated in +X
    destroyBody(h);
}

// =============================================================================
// Get/set position round-trip
// =============================================================================

TEST_CASE("setBodyPosition / getBodyPosition round-trip", "[physics3d][position]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    def.motionType = MotionType3D::KINEMATIC;
    def.position = {0.0f, 0.0f, 0.0f};

    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));

    setBodyPosition(h, {3.0f, 7.0f, -2.0f});
    const glm::vec3 pos = getBodyPosition(h);
    CHECK(pos.x == Approx(3.0f).margin(0.01f));
    CHECK(pos.y == Approx(7.0f).margin(0.01f));
    CHECK(pos.z == Approx(-2.0f).margin(0.01f));
    destroyBody(h);
}

// =============================================================================
// Get/set rotation round-trip
// =============================================================================

TEST_CASE("setBodyRotation / getBodyRotation round-trip", "[physics3d][rotation]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    def.motionType = MotionType3D::KINEMATIC;

    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));

    // 90-degree rotation around Y axis
    const glm::quat expected = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    setBodyRotation(h, expected);
    const glm::quat actual = getBodyRotation(h);

    // Compare components (quaternions can be negated and still be equivalent)
    const float dot = std::abs(expected.w * actual.w + expected.x * actual.x +
                               expected.y * actual.y + expected.z * actual.z);
    CHECK(dot == Approx(1.0f).margin(0.01f));
    destroyBody(h);
}

// =============================================================================
// Set linear velocity
// =============================================================================

TEST_CASE("setLinearVelocity changes velocity", "[physics3d][velocity]") {
    PhysicsGuard pg;

    setGravity({0.0f, 0.0f, 0.0f});

    BodyDef3D def;
    def.shapeType = ShapeType3D::SPHERE;
    def.radius = 0.5f;
    def.motionType = MotionType3D::DYNAMIC;
    def.mass = 1.0f;

    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));

    setLinearVelocity(h, {5.0f, 0.0f, 0.0f});
    const glm::vec3 vel = getLinearVelocity(h);
    CHECK(vel.x == Approx(5.0f).margin(0.01f));
    destroyBody(h);
}

// =============================================================================
// Destroy body — safe, handle becomes invalid-ish
// =============================================================================

TEST_CASE("destroyBody is safe and does not crash", "[physics3d][body]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));
    destroyBody(h);
    // Double destroy is safe
    destroyBody(h);
}

// =============================================================================
// Destroy invalid handle is safe
// =============================================================================

TEST_CASE("destroyBody with invalid handle is no-op", "[physics3d][body]") {
    PhysicsGuard pg;
    destroyBody(BodyHandle3D{}); // sentinel value — no crash
}

// =============================================================================
// NaN/Inf guard on position
// =============================================================================

TEST_CASE("NaN position is sanitised to 0", "[physics3d][nan]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    def.motionType = MotionType3D::KINEMATIC;
    def.position = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};

    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));
    const glm::vec3 pos = getBodyPosition(h);
    CHECK(std::isfinite(pos.x));
    CHECK(std::isfinite(pos.y));
    CHECK(std::isfinite(pos.z));
    destroyBody(h);
}

TEST_CASE("Inf velocity is sanitised", "[physics3d][nan]") {
    PhysicsGuard pg;
    BodyDef3D def;
    def.shapeType = ShapeType3D::SPHERE;
    def.radius = 0.5f;
    def.motionType = MotionType3D::DYNAMIC;
    def.mass = 1.0f;

    const BodyHandle3D h = createBody(def);
    REQUIRE(isValid(h));

    setLinearVelocity(h, {std::numeric_limits<float>::infinity(), 0.0f, 0.0f});
    const glm::vec3 vel = getLinearVelocity(h);
    // Should be sanitised to 0 (not infinite)
    CHECK(std::isfinite(vel.x));
    destroyBody(h);
}

// =============================================================================
// Gravity get/set
// =============================================================================

TEST_CASE("setGravity / getGravity round-trip", "[physics3d][gravity]") {
    PhysicsGuard pg;
    setGravity({0.0f, -20.0f, 0.0f});
    const glm::vec3 g = getGravity();
    CHECK(g.x == Approx(0.0f));
    CHECK(g.y == Approx(-20.0f));
    CHECK(g.z == Approx(0.0f));
}
