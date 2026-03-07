#include "panels/scene_hierarchy_panel.h"
#include "commands/entity_commands.h"
#include "renderer/render_system.h"

#include <imgui.h>

#include <cstdio>

namespace ffe::editor {

void SceneHierarchyPanel::draw(World& world, EntityId& selectedEntity, CommandHistory& history) {
    ImGui::Begin("Scene Hierarchy");

    // Right-click on empty space -> context menu
    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu")) {
        if (ImGui::MenuItem("Create Entity")) {
            auto cmd = std::make_unique<CreateEntityCommand>(world, "New Entity");
            history.executeCommand(std::move(cmd));
        }
        ImGui::EndPopup();
    }

    // Iterate all entities with a view over the entire registry.
    // We use the raw registry to enumerate all living entities.
    auto& registry = world.registry();

    registry.each([&](const entt::entity entity) {
        const auto entityId = static_cast<EntityId>(entity);

        // Build display label
        char label[128] = {};
        if (registry.all_of<Name>(entity)) {
            const auto& nameComp = registry.get<Name>(entity);
            std::snprintf(label, sizeof(label), "%s##%u", nameComp.name, entityId);
        } else {
            std::snprintf(label, sizeof(label), "Entity #%u##%u", entityId, entityId);
        }

        // Selectable row
        const bool isSelected = (entityId == selectedEntity);
        if (ImGui::Selectable(label, isSelected)) {
            selectedEntity = entityId;
        }

        // Right-click on entity -> context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Create Entity")) {
                auto cmd = std::make_unique<CreateEntityCommand>(world, "New Entity");
                history.executeCommand(std::move(cmd));
            }
            if (ImGui::MenuItem("Delete Entity")) {
                auto cmd = std::make_unique<DestroyEntityCommand>(world, entityId);
                history.executeCommand(std::move(cmd));
                if (selectedEntity == entityId) {
                    selectedEntity = NULL_ENTITY;
                }
            }
            ImGui::EndPopup();
        }
    });

    ImGui::End();
}

} // namespace ffe::editor
