// test_physics3d_raycast.cpp — Catch2 unit tests for 3D raycasting.
//
// Runs in ffe_tests_physics (isolated due to Jolt global state).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "physics/physics3d.h"

#include <cmath>
#include <limits>

using namespace ffe::physics;
using ffe::u32;
using ffe::f32;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helper: init/shutdown RAII guard
// ---------------------------------------------------------------------------
struct RaycastPhysicsGuard {
    RaycastPhysicsGuard() { REQUIRE(initPhysics3D()); }
    ~RaycastPhysicsGuard() { shutdownPhysics3D(); }
};

// =============================================================================
// castRay — single hit
// =============================================================================

TEST_CASE("castRay hits a box at known position", "[physics3d][raycast]") {
    RaycastPhysicsGuard pg;

    // Place a static box at (5, 0, 0).
    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    def.halfExtents = {1.0f, 1.0f, 1.0f};
    def.motionType = MotionType3D::STATIC;
    def.position = {5.0f, 0.0f, 0.0f};

    const u32 entityId = 99;
    const BodyHandle3D h = createBody(def, entityId);
    REQUIRE(isValid(h));

    // Step once so Jolt finalizes broadphase.
    stepPhysics3D(1.0f / 60.0f);

    // Cast a ray from origin toward the box.
    const RayHit3D hit = castRay({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f);

    CHECK(hit.valid);
    if (hit.valid) {
        CHECK(hit.entityId == entityId);
        CHECK(hit.distance > 0.0f);
        // Hit point should be near the box surface (x ~ 4.0, the near face).
        CHECK(hit.hitPoint.x == Approx(4.0f).margin(0.1f));
    }

    destroyBody(h);
}

TEST_CASE("castRay misses when aimed away from box", "[physics3d][raycast]") {
    RaycastPhysicsGuard pg;

    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    def.halfExtents = {1.0f, 1.0f, 1.0f};
    def.motionType = MotionType3D::STATIC;
    def.position = {5.0f, 0.0f, 0.0f};

    const BodyHandle3D h = createBody(def, 99);
    REQUIRE(isValid(h));
    stepPhysics3D(1.0f / 60.0f);

    // Cast a ray in the opposite direction.
    const RayHit3D hit = castRay({0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, 100.0f);
    CHECK_FALSE(hit.valid);

    destroyBody(h);
}

TEST_CASE("castRay returns no hit when maxDistance is too short", "[physics3d][raycast]") {
    RaycastPhysicsGuard pg;

    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    def.halfExtents = {1.0f, 1.0f, 1.0f};
    def.motionType = MotionType3D::STATIC;
    def.position = {10.0f, 0.0f, 0.0f};

    const BodyHandle3D h = createBody(def, 99);
    REQUIRE(isValid(h));
    stepPhysics3D(1.0f / 60.0f);

    // Box near face at x=9. MaxDist of 5 should miss.
    const RayHit3D hit = castRay({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 5.0f);
    CHECK_FALSE(hit.valid);

    destroyBody(h);
}

// =============================================================================
// castRayAll — multiple hits
// =============================================================================

TEST_CASE("castRayAll returns hits sorted by distance", "[physics3d][raycast]") {
    RaycastPhysicsGuard pg;

    // Place two boxes along the X axis.
    BodyDef3D def;
    def.shapeType = ShapeType3D::BOX;
    def.halfExtents = {0.5f, 0.5f, 0.5f};
    def.motionType = MotionType3D::STATIC;

    def.position = {3.0f, 0.0f, 0.0f};
    const BodyHandle3D hNear = createBody(def, 10);

    def.position = {8.0f, 0.0f, 0.0f};
    const BodyHandle3D hFar = createBody(def, 20);

    REQUIRE(isValid(hNear));
    REQUIRE(isValid(hFar));
    stepPhysics3D(1.0f / 60.0f);

    RayHit3D hits[MAX_RAY_HITS];
    const u32 hitCount = castRayAll({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f,
                                     hits, MAX_RAY_HITS);

    // Should get at least 2 hits.
    REQUIRE(hitCount >= 2);

    // Verify sorted by distance (nearest first).
    for (u32 i = 1; i < hitCount; ++i) {
        CHECK(hits[i].distance >= hits[i - 1].distance);
    }

    // First hit should be the nearer box (entity 10).
    CHECK(hits[0].entityId == 10);

    destroyBody(hNear);
    destroyBody(hFar);
}

// =============================================================================
// NaN / Inf safety
// =============================================================================

TEST_CASE("castRay with NaN origin does not crash", "[physics3d][raycast][nan]") {
    RaycastPhysicsGuard pg;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const RayHit3D hit = castRay({nan, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f);
    CHECK_FALSE(hit.valid);
}

TEST_CASE("castRay with Inf direction does not crash", "[physics3d][raycast][nan]") {
    RaycastPhysicsGuard pg;

    const float inf = std::numeric_limits<float>::infinity();
    const RayHit3D hit = castRay({0.0f, 0.0f, 0.0f}, {inf, 0.0f, 0.0f}, 100.0f);
    CHECK_FALSE(hit.valid);
}

TEST_CASE("castRayAll with NaN returns 0 hits", "[physics3d][raycast][nan]") {
    RaycastPhysicsGuard pg;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    RayHit3D hits[MAX_RAY_HITS];
    const u32 hitCount = castRayAll({nan, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f,
                                     hits, MAX_RAY_HITS);
    CHECK(hitCount == 0);
}
