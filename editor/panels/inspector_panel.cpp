#include "panels/inspector_panel.h"
#include "renderer/render_system.h"

#include <imgui.h>

#include <cstring>
#include <memory>

namespace ffe::editor {

void InspectorPanel::draw(World& world, const EntityId selectedEntity, CommandHistory& history) {
    ImGui::Begin("Inspector");

    if (selectedEntity == NULL_ENTITY || !world.isValid(selectedEntity)) {
        // Reset edit tracking when selection clears
        m_editingTransform   = false;
        m_editingTransform3D = false;
        m_editingName        = false;
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity #%u", selectedEntity);
    ImGui::Separator();

    // Draw each component section if the entity has it.
    // Each section has a collapsible header with a remove ("X") button.
    // Removal is deferred to after drawing so we don't invalidate mid-frame.
    bool removeNameRequested       = false;
    bool removeTransformRequested  = false;
    bool removeTransform3DRequested = false;
    bool removeSpriteRequested     = false;
    bool removeMaterial3DRequested = false;

    if (world.hasComponent<Name>(selectedEntity)) {
        drawNameComponent(world, selectedEntity, history);
        // Remove button for Name
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20.0f);
        ImGui::PushID("RemoveName");
        if (ImGui::SmallButton("X")) {
            removeNameRequested = true;
        }
        ImGui::PopID();
    }
    if (world.hasComponent<Transform>(selectedEntity)) {
        drawTransformComponent(world, selectedEntity, history);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20.0f);
        ImGui::PushID("RemoveTransform");
        if (ImGui::SmallButton("X")) {
            removeTransformRequested = true;
        }
        ImGui::PopID();
    }
    if (world.hasComponent<Transform3D>(selectedEntity)) {
        drawTransform3DComponent(world, selectedEntity, history);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20.0f);
        ImGui::PushID("RemoveTransform3D");
        if (ImGui::SmallButton("X")) {
            removeTransform3DRequested = true;
        }
        ImGui::PopID();
    }
    if (world.hasComponent<Sprite>(selectedEntity)) {
        drawSpriteComponent(world, selectedEntity);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20.0f);
        ImGui::PushID("RemoveSprite");
        if (ImGui::SmallButton("X")) {
            removeSpriteRequested = true;
        }
        ImGui::PopID();
    }
    if (world.hasComponent<Material3D>(selectedEntity)) {
        drawMaterial3DComponent(world, selectedEntity);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20.0f);
        ImGui::PushID("RemoveMaterial3D");
        if (ImGui::SmallButton("X")) {
            removeMaterial3DRequested = true;
        }
        ImGui::PopID();
    }

    // Process deferred removals through the undo system
    const auto entity = static_cast<entt::entity>(selectedEntity);
    if (removeNameRequested) {
        history.executeCommand(
            std::make_unique<RemoveComponentCommand<Name>>(world, entity));
    }
    if (removeTransformRequested) {
        history.executeCommand(
            std::make_unique<RemoveComponentCommand<Transform>>(world, entity));
    }
    if (removeTransform3DRequested) {
        history.executeCommand(
            std::make_unique<RemoveComponentCommand<Transform3D>>(world, entity));
    }
    if (removeSpriteRequested) {
        history.executeCommand(
            std::make_unique<RemoveComponentCommand<Sprite>>(world, entity));
    }
    if (removeMaterial3DRequested) {
        history.executeCommand(
            std::make_unique<RemoveComponentCommand<Material3D>>(world, entity));
    }

    ImGui::Separator();
    drawAddComponentButton(world, selectedEntity, history);

    ImGui::End();
}

