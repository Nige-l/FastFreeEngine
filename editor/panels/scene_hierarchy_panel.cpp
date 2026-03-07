#include "panels/scene_hierarchy_panel.h"
#include "commands/entity_commands.h"
#include "commands/reparent_command.h"
#include "renderer/render_system.h"
#include "scene/scene_graph.h"

#include <imgui.h>

#include <cstdio>

namespace ffe::editor {

// Payload type identifier for drag-drop reparenting.
static constexpr const char* ENTITY_DRAG_TYPE = "FFE_ENTITY";

void SceneHierarchyPanel::draw(World& world, EntityId& selectedEntity, CommandHistory& history) {
    ImGui::Begin("Scene Hierarchy");

    // Collect root entities (those with no Parent component).
    entt::entity roots[MAX_ROOTS] = {};
    const uint32_t rootCount = ffe::scene::getRootEntities(world, roots, MAX_ROOTS);

    // Draw each root as a tree node.
    for (uint32_t i = 0; i < rootCount; ++i) {
        drawEntityNode(world, roots[i], selectedEntity, history);
    }

    // Right-click on empty space -> create root entity.
    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu",
            ImGuiPopupFlags_NoOpenOverItem)) {
        if (ImGui::MenuItem("Create Entity")) {
            auto cmd = std::make_unique<CreateEntityCommand>(world, "New Entity");
            history.executeCommand(std::move(cmd));
        }
        ImGui::EndPopup();
    }

    // Drop on empty space -> unparent (make root).
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ENTITY_DRAG_TYPE)) {
            const auto droppedEntity = *static_cast<const entt::entity*>(payload->Data);
            // Only reparent if entity currently has a parent.
            if (!ffe::scene::isRoot(world, droppedEntity)) {
                auto cmd = std::make_unique<ReparentCommand>(world, droppedEntity, entt::null);
                history.executeCommand(std::move(cmd));
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
}

void SceneHierarchyPanel::drawEntityNode(World& world, entt::entity entity,
                                          EntityId& selectedEntity, CommandHistory& history) {
    const auto entityId = static_cast<EntityId>(entity);
    auto& reg = world.registry();

    // Build display label.
    char label[128] = {};
    if (reg.all_of<Name>(entity)) {
        const auto& nameComp = reg.get<Name>(entity);
        std::snprintf(label, sizeof(label), "%s##%u", nameComp.name, entityId);
    } else {
        std::snprintf(label, sizeof(label), "Entity #%u##%u", entityId, entityId);
    }

    // Determine if this node has children (affects tree node display).
    uint32_t childCount = 0;
    const entt::entity* children = ffe::scene::getChildren(world, entity, childCount);
    const bool hasChildren = (childCount > 0);

    // Tree node flags.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (entityId == selectedEntity) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    const bool nodeOpen = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(entityId)), flags, "%s",
        reg.all_of<Name>(entity) ? reg.get<Name>(entity).name
                                 : label);

    // Click to select.
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selectedEntity = entityId;
    }

    // Drag source — begin dragging this entity.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(ENTITY_DRAG_TYPE, &entity, sizeof(entity));
        // Tooltip while dragging.
        if (reg.all_of<Name>(entity)) {
            ImGui::Text("%s", reg.get<Name>(entity).name);
        } else {
            ImGui::Text("Entity #%u", entityId);
        }
        ImGui::EndDragDropSource();
    }

    // Drop target — reparent dropped entity to this one.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ENTITY_DRAG_TYPE)) {
            const auto droppedEntity = *static_cast<const entt::entity*>(payload->Data);
            // Prevent self-parenting and circular references (setParent checks too).
            if (droppedEntity != entity) {
                auto cmd = std::make_unique<ReparentCommand>(world, droppedEntity, entity);
                history.executeCommand(std::move(cmd));
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click context menu.
    drawEntityContextMenu(world, entity, selectedEntity, history);

    // Recursively draw children if the node is open and has children.
    if (nodeOpen && hasChildren) {
        // Copy children handles to a local buffer before iterating,
        // because reparenting during iteration would invalidate the pointer.
        entt::entity childBuffer[32] = {};
        const uint32_t count = (childCount <= 32) ? childCount : 32;
        for (uint32_t i = 0; i < count; ++i) {
            childBuffer[i] = children[i];
        }
        for (uint32_t i = 0; i < count; ++i) {
            drawEntityNode(world, childBuffer[i], selectedEntity, history);
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::drawEntityContextMenu(World& world, entt::entity entity,
                                                  EntityId& selectedEntity,
                                                  CommandHistory& history) {
    const auto entityId = static_cast<EntityId>(entity);

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Create Entity")) {
            auto cmd = std::make_unique<CreateEntityCommand>(world, "New Entity");
            history.executeCommand(std::move(cmd));
        }

        if (ImGui::MenuItem("Create Child")) {
            // Create a new entity and parent it under the right-clicked entity.
            auto createCmd = std::make_unique<CreateEntityCommand>(world, "New Child");
            history.executeCommand(std::move(createCmd));

            // The newly created entity is the most recent one. Find it by
            // scanning for the entity with name "New Child" that is a root
            // (just created, not yet parented).
            entt::entity newChild = entt::null;
            auto view = world.view<Name>();
            for (auto e : view) {
                if (ffe::scene::isRoot(world, e)) {
                    newChild = e;
                    // Take the last matching root — it is the most recently created.
                }
            }

            if (newChild != entt::null) {
                auto reparentCmd = std::make_unique<ReparentCommand>(world, newChild, entity);
                history.executeCommand(std::move(reparentCmd));
            }
        }

        if (ImGui::MenuItem("Unparent", nullptr, false,
                            !ffe::scene::isRoot(world, entity))) {
            auto cmd = std::make_unique<ReparentCommand>(world, entity, entt::null);
            history.executeCommand(std::move(cmd));
        }

        if (ImGui::MenuItem("Delete")) {
            // Unparent all children first (make them roots), then destroy.
            uint32_t childCount = 0;
            const entt::entity* children = ffe::scene::getChildren(world, entity, childCount);
            if (children != nullptr && childCount > 0) {
                // Copy children to local buffer (removeParent modifies the array).
                entt::entity childBuffer[32] = {};
                const uint32_t count = (childCount <= 32) ? childCount : 32;
                for (uint32_t i = 0; i < count; ++i) {
                    childBuffer[i] = children[i];
                }
                for (uint32_t i = 0; i < count; ++i) {
                    auto reparentCmd = std::make_unique<ReparentCommand>(
                        world, childBuffer[i], entt::null);
                    history.executeCommand(std::move(reparentCmd));
                }
            }

            auto cmd = std::make_unique<DestroyEntityCommand>(world, entityId);
            history.executeCommand(std::move(cmd));
            if (selectedEntity == entityId) {
                selectedEntity = NULL_ENTITY;
            }
        }

        ImGui::EndPopup();
    }
}

} // namespace ffe::editor
