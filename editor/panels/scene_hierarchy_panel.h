#pragma once

#include "core/ecs.h"
#include "commands/command_history.h"

namespace ffe::editor {

// Scene hierarchy panel — lists all entities in the world.
// Shows entity name (from Name component) or "Entity #<id>" if unnamed.
// Click to select. Right-click context menu for create/delete.
// Simple flat list for now; hierarchy tree (Parent/Children) comes later.
class SceneHierarchyPanel {
public:
    // Draw the panel. Call once per frame inside an ImGui context.
    // selectedEntity: the currently selected entity (read/write).
    // world: the ECS world to display entities from.
    // history: command history for undo-able create/delete operations.
    void draw(World& world, EntityId& selectedEntity, CommandHistory& history);

private:
    // Right-click context menu state
    bool m_contextMenuOpen = false;
};

} // namespace ffe::editor
