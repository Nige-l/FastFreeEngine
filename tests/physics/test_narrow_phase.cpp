#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "physics/narrow_phase.h"
#include "physics/collider2d.h"
#include "physics/collision_system.h"
#include "core/ecs.h"
#include "core/arena_allocator.h"
#include "renderer/render_system.h" // Transform

using namespace ffe;

// =============================================================================
// AABB vs AABB narrow phase
// =============================================================================

TEST_CASE("AABB-AABB: overlapping boxes report overlap", "[physics][narrow_phase][aabb]") {
    // Two 2x2 boxes, centers 1 unit apart on X. Overlap is 1 unit wide.
    REQUIRE(overlapAABB(0.0f, 0.0f, 1.0f, 1.0f,
                        1.0f, 0.0f, 1.0f, 1.0f));
}

TEST_CASE("AABB-AABB: touching edges do NOT report overlap (strict less-than)", "[physics][narrow_phase][aabb]") {
    // Two 2x2 boxes, centers exactly 2 units apart on X.
    // dx == halfAw + halfBw, so strict < fails.
    REQUIRE_FALSE(overlapAABB(0.0f, 0.0f, 1.0f, 1.0f,
                              2.0f, 0.0f, 1.0f, 1.0f));
}

TEST_CASE("AABB-AABB: separated on X axis", "[physics][narrow_phase][aabb]") {
    REQUIRE_FALSE(overlapAABB(0.0f, 0.0f, 1.0f, 1.0f,
                              5.0f, 0.0f, 1.0f, 1.0f));
}

TEST_CASE("AABB-AABB: separated on Y axis", "[physics][narrow_phase][aabb]") {
    REQUIRE_FALSE(overlapAABB(0.0f, 0.0f, 1.0f, 1.0f,
                              0.0f, 5.0f, 1.0f, 1.0f));
}

TEST_CASE("AABB-AABB: identical position and size overlap", "[physics][narrow_phase][aabb]") {
    REQUIRE(overlapAABB(3.0f, 4.0f, 2.0f, 2.0f,
                        3.0f, 4.0f, 2.0f, 2.0f));
}

