// test_component_add_remove.cpp — Unit tests for AddComponentCommand and
// RemoveComponentCommand.
//
// Tests that add/remove commands execute, undo, and redo correctly for
// multiple component types. Also tests integration with CommandHistory
// and chained add-then-remove-then-undo sequences.
// All tests run headless — no GL context or ImGui required.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "editor/commands/component_commands.h"
#include "editor/commands/command_history.h"
#include "core/ecs.h"
#include "renderer/render_system.h"

#include <cstring>
#include <memory>

using Catch::Approx;
using namespace ffe;
using namespace ffe::editor;

// -----------------------------------------------------------------------
// AddComponentCommand
// -----------------------------------------------------------------------

TEST_CASE("AddComponentCommand execute adds component", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    CHECK_FALSE(world.hasComponent<Transform>(id));

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<AddComponentCommand<Transform>>(world, entity);
    cmd->execute();

    CHECK(world.hasComponent<Transform>(id));
}

TEST_CASE("AddComponentCommand undo removes component", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    const auto entity = static_cast<entt::entity>(id);

    auto cmd = std::make_unique<AddComponentCommand<Transform>>(world, entity);
    cmd->execute();
    CHECK(world.hasComponent<Transform>(id));

    cmd->undo();
    CHECK_FALSE(world.hasComponent<Transform>(id));
}

