#pragma once

#include "core/ecs.h"
#include "commands/command_history.h"
#include "commands/component_commands.h"

#include <string>

namespace ffe::editor {

// Inspector panel — shows editable fields for the selected entity's components.
// Displays collapsible headers for each component type.
// Transform and Transform3D fields are editable (DragFloat) with full undo support.
// Sprite and Material3D fields are display-only for now.
// "Add Component" button provides a dropdown to add missing component types.
// Each component header has a remove button ("X") that pushes a
// RemoveComponentCommand through CommandHistory.
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
    void drawMeshComponent(World& world, EntityId entity, CommandHistory& history);
    void drawMaterial3DComponent(World& world, EntityId entity, CommandHistory& history);
    void drawAddComponentButton(World& world, EntityId entity, CommandHistory& history);

    // Returns true if the user clicked the remove ("X") button on a component
    // header. The caller should push a RemoveComponentCommand if true.
    bool drawComponentHeaderWithRemove(const char* label);

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

    // Asset paths assigned via drag-and-drop (display-only for now — actual
    // asset loading integration will connect these to the resource system later).
    std::string m_meshAssetPath;
    std::string m_diffuseTexturePath;
    std::string m_normalMapPath;
    std::string m_specularMapPath;

    // Returns the lowercase extension of a file path (e.g. ".png"), or empty
    // string if there is no extension.
    static std::string extensionLower(const std::string& path);

    // Returns true if the extension matches one of the accepted image formats.
    static bool isImageFile(const std::string& path);

    // Returns true if the extension matches one of the accepted mesh formats.
    static bool isMeshFile(const std::string& path);

    // Helper: draw a drop-target zone for an asset path field. Returns the
    // dropped path if a valid drop occurred, or empty string otherwise.
    // acceptImage: if true, accepts image files; acceptMesh: if true, accepts mesh files.
    static std::string drawAssetDropTarget(const char* label, const std::string& currentPath,
                                           bool acceptImage, bool acceptMesh);
};

} // namespace ffe::editor
