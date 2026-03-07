#pragma once

#include "core/ecs.h"
#include "commands/command_history.h"
#include "commands/component_commands.h"

namespace ffe::editor {

// Inspector panel — shows editable fields for the selected entity's components.
// Displays collapsible headers for each component type.
// Transform and Transform3D fields are editable (DragFloat) with full undo support.
// Sprite and Material3D fields are display-only for now.
// "Add Component" button provides a dropdown to add Transform, Transform3D, or Name.
//
// Undo integration: each field edit captures old/new component values on
// focus-loss / enter (IsItemDeactivatedAfterEdit) and pushes a
// ModifyComponentCommand through CommandHistory.
class InspectorPanel {
public:
    // Draw the panel. Call once per frame inside an ImGui context.
    // selectedEntity: the entity to inspect (NULL_ENTITY = nothing selected).
    // world: the ECS world.
    // history: command history used for undoable component edits.
    void draw(World& world, EntityId selectedEntity, CommandHistory& history);

private:
    void drawNameComponent(World& world, EntityId entity, CommandHistory& history);
    void drawTransformComponent(World& world, EntityId entity, CommandHistory& history);
    void drawTransform3DComponent(World& world, EntityId entity, CommandHistory& history);
    void drawSpriteComponent(World& world, EntityId entity);
    void drawMaterial3DComponent(World& world, EntityId entity);
    void drawAddComponentButton(World& world, EntityId entity);

    // Snapshot of the component being edited, captured when a widget gains focus.
    // Used to create undo commands with the pre-edit value.
    Transform   m_transformSnapshot{};
    Transform3D m_transform3DSnapshot{};
    Name        m_nameSnapshot{};

    // Track which component is actively being edited so we snapshot once on
    // first activation, not every frame.
    bool m_editingTransform   = false;
    bool m_editingTransform3D = false;
    bool m_editingName        = false;
};

} // namespace ffe::editor
