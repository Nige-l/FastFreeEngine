// test_entity_commands.cpp — Unit tests for CreateEntityCommand and DestroyEntityCommand.
//
// Tests that entity creation/destruction commands execute and undo correctly,
// preserving component data through the undo/redo cycle.
// All tests run headless — no GL context or ImGui required.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "editor/commands/entity_commands.h"
#include "editor/commands/command_history.h"
#include "core/ecs.h"
#include "renderer/render_system.h"

#include <cstring>
#include <memory>

using Catch::Approx;
using namespace ffe;
using namespace ffe::editor;

// -----------------------------------------------------------------------
// CreateEntityCommand
// -----------------------------------------------------------------------

TEST_CASE("CreateEntityCommand creates an entity with Name", "[editor][entity_commands]") {
    World world;

    CommandHistory history;
    history.executeCommand(std::make_unique<CreateEntityCommand>(world, "TestEntity"));

    // Should have exactly one entity
    uint32_t count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 1);

    // That entity should have a Name component
    auto view = world.view<Name>();
    bool found = false;
    for (auto entity : view) {
        const auto& name = view.get<Name>(entity);
        CHECK(std::strcmp(name.name, "TestEntity") == 0);
        found = true;
    }
    CHECK(found);
}

TEST_CASE("CreateEntityCommand undo destroys the created entity", "[editor][entity_commands]") {
    World world;

    CommandHistory history;
    history.executeCommand(std::make_unique<CreateEntityCommand>(world, "WillBeUndone"));

    // Entity exists
    uint32_t count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 1);

    history.undo();

    // Entity should be gone
    count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 0);
}

TEST_CASE("CreateEntityCommand redo re-creates the entity", "[editor][entity_commands]") {
    World world;

    CommandHistory history;
    history.executeCommand(std::make_unique<CreateEntityCommand>(world, "RedoMe"));
    history.undo();

    // Gone
    uint32_t count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 0);

    history.redo();

    // Back
    count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 1);

    // Name should still be correct
    auto view = world.view<Name>();
    for (auto entity : view) {
        CHECK(std::strcmp(view.get<Name>(entity).name, "RedoMe") == 0);
    }
}

// -----------------------------------------------------------------------
// DestroyEntityCommand
// -----------------------------------------------------------------------

TEST_CASE("DestroyEntityCommand destroys an entity", "[editor][entity_commands]") {
    World world;
    const EntityId id = world.createEntity();
    world.addComponent<Name>(id, Name{"Victim"});
    world.addComponent<Transform>(id);

    CommandHistory history;
    history.executeCommand(std::make_unique<DestroyEntityCommand>(world, id));

    // Entity should be gone
    uint32_t count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 0);
}

TEST_CASE("DestroyEntityCommand undo restores entity with components", "[editor][entity_commands]") {
    World world;
    const EntityId id = world.createEntity();

    // Set up components with specific values
    auto& name = world.addComponent<Name>(id);
    std::strncpy(name.name, "Restored", sizeof(name.name) - 1);

    auto& t = world.addComponent<Transform>(id);
    t.position = {42.0f, 99.0f, 0.0f};
    t.rotation = 1.5f;
    t.scale = {2.0f, 3.0f, 1.0f};

    CommandHistory history;
    history.executeCommand(std::make_unique<DestroyEntityCommand>(world, id));

    // Entity destroyed
    uint32_t count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 0);

    // Undo should restore it
    history.undo();

    count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 1);

    // Verify components are restored
    auto view = world.view<Name, Transform>();
    bool found = false;
    for (auto entity : view) {
        const auto& restoredName = view.get<Name>(entity);
        const auto& restoredTransform = view.get<Transform>(entity);

        CHECK(std::strcmp(restoredName.name, "Restored") == 0);
        CHECK(restoredTransform.position.x == Approx(42.0f));
        CHECK(restoredTransform.position.y == Approx(99.0f));
        CHECK(restoredTransform.rotation == Approx(1.5f));
        CHECK(restoredTransform.scale.x == Approx(2.0f));
        CHECK(restoredTransform.scale.y == Approx(3.0f));
        found = true;
    }
    CHECK(found);
}

TEST_CASE("DestroyEntityCommand preserves Transform3D on undo", "[editor][entity_commands]") {
    World world;
    const EntityId id = world.createEntity();

    auto& t3d = world.addComponent<Transform3D>(id);
    t3d.position = {1.0f, 2.0f, 3.0f};
    t3d.scale = {4.0f, 5.0f, 6.0f};

    CommandHistory history;
    history.executeCommand(std::make_unique<DestroyEntityCommand>(world, id));
    history.undo();

    auto view = world.view<Transform3D>();
    bool found = false;
    for (auto entity : view) {
        const auto& restored = view.get<Transform3D>(entity);
        CHECK(restored.position.x == Approx(1.0f));
        CHECK(restored.position.y == Approx(2.0f));
        CHECK(restored.position.z == Approx(3.0f));
        CHECK(restored.scale.x == Approx(4.0f));
        CHECK(restored.scale.y == Approx(5.0f));
        CHECK(restored.scale.z == Approx(6.0f));
        found = true;
    }
    CHECK(found);
}

TEST_CASE("DestroyEntityCommand preserves Material3D on undo", "[editor][entity_commands]") {
    World world;
    const EntityId id = world.createEntity();

    auto& mat = world.addComponent<Material3D>(id);
    mat.diffuseColor = {0.5f, 0.6f, 0.7f, 1.0f};
    mat.shininess = 64.0f;

    CommandHistory history;
    history.executeCommand(std::make_unique<DestroyEntityCommand>(world, id));
    history.undo();

    auto view = world.view<Material3D>();
    bool found = false;
    for (auto entity : view) {
        const auto& restored = view.get<Material3D>(entity);
        CHECK(restored.diffuseColor.r == Approx(0.5f));
        CHECK(restored.diffuseColor.g == Approx(0.6f));
        CHECK(restored.diffuseColor.b == Approx(0.7f));
        CHECK(restored.shininess == Approx(64.0f));
        found = true;
    }
    CHECK(found);
}

TEST_CASE("DestroyEntityCommand redo re-destroys the entity", "[editor][entity_commands]") {
    World world;
    const EntityId id = world.createEntity();
    world.addComponent<Name>(id, Name{"RedoDestroy"});

    CommandHistory history;
    history.executeCommand(std::make_unique<DestroyEntityCommand>(world, id));
    history.undo();

    // Entity should be back
    uint32_t count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 1);

    history.redo();

    // Entity should be gone again
    count = 0;
    for (auto [e] : world.registry().storage<entt::entity>().each()) { (void)e; ++count; }
    CHECK(count == 0);
}
