// test_hierarchy_panel.cpp — Unit tests for ReparentCommand and hierarchy panel
// operations (create child, unparent). All tests run headless — no GL context
// or ImGui required. Tests exercise the command/ECS/scene-graph logic only.

#include <catch2/catch_test_macros.hpp>

#include "editor/commands/reparent_command.h"
#include "editor/commands/entity_commands.h"
#include "editor/commands/command_history.h"
#include "core/ecs.h"
#include "renderer/render_system.h"
#include "scene/scene_graph.h"

#include <cstring>
#include <memory>

using namespace ffe;
using namespace ffe::editor;

// -----------------------------------------------------------------------
// Helper: count living entities
// -----------------------------------------------------------------------
static uint32_t countEntities(const World& world) {
    uint32_t count = 0;
    for (auto [e] : world.registry().storage<entt::entity>()->each()) {
        (void)e;
        ++count;
    }
    return count;
}

// -----------------------------------------------------------------------
// ReparentCommand — execute
// -----------------------------------------------------------------------

TEST_CASE("ReparentCommand sets parent on execute", "[editor][hierarchy]") {
    World world;
    const auto parent = static_cast<entt::entity>(world.createEntity());
    const auto child  = static_cast<entt::entity>(world.createEntity());

    CommandHistory history;
    history.executeCommand(std::make_unique<ReparentCommand>(world, child, parent));

    // Child should now have parent.
    CHECK(ffe::scene::getParent(world, child) == parent);
    CHECK_FALSE(ffe::scene::isRoot(world, child));

    // Parent should list child.
    uint32_t childCount = 0;
    const entt::entity* children = ffe::scene::getChildren(world, parent, childCount);
    REQUIRE(childCount == 1);
    CHECK(children[0] == child);
}

// -----------------------------------------------------------------------
// ReparentCommand — undo restores old parent
// -----------------------------------------------------------------------

TEST_CASE("ReparentCommand undo restores previous parent", "[editor][hierarchy]") {
    World world;
    const auto grandparent = static_cast<entt::entity>(world.createEntity());
    const auto parent      = static_cast<entt::entity>(world.createEntity());
    const auto child       = static_cast<entt::entity>(world.createEntity());

    // Set up: child is under grandparent initially.
    ffe::scene::setParent(world, child, grandparent);
    CHECK(ffe::scene::getParent(world, child) == grandparent);

    // Reparent child under parent.
    CommandHistory history;
    history.executeCommand(std::make_unique<ReparentCommand>(world, child, parent));
    CHECK(ffe::scene::getParent(world, child) == parent);

    // Undo should restore child back under grandparent.
    history.undo();
    CHECK(ffe::scene::getParent(world, child) == grandparent);

    // Grandparent should list child again.
    uint32_t childCount = 0;
    const entt::entity* children = ffe::scene::getChildren(world, grandparent, childCount);
    REQUIRE(childCount == 1);
    CHECK(children[0] == child);

    // Parent should have no children.
    uint32_t parentChildCount = 0;
    ffe::scene::getChildren(world, parent, parentChildCount);
    CHECK(parentChildCount == 0);
}

// -----------------------------------------------------------------------
// ReparentCommand — undo from root-to-parent restores root
// -----------------------------------------------------------------------

TEST_CASE("ReparentCommand undo restores root status", "[editor][hierarchy]") {
    World world;
    const auto parent = static_cast<entt::entity>(world.createEntity());
    const auto child  = static_cast<entt::entity>(world.createEntity());

    // Child starts as root (no parent).
    CHECK(ffe::scene::isRoot(world, child));

    CommandHistory history;
    history.executeCommand(std::make_unique<ReparentCommand>(world, child, parent));
    CHECK_FALSE(ffe::scene::isRoot(world, child));

    history.undo();
    CHECK(ffe::scene::isRoot(world, child));
}

// -----------------------------------------------------------------------
// ReparentCommand — unparent (new parent is entt::null)
// -----------------------------------------------------------------------

TEST_CASE("ReparentCommand to null unparents entity", "[editor][hierarchy]") {
    World world;
    const auto parent = static_cast<entt::entity>(world.createEntity());
    const auto child  = static_cast<entt::entity>(world.createEntity());

    ffe::scene::setParent(world, child, parent);
    CHECK_FALSE(ffe::scene::isRoot(world, child));

    CommandHistory history;
    history.executeCommand(std::make_unique<ReparentCommand>(world, child, entt::null));

    CHECK(ffe::scene::isRoot(world, child));

    // Undo should re-parent.
    history.undo();
    CHECK(ffe::scene::getParent(world, child) == parent);
}

