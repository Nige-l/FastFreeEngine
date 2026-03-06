#include <catch2/catch_test_macros.hpp>
#include "core/ecs.h"

namespace {

struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
};

struct Health {
    int hp = 100;
};

void movementSystem(ffe::World& world, [[maybe_unused]] float dt) {
    auto v = world.view<Position, Velocity>();
    for (const auto entity : v) {
        auto& pos = v.get<Position>(entity);
        const auto& vel = v.get<Velocity>(entity);
        pos.x += vel.dx * dt;
        pos.y += vel.dy * dt;
    }
}

void healthSystem(ffe::World& world, [[maybe_unused]] float dt) {
    auto v = world.view<Health>();
    for (const auto entity : v) {
        auto& hp = v.get<Health>(entity);
        hp.hp -= 1;
    }
}

} // anonymous namespace

TEST_CASE("World entity creation and destruction", "[ecs]") {
    ffe::World world;

    const ffe::EntityId e1 = world.createEntity();
    const ffe::EntityId e2 = world.createEntity();

    REQUIRE(world.isValid(e1));
    REQUIRE(world.isValid(e2));
    REQUIRE(e1 != e2);

    world.destroyEntity(e1);
    REQUIRE_FALSE(world.isValid(e1));
    REQUIRE(world.isValid(e2));
}

TEST_CASE("World add and get components", "[ecs]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();

    auto& pos = world.addComponent<Position>(e, 10.0f, 20.0f);
    REQUIRE(pos.x == 10.0f);
    REQUIRE(pos.y == 20.0f);

    auto& retrieved = world.getComponent<Position>(e);
    REQUIRE(retrieved.x == 10.0f);
    REQUIRE(retrieved.y == 20.0f);

    REQUIRE(world.hasComponent<Position>(e));
    REQUIRE_FALSE(world.hasComponent<Velocity>(e));
}

TEST_CASE("World remove components", "[ecs]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();

    world.addComponent<Position>(e);
    REQUIRE(world.hasComponent<Position>(e));

    world.removeComponent<Position>(e);
    REQUIRE_FALSE(world.hasComponent<Position>(e));
}

TEST_CASE("World views iterate correctly", "[ecs]") {
    ffe::World world;

    // Create entities with different component sets
    const ffe::EntityId e1 = world.createEntity();
    world.addComponent<Position>(e1, 1.0f, 0.0f);
    world.addComponent<Velocity>(e1, 2.0f, 0.0f);

    const ffe::EntityId e2 = world.createEntity();
    world.addComponent<Position>(e2, 3.0f, 0.0f);
    // e2 has no Velocity

    const ffe::EntityId e3 = world.createEntity();
    world.addComponent<Position>(e3, 5.0f, 0.0f);
    world.addComponent<Velocity>(e3, 6.0f, 0.0f);

    // View with both Position and Velocity should yield e1 and e3
    int count = 0;
    auto v = world.view<Position, Velocity>();
    for ([[maybe_unused]] const auto entity : v) {
        ++count;
    }
    REQUIRE(count == 2);

    // View with only Position should yield all three
    count = 0;
    auto posView = world.view<Position>();
    for ([[maybe_unused]] const auto entity : posView) {
        ++count;
    }
    REQUIRE(count == 3);
}

TEST_CASE("World system registration and sorting", "[ecs]") {
    ffe::World world;

    // Register systems out of order
    world.registerSystem({"Health", 6, healthSystem, 200});
    world.registerSystem({"Movement", 8, movementSystem, 100});

    world.sortSystems();

    const auto& systems = world.systems();
    REQUIRE(systems.size() == 2);
    REQUIRE(systems[0].priority == 100);
    REQUIRE(systems[1].priority == 200);

    // Verify system execution works
    const ffe::EntityId e = world.createEntity();
    world.addComponent<Position>(e, 0.0f, 0.0f);
    world.addComponent<Velocity>(e, 10.0f, 5.0f);
    world.addComponent<Health>(e, 100);

    for (const auto& sys : systems) {
        sys.updateFn(world, 1.0f);
    }

    REQUIRE(world.getComponent<Position>(e).x == 10.0f);
    REQUIRE(world.getComponent<Position>(e).y == 5.0f);
    REQUIRE(world.getComponent<Health>(e).hp == 99);
}

