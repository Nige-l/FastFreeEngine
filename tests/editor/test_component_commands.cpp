// test_component_commands.cpp — Unit tests for ModifyComponentCommand<T>.
//
// Tests that component modification commands execute, undo, and redo correctly
// for Transform, Transform3D, and Name components. Also tests graceful handling
// of invalid entities and integration with CommandHistory.
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
// ModifyComponentCommand<Transform>
// -----------------------------------------------------------------------

TEST_CASE("ModifyComponentCommand<Transform> execute changes value", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {1.0f, 2.0f, 0.0f};
    t.rotation = 0.0f;
    t.scale = {1.0f, 1.0f, 1.0f};

    const Transform oldVal = t;
    Transform newVal = t;
    newVal.position = {10.0f, 20.0f, 0.0f};
    newVal.rotation = 3.14f;
    newVal.scale = {2.0f, 2.0f, 1.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(world, entity, oldVal, newVal);
    cmd->execute();

    const auto& result = world.getComponent<Transform>(id);
    CHECK(result.position.x == Approx(10.0f));
    CHECK(result.position.y == Approx(20.0f));
    CHECK(result.rotation == Approx(3.14f));
    CHECK(result.scale.x == Approx(2.0f));
    CHECK(result.scale.y == Approx(2.0f));
}

TEST_CASE("ModifyComponentCommand<Transform> undo restores old value", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {5.0f, 6.0f, 0.0f};
    t.rotation = 1.0f;

    const Transform oldVal = t;
    Transform newVal = t;
    newVal.position = {50.0f, 60.0f, 0.0f};
    newVal.rotation = 2.0f;

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(world, entity, oldVal, newVal);
    cmd->execute();
    cmd->undo();

    const auto& result = world.getComponent<Transform>(id);
    CHECK(result.position.x == Approx(5.0f));
    CHECK(result.position.y == Approx(6.0f));
    CHECK(result.rotation == Approx(1.0f));
}

TEST_CASE("ModifyComponentCommand<Transform> redo reapplies new value", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {1.0f, 1.0f, 0.0f};

    const Transform oldVal = t;
    Transform newVal = t;
    newVal.position = {99.0f, 88.0f, 0.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(world, entity, oldVal, newVal);
    cmd->execute();
    cmd->undo();
    cmd->execute(); // redo is just execute again

    const auto& result = world.getComponent<Transform>(id);
    CHECK(result.position.x == Approx(99.0f));
    CHECK(result.position.y == Approx(88.0f));
}

// -----------------------------------------------------------------------
// ModifyComponentCommand<Transform3D>
// -----------------------------------------------------------------------

TEST_CASE("ModifyComponentCommand<Transform3D> execute changes value", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t3d = world.addComponent<Transform3D>(id);
    t3d.position = {1.0f, 2.0f, 3.0f};
    t3d.scale = {1.0f, 1.0f, 1.0f};

    const Transform3D oldVal = t3d;
    Transform3D newVal = t3d;
    newVal.position = {10.0f, 20.0f, 30.0f};
    newVal.scale = {4.0f, 5.0f, 6.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Transform3D>>(world, entity, oldVal, newVal);
    cmd->execute();

    const auto& result = world.getComponent<Transform3D>(id);
    CHECK(result.position.x == Approx(10.0f));
    CHECK(result.position.y == Approx(20.0f));
    CHECK(result.position.z == Approx(30.0f));
    CHECK(result.scale.x == Approx(4.0f));
    CHECK(result.scale.y == Approx(5.0f));
    CHECK(result.scale.z == Approx(6.0f));
}

TEST_CASE("ModifyComponentCommand<Transform3D> undo restores old value", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t3d = world.addComponent<Transform3D>(id);
    t3d.position = {7.0f, 8.0f, 9.0f};

    const Transform3D oldVal = t3d;
    Transform3D newVal = t3d;
    newVal.position = {70.0f, 80.0f, 90.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Transform3D>>(world, entity, oldVal, newVal);
    cmd->execute();
    cmd->undo();

    const auto& result = world.getComponent<Transform3D>(id);
    CHECK(result.position.x == Approx(7.0f));
    CHECK(result.position.y == Approx(8.0f));
    CHECK(result.position.z == Approx(9.0f));
}

TEST_CASE("ModifyComponentCommand<Transform3D> redo reapplies new value", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t3d = world.addComponent<Transform3D>(id);
    t3d.position = {1.0f, 1.0f, 1.0f};

    const Transform3D oldVal = t3d;
    Transform3D newVal = t3d;
    newVal.position = {100.0f, 200.0f, 300.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Transform3D>>(world, entity, oldVal, newVal);
    cmd->execute();
    cmd->undo();
    cmd->execute();

    const auto& result = world.getComponent<Transform3D>(id);
    CHECK(result.position.x == Approx(100.0f));
    CHECK(result.position.y == Approx(200.0f));
    CHECK(result.position.z == Approx(300.0f));
}

// -----------------------------------------------------------------------
// ModifyComponentCommand<Name>
// -----------------------------------------------------------------------

TEST_CASE("ModifyComponentCommand<Name> execute changes value", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& name = world.addComponent<Name>(id);
    std::strncpy(name.name, "Original", sizeof(name.name) - 1);

    const Name oldVal = name;
    Name newVal = {};
    std::strncpy(newVal.name, "Modified", sizeof(newVal.name) - 1);

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Name>>(world, entity, oldVal, newVal);
    cmd->execute();

    const auto& result = world.getComponent<Name>(id);
    CHECK(std::strcmp(result.name, "Modified") == 0);
}

TEST_CASE("ModifyComponentCommand<Name> undo restores old value", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& name = world.addComponent<Name>(id);
    std::strncpy(name.name, "Before", sizeof(name.name) - 1);

    const Name oldVal = name;
    Name newVal = {};
    std::strncpy(newVal.name, "After", sizeof(newVal.name) - 1);

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Name>>(world, entity, oldVal, newVal);
    cmd->execute();
    cmd->undo();

    const auto& result = world.getComponent<Name>(id);
    CHECK(std::strcmp(result.name, "Before") == 0);
}

TEST_CASE("ModifyComponentCommand<Name> redo reapplies new value", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& name = world.addComponent<Name>(id);
    std::strncpy(name.name, "Start", sizeof(name.name) - 1);

    const Name oldVal = name;
    Name newVal = {};
    std::strncpy(newVal.name, "End", sizeof(newVal.name) - 1);

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Name>>(world, entity, oldVal, newVal);
    cmd->execute();
    cmd->undo();
    cmd->execute();

    const auto& result = world.getComponent<Name>(id);
    CHECK(std::strcmp(result.name, "End") == 0);
}

// -----------------------------------------------------------------------
// Invalid entity safety
// -----------------------------------------------------------------------

TEST_CASE("ModifyComponentCommand with invalid entity does not crash on execute", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {1.0f, 2.0f, 0.0f};

    const Transform oldVal = t;
    Transform newVal = t;
    newVal.position = {10.0f, 20.0f, 0.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(world, entity, oldVal, newVal);

    // Destroy the entity before executing the command
    world.destroyEntity(id);

    // Should not crash — entity is invalid, command is a no-op
    cmd->execute();
}

TEST_CASE("ModifyComponentCommand with invalid entity does not crash on undo", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {1.0f, 2.0f, 0.0f};

    const Transform oldVal = t;
    Transform newVal = t;
    newVal.position = {10.0f, 20.0f, 0.0f};

    const auto entity = static_cast<entt::entity>(id);
    auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(world, entity, oldVal, newVal);

    // Execute first, then destroy, then undo
    cmd->execute();
    world.destroyEntity(id);

    // Should not crash — entity is invalid, undo is a no-op
    cmd->undo();
}

// -----------------------------------------------------------------------
// Integration with CommandHistory
// -----------------------------------------------------------------------

TEST_CASE("ModifyComponentCommand integrates with CommandHistory execute", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {0.0f, 0.0f, 0.0f};

    const Transform oldVal = t;
    Transform newVal = t;
    newVal.position = {42.0f, 84.0f, 0.0f};

    const auto entity = static_cast<entt::entity>(id);

    CommandHistory history;
    history.executeCommand(
        std::make_unique<ModifyComponentCommand<Transform>>(world, entity, oldVal, newVal));

    const auto& result = world.getComponent<Transform>(id);
    CHECK(result.position.x == Approx(42.0f));
    CHECK(result.position.y == Approx(84.0f));
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("ModifyComponentCommand integrates with CommandHistory undo", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {5.0f, 10.0f, 0.0f};

    const Transform oldVal = t;
    Transform newVal = t;
    newVal.position = {50.0f, 100.0f, 0.0f};

    const auto entity = static_cast<entt::entity>(id);

    CommandHistory history;
    history.executeCommand(
        std::make_unique<ModifyComponentCommand<Transform>>(world, entity, oldVal, newVal));
    history.undo();

    const auto& result = world.getComponent<Transform>(id);
    CHECK(result.position.x == Approx(5.0f));
    CHECK(result.position.y == Approx(10.0f));
    CHECK_FALSE(history.canUndo());
    CHECK(history.canRedo());
}

TEST_CASE("ModifyComponentCommand integrates with CommandHistory redo", "[editor][component_commands]") {
    World world;
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<Transform>(id);
    t.position = {3.0f, 7.0f, 0.0f};

    const Transform oldVal = t;
    Transform newVal = t;
    newVal.position = {30.0f, 70.0f, 0.0f};

    const auto entity = static_cast<entt::entity>(id);

    CommandHistory history;
    history.executeCommand(
        std::make_unique<ModifyComponentCommand<Transform>>(world, entity, oldVal, newVal));
    history.undo();
    history.redo();

    const auto& result = world.getComponent<Transform>(id);
    CHECK(result.position.x == Approx(30.0f));
    CHECK(result.position.y == Approx(70.0f));
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());
}
