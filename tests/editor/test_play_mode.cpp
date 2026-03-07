// test_play_mode.cpp — Unit tests for the editor play-in-editor snapshot/restore.
//
// Tests state transitions (play/pause/resume/stop) and verifies that the ECS
// World is correctly snapshotted on play() and restored on stop().
// All tests run headless — no GL context or ImGui required.

#include <catch2/catch_test_macros.hpp>

#include "editor/play_mode.h"
#include "renderer/render_system.h" // Transform, Name

#include <cstring>

using namespace ffe::editor;

// -----------------------------------------------------------------------
// State transitions
// -----------------------------------------------------------------------

TEST_CASE("PlayMode starts in EDITING state", "[editor][play_mode]") {
    PlayMode pm;
    REQUIRE(pm.state() == PlayState::EDITING);
}

TEST_CASE("PlayMode play transitions to PLAYING", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    pm.play(world);
    REQUIRE(pm.state() == PlayState::PLAYING);
}

TEST_CASE("PlayMode pause transitions from PLAYING to PAUSED", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    pm.play(world);
    pm.pause();
    REQUIRE(pm.state() == PlayState::PAUSED);
}

TEST_CASE("PlayMode resume transitions from PAUSED to PLAYING", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    pm.play(world);
    pm.pause();
    pm.resume();
    REQUIRE(pm.state() == PlayState::PLAYING);
}

TEST_CASE("PlayMode stop transitions to EDITING", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    pm.play(world);
    pm.stop(world);
    REQUIRE(pm.state() == PlayState::EDITING);
}

TEST_CASE("PlayMode stop from PAUSED transitions to EDITING", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    pm.play(world);
    pm.pause();
    pm.stop(world);
    REQUIRE(pm.state() == PlayState::EDITING);
}

// -----------------------------------------------------------------------
// No-op guards
// -----------------------------------------------------------------------

TEST_CASE("PlayMode play is no-op when already PLAYING", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    pm.play(world);
    pm.play(world); // should not crash or change state
    REQUIRE(pm.state() == PlayState::PLAYING);
}

TEST_CASE("PlayMode pause is no-op when EDITING", "[editor][play_mode]") {
    PlayMode pm;

    pm.pause();
    REQUIRE(pm.state() == PlayState::EDITING);
}

TEST_CASE("PlayMode resume is no-op when EDITING", "[editor][play_mode]") {
    PlayMode pm;

    pm.resume();
    REQUIRE(pm.state() == PlayState::EDITING);
}

TEST_CASE("PlayMode stop is no-op when EDITING", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    pm.stop(world);
    REQUIRE(pm.state() == PlayState::EDITING);
}

// -----------------------------------------------------------------------
// Snapshot and restore
// -----------------------------------------------------------------------

TEST_CASE("PlayMode play snapshots state and stop restores it", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    // Create an entity with a Name and Transform before play
    const auto e = world.createEntity();
    auto& name = world.addComponent<ffe::Name>(e);
    std::strncpy(name.name, "TestEntity", sizeof(name.name) - 1);
    name.name[sizeof(name.name) - 1] = '\0';

    auto& t = world.addComponent<ffe::Transform>(e);
    t.position.x = 42.0f;
    t.position.y = 7.0f;

    pm.play(world);
    REQUIRE(pm.state() == PlayState::PLAYING);

    // Modify the world during "play" — destroy the entity and create a new one
    world.destroyEntity(e);
    const auto e2 = world.createEntity();
    auto& name2 = world.addComponent<ffe::Name>(e2);
    std::strncpy(name2.name, "RuntimeEntity", sizeof(name2.name) - 1);
    name2.name[sizeof(name2.name) - 1] = '\0';

    // Stop — should restore original state
    pm.stop(world);
    REQUIRE(pm.state() == PlayState::EDITING);

    // The world should have exactly one entity with the original data
    int count = 0;
    bool foundOriginal = false;

    auto view = world.view<ffe::Name, ffe::Transform>();
    for (const auto entity : view) {
        ++count;
        const auto& n = view.get<ffe::Name>(entity);
        const auto& tr = view.get<ffe::Transform>(entity);
        if (std::strcmp(n.name, "TestEntity") == 0) {
            foundOriginal = true;
            REQUIRE(tr.position.x == 42.0f);
            REQUIRE(tr.position.y == 7.0f);
        }
    }

    REQUIRE(count == 1);
    REQUIRE(foundOriginal);
}

TEST_CASE("PlayMode stop with modified state restores original", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    // Create two entities
    const auto e1 = world.createEntity();
    auto& name1 = world.addComponent<ffe::Name>(e1);
    std::strncpy(name1.name, "Entity_A", sizeof(name1.name) - 1);
    name1.name[sizeof(name1.name) - 1] = '\0';
    auto& t1 = world.addComponent<ffe::Transform>(e1);
    t1.position.x = 10.0f;

    const auto e2 = world.createEntity();
    auto& name2 = world.addComponent<ffe::Name>(e2);
    std::strncpy(name2.name, "Entity_B", sizeof(name2.name) - 1);
    name2.name[sizeof(name2.name) - 1] = '\0';
    auto& t2 = world.addComponent<ffe::Transform>(e2);
    t2.position.x = 20.0f;

    pm.play(world);

    // Modify positions and add a third entity during play
    {
        auto playView = world.view<ffe::Transform>();
        for (const auto entity : playView) {
            auto& tr = playView.get<ffe::Transform>(entity);
            tr.position.x += 100.0f;
        }
    }

    const auto e3 = world.createEntity();
    auto& name3 = world.addComponent<ffe::Name>(e3);
    std::strncpy(name3.name, "Entity_C", sizeof(name3.name) - 1);
    name3.name[sizeof(name3.name) - 1] = '\0';

    // Stop — restore
    pm.stop(world);

    // Should have exactly 2 entities with original positions
    int count = 0;
    auto view = world.view<ffe::Name, ffe::Transform>();
    for (const auto entity : view) {
        ++count;
        const auto& n = view.get<ffe::Name>(entity);
        const auto& tr = view.get<ffe::Transform>(entity);

        if (std::strcmp(n.name, "Entity_A") == 0) {
            REQUIRE(tr.position.x == 10.0f);
        } else if (std::strcmp(n.name, "Entity_B") == 0) {
            REQUIRE(tr.position.x == 20.0f);
        } else {
            FAIL("Unexpected entity: " << n.name);
        }
    }

    REQUIRE(count == 2);
}

TEST_CASE("PlayMode empty world round-trips correctly", "[editor][play_mode]") {
    PlayMode pm;
    ffe::World world;

    // Play with empty world
    pm.play(world);

    // Add entities during play
    const auto e = world.createEntity();
    world.addComponent<ffe::Name>(e);

    // Stop — should restore to empty
    pm.stop(world);

    int count = 0;
    auto view = world.view<ffe::Name>();
    for ([[maybe_unused]] const auto entity : view) {
        ++count;
    }
    REQUIRE(count == 0);
}