// -----------------------------------------------------------------------
// ReparentCommand — redo re-applies reparent
// -----------------------------------------------------------------------

TEST_CASE("ReparentCommand redo re-applies reparent", "[editor][hierarchy]") {
    World world;
    const auto parent = static_cast<entt::entity>(world.createEntity());
    const auto child  = static_cast<entt::entity>(world.createEntity());

    CommandHistory history;
    history.executeCommand(std::make_unique<ReparentCommand>(world, child, parent));
    CHECK(ffe::scene::getParent(world, child) == parent);

    history.undo();
    CHECK(ffe::scene::isRoot(world, child));

    history.redo();
    CHECK(ffe::scene::getParent(world, child) == parent);
}

// -----------------------------------------------------------------------
// Create child entity — verifies parent/child relationship
// -----------------------------------------------------------------------

TEST_CASE("Create child entity establishes parent-child relationship", "[editor][hierarchy]") {
    World world;
    const EntityId parentId = world.createEntity();
    world.addComponent<Name>(parentId, Name{"Parent"});
    const auto parentEntity = static_cast<entt::entity>(parentId);

    CommandHistory history;

    // Create a new entity.
    history.executeCommand(std::make_unique<CreateEntityCommand>(world, "Child"));
    CHECK(countEntities(world) == 2);

    // Find the child entity (the one that is not parentId).
    entt::entity childEntity = entt::null;
    auto& reg = world.registry();
    for (auto [e] : reg.storage<entt::entity>().each()) {
        if (e != parentEntity && reg.valid(e)) {
            childEntity = e;
        }
    }
    REQUIRE((childEntity != entt::null));

    // Reparent under parent.
    history.executeCommand(std::make_unique<ReparentCommand>(world, childEntity, parentEntity));

    CHECK(ffe::scene::getParent(world, childEntity) == parentEntity);

    uint32_t childCount = 0;
    const entt::entity* children = ffe::scene::getChildren(world, parentEntity, childCount);
    REQUIRE(childCount == 1);
    CHECK(children[0] == childEntity);

    // Verify the child has the correct name.
    CHECK(reg.all_of<Name>(childEntity));
    CHECK(std::strcmp(reg.get<Name>(childEntity).name, "Child") == 0);
}

// -----------------------------------------------------------------------
// Unparent command via ReparentCommand(entity, null)
// -----------------------------------------------------------------------

TEST_CASE("Unparent command makes entity a root", "[editor][hierarchy]") {
    World world;
    const auto parent = static_cast<entt::entity>(world.createEntity());
    const auto child  = static_cast<entt::entity>(world.createEntity());

    ffe::scene::setParent(world, child, parent);
    REQUIRE_FALSE(ffe::scene::isRoot(world, child));

    CommandHistory history;
    history.executeCommand(std::make_unique<ReparentCommand>(world, child, entt::null));

    CHECK(ffe::scene::isRoot(world, child));

    // Parent should have no children.
    uint32_t childCount = 0;
    ffe::scene::getChildren(world, parent, childCount);
    CHECK(childCount == 0);

    // Undo restores the parent-child relationship.
    history.undo();
    CHECK(ffe::scene::getParent(world, child) == parent);

    uint32_t restoredCount = 0;
    const entt::entity* restoredChildren = ffe::scene::getChildren(world, parent, restoredCount);
    REQUIRE(restoredCount == 1);
    CHECK(restoredChildren[0] == child);
}

// -----------------------------------------------------------------------
// Multiple reparent undo/redo cycle
// -----------------------------------------------------------------------

TEST_CASE("Multiple reparent commands undo in correct order", "[editor][hierarchy]") {
    World world;
    const auto a = static_cast<entt::entity>(world.createEntity());
    const auto b = static_cast<entt::entity>(world.createEntity());
    const auto c = static_cast<entt::entity>(world.createEntity());

    CommandHistory history;

    // c -> parent a
    history.executeCommand(std::make_unique<ReparentCommand>(world, c, a));
    CHECK(ffe::scene::getParent(world, c) == a);

    // c -> parent b (moves from a to b)
    history.executeCommand(std::make_unique<ReparentCommand>(world, c, b));
    CHECK(ffe::scene::getParent(world, c) == b);

    // Undo: c should be back under a.
    history.undo();
    CHECK(ffe::scene::getParent(world, c) == a);

    // Undo: c should be root again.
    history.undo();
    CHECK(ffe::scene::isRoot(world, c));
}
