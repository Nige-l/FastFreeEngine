#include "panels/inspector_panel.h"
#include "renderer/render_system.h"

#include <imgui.h>

namespace ffe::editor {

void InspectorPanel::draw(World& world, const EntityId selectedEntity, CommandHistory& /*history*/) {
    ImGui::Begin("Inspector");

    if (selectedEntity == NULL_ENTITY || !world.isValid(selectedEntity)) {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity #%u", selectedEntity);
    ImGui::Separator();

    // Draw each component section if the entity has it
    if (world.hasComponent<Name>(selectedEntity)) {
        drawNameComponent(world, selectedEntity);
    }
    if (world.hasComponent<Transform>(selectedEntity)) {
        drawTransformComponent(world, selectedEntity);
    }
    if (world.hasComponent<Transform3D>(selectedEntity)) {
        drawTransform3DComponent(world, selectedEntity);
    }
    if (world.hasComponent<Sprite>(selectedEntity)) {
        drawSpriteComponent(world, selectedEntity);
    }
    if (world.hasComponent<Material3D>(selectedEntity)) {
        drawMaterial3DComponent(world, selectedEntity);
    }

    ImGui::Separator();
    drawAddComponentButton(world, selectedEntity);

    ImGui::End();
}

void InspectorPanel::drawNameComponent(World& world, const EntityId entity) {
    if (!ImGui::CollapsingHeader("Name", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto& nameComp = world.getComponent<Name>(entity);
    ImGui::InputText("##Name", nameComp.name, sizeof(nameComp.name));
}

void InspectorPanel::drawTransformComponent(World& world, const EntityId entity) {
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto& t = world.getComponent<Transform>(entity);
    ImGui::DragFloat("X##Transform", &t.position.x, 0.1f);
    ImGui::DragFloat("Y##Transform", &t.position.y, 0.1f);
    ImGui::DragFloat("Rotation##Transform", &t.rotation, 0.01f);
    ImGui::DragFloat("Scale X##Transform", &t.scale.x, 0.01f);
    ImGui::DragFloat("Scale Y##Transform", &t.scale.y, 0.01f);
}

void InspectorPanel::drawTransform3DComponent(World& world, const EntityId entity) {
    if (!ImGui::CollapsingHeader("Transform3D", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto& t = world.getComponent<Transform3D>(entity);
    ImGui::DragFloat3("Position##T3D", &t.position.x, 0.1f);

    // Display quaternion as 4 floats (w, x, y, z)
    ImGui::DragFloat("Rot W##T3D", &t.rotation.w, 0.01f);
    ImGui::DragFloat("Rot X##T3D", &t.rotation.x, 0.01f);
    ImGui::DragFloat("Rot Y##T3D", &t.rotation.y, 0.01f);
    ImGui::DragFloat("Rot Z##T3D", &t.rotation.z, 0.01f);

    ImGui::DragFloat3("Scale##T3D", &t.scale.x, 0.01f);
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

void InspectorPanel::drawAddComponentButton(World& world, const EntityId entity) {
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!world.hasComponent<Name>(entity)) {
            if (ImGui::MenuItem("Name")) {
                world.addComponent<Name>(entity);
            }
        }
        if (!world.hasComponent<Transform>(entity)) {
            if (ImGui::MenuItem("Transform")) {
                world.addComponent<Transform>(entity);
            }
        }
        if (!world.hasComponent<Transform3D>(entity)) {
            if (ImGui::MenuItem("Transform3D")) {
                world.addComponent<Transform3D>(entity);
            }
        }
        ImGui::EndPopup();
    }
}

} // namespace ffe::editor