TEST_CASE("Stale entity handle is invalid after reuse", "[ecs]") {
    ffe::World world;

    // Create and destroy an entity
    const ffe::EntityId e1 = world.createEntity();
    world.addComponent<Position>(e1, 1.0f, 2.0f);
    world.destroyEntity(e1);

    // Create a new entity — may reuse the same index with a different version
    const ffe::EntityId e2 = world.createEntity();

    // The old handle must no longer be valid (version mismatch)
    REQUIRE_FALSE(world.isValid(e1));
    REQUIRE(world.isValid(e2));
}

TEST_CASE("Destroyed entity with components does not appear in views", "[ecs]") {
    ffe::World world;

    const ffe::EntityId e1 = world.createEntity();
    world.addComponent<Position>(e1, 1.0f, 0.0f);
    world.addComponent<Velocity>(e1, 1.0f, 0.0f);

    const ffe::EntityId e2 = world.createEntity();
    world.addComponent<Position>(e2, 2.0f, 0.0f);
    world.addComponent<Velocity>(e2, 2.0f, 0.0f);

    world.destroyEntity(e1);

    int count = 0;
    auto v = world.view<Position, Velocity>();
    for (const auto entity : v) {
        (void)entity;
        ++count;
    }
    REQUIRE(count == 1);

    // The surviving entity should still be accessible
    REQUIRE(world.getComponent<Position>(e2).x == 2.0f);
}

TEST_CASE("Empty view iteration yields zero entities", "[ecs]") {
    ffe::World world;

    // No entities exist at all
    int count = 0;
    auto v = world.view<Position, Velocity>();
    for (const auto entity : v) {
        (void)entity;
        ++count;
    }
    REQUIRE(count == 0);

    // Create an entity with only Position — view requiring both should still be empty
    const ffe::EntityId e = world.createEntity();
    world.addComponent<Position>(e, 1.0f, 0.0f);

    count = 0;
    auto v2 = world.view<Position, Velocity>();
    for (const auto entity : v2) {
        (void)entity;
        ++count;
    }
    REQUIRE(count == 0);
}

TEST_CASE("Const World view and getComponent", "[ecs]") {
    ffe::World world;

    const ffe::EntityId e = world.createEntity();
    world.addComponent<Position>(e, 42.0f, 99.0f);
    world.addComponent<Health>(e, 75);

    // Access through a const reference
    const ffe::World& cworld = world;

    const auto& pos = cworld.getComponent<Position>(e);
    REQUIRE(pos.x == 42.0f);
    REQUIRE(pos.y == 99.0f);

    const auto& hp = cworld.getComponent<Health>(e);
    REQUIRE(hp.hp == 75);

    REQUIRE(cworld.hasComponent<Position>(e));
    REQUIRE_FALSE(cworld.hasComponent<Velocity>(e));

    // Const view iteration
    int count = 0;
    auto v = cworld.view<Position>();
    for (const auto entity : v) {
        (void)entity;
        ++count;
    }
    REQUIRE(count == 1);
}

TEST_CASE("World clearAllEntities removes all entities", "[ecs]") {
    ffe::World world;

    const ffe::EntityId e1 = world.createEntity();
    const ffe::EntityId e2 = world.createEntity();
    const ffe::EntityId e3 = world.createEntity();
    world.addComponent<Position>(e1, 1.0f, 2.0f);
    world.addComponent<Health>(e2, 100);
    world.addComponent<Position>(e3, 3.0f, 4.0f);
    world.addComponent<Health>(e3, 50);

    world.clearAllEntities();

    REQUIRE_FALSE(world.isValid(e1));
    REQUIRE_FALSE(world.isValid(e2));
    REQUIRE_FALSE(world.isValid(e3));

    // Views should be empty
    int count = 0;
    for (auto entity : world.view<Position>()) {
        (void)entity;
        ++count;
    }
    REQUIRE(count == 0);

    // Can create new entities after clearing
    const ffe::EntityId e4 = world.createEntity();
    REQUIRE(world.isValid(e4));
}
