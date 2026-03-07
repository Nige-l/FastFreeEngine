#pragma once

#include "core/ecs.h"
#include "commands/command_history.h"

namespace ffe::editor {

// Inspector panel — shows editable fields for the selected entity's components.
// Displays collapsible headers for each component type.
// Transform and Transform3D fields are editable (DragFloat).
// Sprite and Material3D fields are display-only for now.
// "Add Component" button provides a dropdown to add Transform, Transform3D, or Name.
class InspectorPanel {
public:
    // Draw the panel. Call once per frame inside an ImGui context.
    // selectedEntity: the entity to inspect (NULL_ENTITY = nothing selected).
    // world: the ECS world.
    // history: command history (future: used for component edit commands).
    void draw(World& world, EntityId selectedEntity, CommandHistory& history);

private:
    void drawNameComponent(World& world, EntityId entity);
    void drawTransformComponent(World& world, EntityId entity);
    void drawTransform3DComponent(World& world, EntityId entity);
    void drawSpriteComponent(World& world, EntityId entity);
    void drawMaterial3DComponent(World& world, EntityId entity);
    void drawAddComponentButton(World& world, EntityId entity);
};

} // namespace ffe::editor