TEST_CASE("AddComponentCommand execute is no-op if component already exists", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {42.0f, 84.0f, 0.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<AddComponentCommand<Transform>>(world, entity);
    cmd->execute();

    // Component should still be there with the original value
    CHECK(world.hasComponent<Transform>(id));
    CHECK(world.getComponent<Transform>(id).position.x == Approx(42.0f));
}

TEST_CASE("AddComponentCommand with invalid entity does not crash", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    const auto entity = static_cast<entt::entity>(id);
    world.destroyEntity(id);

    auto cmd = std::make_unique<AddComponentCommand<Transform>>(world, entity);
    cmd->execute(); // Should not crash
    cmd->undo();    // Should not crash
}

// -----------------------------------------------------------------------
// RemoveComponentCommand
// -----------------------------------------------------------------------

TEST_CASE("RemoveComponentCommand execute removes component", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {10.0f, 20.0f, 0.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<RemoveComponentCommand<Transform>>(world, entity);
    cmd->execute();

    CHECK_FALSE(world.hasComponent<Transform>(id));
}

TEST_CASE("RemoveComponentCommand undo restores with original data", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {10.0f, 20.0f, 0.0f};
    t.rotation = 1.5f;
    t.scale = {2.0f, 3.0f, 1.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<RemoveComponentCommand<Transform>>(world, entity);
    cmd->execute();
    CHECK_FALSE(world.hasComponent<Transform>(id));

    cmd->undo();
    CHECK(world.hasComponent<Transform>(id));

    const auto& restored = world.getComponent<Transform>(id);
    CHECK(restored.position.x == Approx(10.0f));
    CHECK(restored.position.y == Approx(20.0f));
    CHECK(restored.rotation == Approx(1.5f));
    CHECK(restored.scale.x == Approx(2.0f));
    CHECK(restored.scale.y == Approx(3.0f));
}

TEST_CASE("RemoveComponentCommand with Name restores original name", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    auto& name = world.addComponent<Name>(id);
    std::strncpy(name.name, "TestEntity", sizeof(name.name) - 1);

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<RemoveComponentCommand<Name>>(world, entity);
    cmd->execute();
    CHECK_FALSE(world.hasComponent<Name>(id));

    cmd->undo();
    CHECK(world.hasComponent<Name>(id));
    CHECK(std::strcmp(world.getComponent<Name>(id).name, "TestEntity") == 0);
}

TEST_CASE("RemoveComponentCommand with invalid entity does not crash", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    world.addComponent<Transform>(id);
    const auto entity = static_cast<entt::entity>(id);

    auto cmd = std::make_unique<RemoveComponentCommand<Transform>>(world, entity);
    world.destroyEntity(id);

    cmd->execute(); // Should not crash
    cmd->undo();    // Should not crash
}

// -----------------------------------------------------------------------
// Add then remove then undo chain
// -----------------------------------------------------------------------

TEST_CASE("Add then remove then undo chain", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    const auto entity = static_cast<entt::entity>(id);

    // Step 1: Add Transform via command
    auto addCmd = std::make_unique<AddComponentCommand<Transform>>(world, entity);
    addCmd->execute();
    CHECK(world.hasComponent<Transform>(id));

    // Modify it so we can check the snapshot
    world.getComponent<Transform>(id).position = {99.0f, 88.0f, 0.0f};

    // Step 2: Remove Transform via command
    auto removeCmd = std::make_unique<RemoveComponentCommand<Transform>>(world, entity);
    removeCmd->execute();
    CHECK_FALSE(world.hasComponent<Transform>(id));

    // Step 3: Undo the remove — should restore with {99, 88, 0}
    removeCmd->undo();
    CHECK(world.hasComponent<Transform>(id));
    CHECK(world.getComponent<Transform>(id).position.x == Approx(99.0f));
    CHECK(world.getComponent<Transform>(id).position.y == Approx(88.0f));

    // Step 4: Undo the add — should remove the component
    addCmd->undo();
    CHECK_FALSE(world.hasComponent<Transform>(id));
}

// -----------------------------------------------------------------------
// Integration with CommandHistory
// -----------------------------------------------------------------------

TEST_CASE("AddComponentCommand integrates with CommandHistory", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    const auto entity = static_cast<entt::entity>(id);

    CommandHistory history;
    history.executeCommand(
        std::make_unique<AddComponentCommand<Transform3D>>(world, entity));

    CHECK(world.hasComponent<Transform3D>(id));
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());

    history.undo();
    CHECK_FALSE(world.hasComponent<Transform3D>(id));
    CHECK(history.canRedo());

    history.redo();
    CHECK(world.hasComponent<Transform3D>(id));
}

TEST_CASE("RemoveComponentCommand integrates with CommandHistory", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {5.0f, 10.0f, 0.0f};
    const auto entity = static_cast<entt::entity>(id);

    CommandHistory history;
    history.executeCommand(
        std::make_unique<RemoveComponentCommand<Transform>>(world, entity));

    CHECK_FALSE(world.hasComponent<Transform>(id));
    CHECK(history.canUndo());

    history.undo();
    CHECK(world.hasComponent<Transform>(id));
    CHECK(world.getComponent<Transform>(id).position.x == Approx(5.0f));
    CHECK(world.getComponent<Transform>(id).position.y == Approx(10.0f));
}

TEST_CASE("Add and remove through CommandHistory full cycle", "[editor][add_remove_component]") {
    World world;
    const EntityId id = world.createEntity();
    const auto entity = static_cast<entt::entity>(id);

    CommandHistory history;

    // Add component
    history.executeCommand(
        std::make_unique<AddComponentCommand<Name>>(world, entity));
    CHECK(world.hasComponent<Name>(id));

    // Set a name so we can verify snapshot
    std::strncpy(world.getComponent<Name>(id).name, "Player", sizeof(Name::name) - 1);

    // Remove component
    history.executeCommand(
        std::make_unique<RemoveComponentCommand<Name>>(world, entity));
    CHECK_FALSE(world.hasComponent<Name>(id));

    // Undo remove — component restored with "Player"
    history.undo();
    CHECK(world.hasComponent<Name>(id));
    CHECK(std::strcmp(world.getComponent<Name>(id).name, "Player") == 0);

    // Undo add — component gone
    history.undo();
    CHECK_FALSE(world.hasComponent<Name>(id));

    // Redo add — component back (default value)
    history.redo();
    CHECK(world.hasComponent<Name>(id));
}
