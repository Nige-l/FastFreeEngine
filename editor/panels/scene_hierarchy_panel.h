#pragma once

#include "core/ecs.h"
#include "commands/command_history.h"

#include <entt/entt.hpp>

namespace ffe::editor {

// Scene hierarchy panel — displays entities as a tree using Parent/Children
// components from the scene graph. Supports click-to-select, drag-to-reparent,
// and right-click context menus (create, create child, unparent, delete).
// All mutating operations go through CommandHistory for undo/redo.
class SceneHierarchyPanel {
public:
    // Draw the panel. Call once per frame inside an ImGui context.
    // selectedEntity: the currently selected entity (read/write).
    // world: the ECS world to display entities from.
    // history: command history for undo-able operations.
    void draw(World& world, EntityId& selectedEntity, CommandHistory& history);

private:
    // Recursively draw a single entity node and its children as a tree.
    void drawEntityNode(World& world, entt::entity entity,
                        EntityId& selectedEntity, CommandHistory& history);

    // Draw the right-click context menu for an entity.
    void drawEntityContextMenu(World& world, entt::entity entity,
                               EntityId& selectedEntity, CommandHistory& history);

    // Maximum number of root entities to display.
    static constexpr uint32_t MAX_ROOTS = 1024;
};

} // namespace ffe::editor