TEST_CASE("AABB-AABB: zero-size AABB does not overlap anything", "[physics][narrow_phase][aabb]") {
    // A point at (0,0) vs a 2x2 box at (0,0).
    // halfW=0 means extent sum = 0+1=1, dx=0 < 1 on X. halfH=0 means sum=0+1=1, dy=0 < 1.
    // Actually this overlaps because 0 < 1 on both axes.
    // But two zero-size at same position: 0 < 0 is false.
    REQUIRE_FALSE(overlapAABB(0.0f, 0.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("AABB-AABB: one zero-size AABB inside another box", "[physics][narrow_phase][aabb]") {
    // Point at origin vs 2x2 box centered at origin.
    // dx=0 < (0+1)=1 on X, dy=0 < (0+1)=1 on Y => overlap.
    REQUIRE(overlapAABB(0.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 1.0f));
}

// =============================================================================
// Circle vs Circle narrow phase
// =============================================================================

TEST_CASE("Circle-Circle: overlapping circles", "[physics][narrow_phase][circle]") {
    // Two circles of radius 2, centers 1 unit apart. Sum=4, dist=1 < 4.
    REQUIRE(overlapCircle(0.0f, 0.0f, 2.0f,
                          1.0f, 0.0f, 2.0f));
}

TEST_CASE("Circle-Circle: touching circles do NOT overlap (strict less-than)", "[physics][narrow_phase][circle]") {
    // Two circles of radius 1, centers exactly 2 units apart.
    // distSq=4, radiusSum=2, radiusSumSq=4. 4 < 4 is false.
    REQUIRE_FALSE(overlapCircle(0.0f, 0.0f, 1.0f,
                                2.0f, 0.0f, 1.0f));
}

TEST_CASE("Circle-Circle: separated circles", "[physics][narrow_phase][circle]") {
    REQUIRE_FALSE(overlapCircle(0.0f, 0.0f, 1.0f,
                                10.0f, 0.0f, 1.0f));
}

TEST_CASE("Circle-Circle: concentric circles overlap", "[physics][narrow_phase][circle]") {
    REQUIRE(overlapCircle(5.0f, 5.0f, 3.0f,
                          5.0f, 5.0f, 1.0f));
}

TEST_CASE("Circle-Circle: zero-radius circles at same point do not overlap", "[physics][narrow_phase][circle]") {
    // distSq=0, radiusSum=0, 0 < 0 is false.
    REQUIRE_FALSE(overlapCircle(0.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 0.0f));
}

// =============================================================================
// AABB vs Circle narrow phase
// =============================================================================

TEST_CASE("AABB-Circle: circle center inside AABB overlaps", "[physics][narrow_phase][aabb_circle]") {
    // AABB at (0,0) size 4x4 (half 2x2), circle at (0.5, 0.5) radius 1.
    REQUIRE(overlapAABBCircle(0.0f, 0.0f, 2.0f, 2.0f,
                              0.5f, 0.5f, 1.0f));
}

TEST_CASE("AABB-Circle: circle touching edge does NOT overlap (strict less-than)", "[physics][narrow_phase][aabb_circle]") {
    // AABB: center (0,0), half (2,2), so right edge at x=2.
    // Circle: center (3,0), radius 1 => left edge at x=2.
    // Closest point on AABB to circle center is (2,0). dx=1, dy=0. distSq=1, rSq=1. 1<1 false.
    REQUIRE_FALSE(overlapAABBCircle(0.0f, 0.0f, 2.0f, 2.0f,
                                    3.0f, 0.0f, 1.0f));
}

TEST_CASE("AABB-Circle: circle separated from AABB", "[physics][narrow_phase][aabb_circle]") {
    REQUIRE_FALSE(overlapAABBCircle(0.0f, 0.0f, 1.0f, 1.0f,
                                    10.0f, 10.0f, 1.0f));
}

TEST_CASE("AABB-Circle: circle overlapping at corner", "[physics][narrow_phase][aabb_circle]") {
    // AABB center (0,0), half (1,1). Corner at (1,1).
    // Circle center (1.5, 1.5), radius 1.
    // Closest point = (1,1). dx=0.5, dy=0.5. distSq=0.5, rSq=1. 0.5<1 => overlap.
    REQUIRE(overlapAABBCircle(0.0f, 0.0f, 1.0f, 1.0f,
                              1.5f, 1.5f, 1.0f));
}

TEST_CASE("AABB-Circle: circle near corner but not overlapping", "[physics][narrow_phase][aabb_circle]") {
    // AABB center (0,0), half (1,1). Corner at (1,1).
    // Circle center (2,2), radius 0.5.
    // Closest point = (1,1). dx=1, dy=1. distSq=2, rSq=0.25. 2<0.25 => false.
    REQUIRE_FALSE(overlapAABBCircle(0.0f, 0.0f, 1.0f, 1.0f,
                                    2.0f, 2.0f, 0.5f));
}

TEST_CASE("AABB-Circle: zero-radius circle inside AABB does not overlap", "[physics][narrow_phase][aabb_circle]") {
    // Circle center at AABB center. Closest pt = center. dist=0, rSq=0. 0<0 => false.
    REQUIRE_FALSE(overlapAABBCircle(0.0f, 0.0f, 2.0f, 2.0f,
                                    0.0f, 0.0f, 0.0f));
}

TEST_CASE("AABB-Circle: zero-size AABB with circle at same point", "[physics][narrow_phase][aabb_circle]") {
    // AABB is a point at (0,0). Circle at (0,0) radius 1.
    // Closest pt = (0,0). dx=0, dy=0. distSq=0, rSq=1. 0<1 => overlap.
    REQUIRE(overlapAABBCircle(0.0f, 0.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f));
}

// =============================================================================
// Collision system integration tests
// =============================================================================

// Helper: sets up a World with an ArenaAllocator in the ECS context.
struct CollisionFixture {
    ArenaAllocator arena{1024 * 1024}; // 1 MB
    World world;

    CollisionFixture() {
        world.registry().ctx().emplace<ArenaAllocator*>(&arena);
    }

    // Run the collision system and return the event list.
    const CollisionEventList& runCollision() {
        arena.reset();
        collisionSystem(world, 0.0f);
        if (!world.registry().ctx().contains<CollisionEventList>()) {
            // Emplace an empty one if the system didn't create it (0 entities).
            world.registry().ctx().emplace<CollisionEventList>();
        }
        return world.registry().ctx().get<CollisionEventList>();
    }
};

TEST_CASE("Collision system: two overlapping AABB entities generate a CollisionEvent", "[physics][collision_system]") {
    CollisionFixture fix;

    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 10.0f;
    c1.halfHeight = 10.0f;

    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {5.0f, 5.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::AABB;
    c2.halfWidth = 10.0f;
    c2.halfHeight = 10.0f;

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 1);
    // Canonical ordering: entityA < entityB.
    const EntityId lo = std::min(e1, e2);
    const EntityId hi = std::max(e1, e2);
    REQUIRE(events.events[0].entityA == lo);
    REQUIRE(events.events[0].entityB == hi);
}

TEST_CASE("Collision system: two separated AABB entities generate no events", "[physics][collision_system]") {
    CollisionFixture fix;

    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 1.0f;
    c1.halfHeight = 1.0f;

    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {500.0f, 500.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::AABB;
    c2.halfWidth = 1.0f;
    c2.halfHeight = 1.0f;

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 0);
}

TEST_CASE("Collision system: layer/mask filtering prevents collision", "[physics][collision_system]") {
    CollisionFixture fix;

    // Entity 1: layer=0x0001, mask=0x0001
    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 10.0f;
    c1.halfHeight = 10.0f;
    c1.layer = 0x0001;
    c1.mask  = 0x0001;

    // Entity 2: layer=0x0002, mask=0x0002 — no overlap with e1's layer/mask.
    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {1.0f, 1.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::AABB;
    c2.halfWidth = 10.0f;
    c2.halfHeight = 10.0f;
    c2.layer = 0x0002;
    c2.mask  = 0x0002;

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 0);
}

TEST_CASE("Collision system: bidirectional layer/mask — both must pass", "[physics][collision_system]") {
    CollisionFixture fix;

    // A.layer=0x0001, A.mask=0x0003  =>  A.layer & B.mask = 0x0001 & 0x0001 = 1 (pass)
    //                                    B.layer & A.mask = 0x0002 & 0x0003 = 2 (pass)
    // => Both pass, collision should happen.
    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 10.0f;
    c1.halfHeight = 10.0f;
    c1.layer = 0x0001;
    c1.mask  = 0x0003;

    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {1.0f, 1.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::AABB;
    c2.halfWidth = 10.0f;
    c2.halfHeight = 10.0f;
    c2.layer = 0x0002;
    c2.mask  = 0x0001;

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 1);
}

TEST_CASE("Collision system: one-directional layer/mask fail blocks collision", "[physics][collision_system]") {
    CollisionFixture fix;

    // A.layer=0x0001, A.mask=0x0001
    // B.layer=0x0002, B.mask=0x0001
    // Check: A.layer & B.mask = 0x0001 & 0x0001 = 1 (pass)
    //        B.layer & A.mask = 0x0002 & 0x0001 = 0 (FAIL)
    // => Collision blocked.
    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 10.0f;
    c1.halfHeight = 10.0f;
    c1.layer = 0x0001;
    c1.mask  = 0x0001;

    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {1.0f, 1.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::AABB;
    c2.halfWidth = 10.0f;
    c2.halfHeight = 10.0f;
    c2.layer = 0x0002;
    c2.mask  = 0x0001;

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 0);
}

TEST_CASE("Collision system: trigger entities still generate events", "[physics][collision_system]") {
    CollisionFixture fix;

    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 10.0f;
    c1.halfHeight = 10.0f;
    c1.isTrigger = true;

    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {1.0f, 1.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::AABB;
    c2.halfWidth = 10.0f;
    c2.halfHeight = 10.0f;
    c2.isTrigger = false;

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 1);
}

TEST_CASE("Collision system: event canonical ordering entityA < entityB", "[physics][collision_system]") {
    CollisionFixture fix;

    // Create entities — we want to verify ordering regardless of creation order.
    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 10.0f;
    c1.halfHeight = 10.0f;

    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {1.0f, 1.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::AABB;
    c2.halfWidth = 10.0f;
    c2.halfHeight = 10.0f;

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 1);
    REQUIRE(events.events[0].entityA < events.events[0].entityB);
}

TEST_CASE("Collision system: Circle-Circle collision generates event", "[physics][collision_system]") {
    CollisionFixture fix;

    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::CIRCLE;
    c1.halfWidth = 5.0f; // radius

    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {3.0f, 0.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::CIRCLE;
    c2.halfWidth = 5.0f; // radius

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 1);
}

TEST_CASE("Collision system: AABB-Circle collision generates event", "[physics][collision_system]") {
    CollisionFixture fix;

    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 10.0f;
    c1.halfHeight = 10.0f;

    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {8.0f, 0.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::CIRCLE;
    c2.halfWidth = 5.0f; // radius

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 1);
}

TEST_CASE("Collision system: Circle-AABB (reversed order) collision generates event", "[physics][collision_system]") {
    CollisionFixture fix;

    // Create circle entity first, AABB second — tests the else branch
    // in the narrow-phase dispatcher.
    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::CIRCLE;
    c1.halfWidth = 5.0f;

    const EntityId e2 = fix.world.createEntity();
    auto& t2 = fix.world.addComponent<Transform>(e2);
    t2.position = {3.0f, 0.0f, 0.0f};
    auto& c2 = fix.world.addComponent<Collider2D>(e2);
    c2.shape = ColliderShape::AABB;
    c2.halfWidth = 10.0f;
    c2.halfHeight = 10.0f;

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 1);
}

TEST_CASE("Collision system: no entities produces empty event list", "[physics][collision_system]") {
    CollisionFixture fix;
    const auto& events = fix.runCollision();
    REQUIRE(events.count == 0);
}

TEST_CASE("Collision system: single entity produces no events", "[physics][collision_system]") {
    CollisionFixture fix;

    const EntityId e1 = fix.world.createEntity();
    auto& t1 = fix.world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = fix.world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 10.0f;
    c1.halfHeight = 10.0f;

    const auto& events = fix.runCollision();
    REQUIRE(events.count == 0);
}

TEST_CASE("Collision system: three overlapping entities produce three events", "[physics][collision_system]") {
    CollisionFixture fix;

    // Three entities all at (0,0) with large colliders — all overlap each other.
    for (int i = 0; i < 3; ++i) {
        const EntityId e = fix.world.createEntity();
        auto& t = fix.world.addComponent<Transform>(e);
        t.position = {0.0f, 0.0f, 0.0f};
        auto& c = fix.world.addComponent<Collider2D>(e);
        c.shape = ColliderShape::AABB;
        c.halfWidth = 100.0f;
        c.halfHeight = 100.0f;
    }

    const auto& events = fix.runCollision();
    // 3 entities => C(3,2) = 3 pairs, all overlapping.
    REQUIRE(events.count == 3);
}

TEST_CASE("Collision system: no arena pointer means no crash", "[physics][collision_system]") {
    // World without ArenaAllocator* in context — system should skip gracefully.
    World world;

    const EntityId e1 = world.createEntity();
    auto& t1 = world.addComponent<Transform>(e1);
    t1.position = {0.0f, 0.0f, 0.0f};
    auto& c1 = world.addComponent<Collider2D>(e1);
    c1.shape = ColliderShape::AABB;
    c1.halfWidth = 10.0f;
    c1.halfHeight = 10.0f;

    // Must not crash — just returns without writing events.
    collisionSystem(world, 0.0f);
    // No event list should exist.
    REQUIRE_FALSE(world.registry().ctx().contains<CollisionEventList>());
}

// =============================================================================
// CollisionEvent and Collider2D static assertions
// =============================================================================

TEST_CASE("Collider2D is 16 bytes POD", "[physics][collider2d]") {
    REQUIRE(sizeof(Collider2D) == 16);
}

TEST_CASE("CollisionEvent is 8 bytes", "[physics][collider2d]") {
    REQUIRE(sizeof(CollisionEvent) == 8);
}

TEST_CASE("Collider2D default values", "[physics][collider2d]") {
    const Collider2D c{};
    REQUIRE(c.shape == ColliderShape::AABB);
    REQUIRE(c.isTrigger == false);
    REQUIRE(c.layer == 0xFFFF);
    REQUIRE(c.mask == 0xFFFF);
    REQUIRE(c.halfWidth == 0.0f);
    REQUIRE(c.halfHeight == 0.0f);
}

TEST_CASE("COLLISION_SYSTEM_PRIORITY is 200", "[physics][collision_system]") {
    REQUIRE(COLLISION_SYSTEM_PRIORITY == 200);
}