void InspectorPanel::drawNameComponent(World& world, const EntityId entity, CommandHistory& history) {
    if (!ImGui::CollapsingHeader("Name", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto& nameComp = world.getComponent<Name>(entity);

    ImGui::InputText("##Name", nameComp.name, sizeof(nameComp.name));

    // Capture snapshot when the text field becomes active
    if (ImGui::IsItemActivated()) {
        m_nameSnapshot = nameComp;
        m_editingName = true;
    }

    // Commit undo command when editing finishes (focus-loss or Enter)
    if (m_editingName && ImGui::IsItemDeactivatedAfterEdit()) {
        // Only push a command if the value actually changed
        if (std::memcmp(m_nameSnapshot.name, nameComp.name, sizeof(nameComp.name)) != 0) {
            const Name newValue = nameComp;
            auto cmd = std::make_unique<ModifyComponentCommand<Name>>(
                world, static_cast<entt::entity>(entity), m_nameSnapshot, newValue);
            history.executeCommand(std::move(cmd));
        }
        m_editingName = false;
    }
    // Also reset if deactivated without edit (e.g. Escape)
    if (m_editingName && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingName = false;
    }
}

void InspectorPanel::drawTransformComponent(World& world, const EntityId entity, CommandHistory& history) {
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto& t = world.getComponent<Transform>(entity);

    // Helper: for each DragFloat, snapshot on activation, commit on deactivation-after-edit.
    // We snapshot the entire Transform component once on first activation and commit
    // a single undo command when any field finishes editing.

    ImGui::DragFloat("X##Transform", &t.position.x, 0.1f);
    if (ImGui::IsItemActivated()) { m_transformSnapshot = t; m_editingTransform = true; }
    if (m_editingTransform && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(
            world, static_cast<entt::entity>(entity), m_transformSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform = false;
    }
    if (m_editingTransform && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform = false;
    }

    ImGui::DragFloat("Y##Transform", &t.position.y, 0.1f);
    if (ImGui::IsItemActivated()) { m_transformSnapshot = t; m_editingTransform = true; }
    if (m_editingTransform && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(
            world, static_cast<entt::entity>(entity), m_transformSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform = false;
    }
    if (m_editingTransform && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform = false;
    }

    ImGui::DragFloat("Rotation##Transform", &t.rotation, 0.01f);
    if (ImGui::IsItemActivated()) { m_transformSnapshot = t; m_editingTransform = true; }
    if (m_editingTransform && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(
            world, static_cast<entt::entity>(entity), m_transformSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform = false;
    }
    if (m_editingTransform && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform = false;
    }

    ImGui::DragFloat("Scale X##Transform", &t.scale.x, 0.01f);
    if (ImGui::IsItemActivated()) { m_transformSnapshot = t; m_editingTransform = true; }
    if (m_editingTransform && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(
            world, static_cast<entt::entity>(entity), m_transformSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform = false;
    }
    if (m_editingTransform && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform = false;
    }

    ImGui::DragFloat("Scale Y##Transform", &t.scale.y, 0.01f);
    if (ImGui::IsItemActivated()) { m_transformSnapshot = t; m_editingTransform = true; }
    if (m_editingTransform && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform>>(
            world, static_cast<entt::entity>(entity), m_transformSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform = false;
    }
    if (m_editingTransform && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform = false;
    }
}

void InspectorPanel::drawTransform3DComponent(World& world, const EntityId entity, CommandHistory& history) {
    if (!ImGui::CollapsingHeader("Transform3D", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto& t = world.getComponent<Transform3D>(entity);

    ImGui::DragFloat3("Position##T3D", &t.position.x, 0.1f);
    if (ImGui::IsItemActivated()) { m_transform3DSnapshot = t; m_editingTransform3D = true; }
    if (m_editingTransform3D && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform3D>>(
            world, static_cast<entt::entity>(entity), m_transform3DSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform3D = false;
    }
    if (m_editingTransform3D && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform3D = false;
    }

    // Display quaternion as 4 floats (w, x, y, z)
    ImGui::DragFloat("Rot W##T3D", &t.rotation.w, 0.01f);
    if (ImGui::IsItemActivated()) { m_transform3DSnapshot = t; m_editingTransform3D = true; }
    if (m_editingTransform3D && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform3D>>(
            world, static_cast<entt::entity>(entity), m_transform3DSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform3D = false;
    }
    if (m_editingTransform3D && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform3D = false;
    }

    ImGui::DragFloat("Rot X##T3D", &t.rotation.x, 0.01f);
    if (ImGui::IsItemActivated()) { m_transform3DSnapshot = t; m_editingTransform3D = true; }
    if (m_editingTransform3D && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform3D>>(
            world, static_cast<entt::entity>(entity), m_transform3DSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform3D = false;
    }
    if (m_editingTransform3D && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform3D = false;
    }

    ImGui::DragFloat("Rot Y##T3D", &t.rotation.y, 0.01f);
    if (ImGui::IsItemActivated()) { m_transform3DSnapshot = t; m_editingTransform3D = true; }
    if (m_editingTransform3D && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform3D>>(
            world, static_cast<entt::entity>(entity), m_transform3DSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform3D = false;
    }
    if (m_editingTransform3D && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform3D = false;
    }

    ImGui::DragFloat("Rot Z##T3D", &t.rotation.z, 0.01f);
    if (ImGui::IsItemActivated()) { m_transform3DSnapshot = t; m_editingTransform3D = true; }
    if (m_editingTransform3D && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform3D>>(
            world, static_cast<entt::entity>(entity), m_transform3DSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform3D = false;
    }
    if (m_editingTransform3D && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform3D = false;
    }

    ImGui::DragFloat3("Scale##T3D", &t.scale.x, 0.01f);
    if (ImGui::IsItemActivated()) { m_transform3DSnapshot = t; m_editingTransform3D = true; }
    if (m_editingTransform3D && ImGui::IsItemDeactivatedAfterEdit()) {
        auto cmd = std::make_unique<ModifyComponentCommand<Transform3D>>(
            world, static_cast<entt::entity>(entity), m_transform3DSnapshot, t);
        history.executeCommand(std::move(cmd));
        m_editingTransform3D = false;
    }
    if (m_editingTransform3D && ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        m_editingTransform3D = false;
    }
}

void InspectorPanel::drawSpriteComponent(World& world, const EntityId entity) {
    if (!ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen)) return;

    const auto& s = world.getComponent<Sprite>(entity);

    // Display-only for now
    ImGui::Text("Texture ID: %u", s.texture.id);
    ImGui::Text("Size: %.0f x %.0f", s.size.x, s.size.y);
    ImGui::ColorEdit4("Color##Sprite", const_cast<float*>(&s.color.r),
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoPicker);
}

void InspectorPanel::drawMaterial3DComponent(World& world, const EntityId entity) {
    if (!ImGui::CollapsingHeader("Material3D", ImGuiTreeNodeFlags_DefaultOpen)) return;

    const auto& m = world.getComponent<Material3D>(entity);

    // Display-only for now
    ImGui::ColorEdit3("Diffuse##Mat3D", const_cast<float*>(&m.diffuseColor.r),
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoPicker);
    ImGui::Text("Shininess: %.1f", m.shininess);
    ImGui::Text("Specular: (%.2f, %.2f, %.2f)", m.specularColor.r, m.specularColor.g, m.specularColor.b);
}

void InspectorPanel::drawAddComponentButton(World& world, const EntityId entity,
                                              CommandHistory& history) {
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    const auto ent = static_cast<entt::entity>(entity);

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!world.hasComponent<Name>(entity)) {
            if (ImGui::MenuItem("Name")) {
                history.executeCommand(
                    std::make_unique<AddComponentCommand<Name>>(world, ent));
            }
        }
        if (!world.hasComponent<Transform>(entity)) {
            if (ImGui::MenuItem("Transform")) {
                history.executeCommand(
                    std::make_unique<AddComponentCommand<Transform>>(world, ent));
            }
        }
        if (!world.hasComponent<Transform3D>(entity)) {
            if (ImGui::MenuItem("Transform3D")) {
                history.executeCommand(
                    std::make_unique<AddComponentCommand<Transform3D>>(world, ent));
            }
        }
        if (!world.hasComponent<Sprite>(entity)) {
            if (ImGui::MenuItem("Sprite")) {
                history.executeCommand(
                    std::make_unique<AddComponentCommand<Sprite>>(world, ent));
            }
        }
        if (!world.hasComponent<Material3D>(entity)) {
            if (ImGui::MenuItem("Material3D")) {
                history.executeCommand(
                    std::make_unique<AddComponentCommand<Material3D>>(world, ent));
            }
        }
        ImGui::EndPopup();
    }
}

} // namespace ffe::editor
