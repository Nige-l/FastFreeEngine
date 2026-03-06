#ifdef FFE_EDITOR

#include "editor/editor.h"
#include "core/ecs.h"
#include "core/types.h"
#include "renderer/render_system.h"
#include "audio/audio.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <entt/entt.hpp>

namespace ffe::editor {

void EditorOverlay::init(GLFWwindow* window) {
    if (m_initialised) {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Dark theme
    ImGui::StyleColorsDark();

    // Install GLFW backend with callback chaining (true = install callbacks)
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    m_initialised = true;
}

void EditorOverlay::shutdown() {
    if (!m_initialised) {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_initialised = false;
}

void EditorOverlay::beginFrame() {
    if (!m_initialised) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorOverlay::render(World& world) {
    if (!m_initialised) {
        return;
    }

    if (m_visible) {
        drawPerformancePanel(world);
        drawEntityInspector(world);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EditorOverlay::toggle() {
    m_visible = !m_visible;
}

bool EditorOverlay::isVisible() const {
    return m_visible;
}

bool EditorOverlay::wantsMouse() const {
    if (!m_initialised || !m_visible) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

bool EditorOverlay::wantsKeyboard() const {
    if (!m_initialised || !m_visible) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

void EditorOverlay::drawPerformancePanel(World& world) {
    // Update FPS with 1-second rolling average
    const ImGuiIO& io = ImGui::GetIO();
    m_fpsAccum += io.DeltaTime;
    ++m_fpsFrameCount;

    if (m_fpsAccum >= 1.0f) {
        m_displayFps = static_cast<float>(m_fpsFrameCount) / m_fpsAccum;
        m_displayFrameTime = (m_fpsAccum / static_cast<float>(m_fpsFrameCount)) * 1000.0f;
        m_fpsAccum = 0.0f;
        m_fpsFrameCount = 0;
    }

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("FPS: %.1f", static_cast<double>(m_displayFps));
        ImGui::Text("Frame time: %.2f ms", static_cast<double>(m_displayFrameTime));

        // Entity count: total entities minus free-list entries
        const auto& reg = world.registry();
        const auto* storage = reg.storage<entt::entity>();
        std::size_t entityCount = 0;
        if (storage != nullptr) {
            entityCount = storage->size() - storage->free_list();
        }
        ImGui::Text("Entities: %zu", entityCount);

        // Audio voice count
        const u32 voices = ffe::audio::getActiveVoiceCount();
        ImGui::Text("Audio voices: %u", voices);
    }
    ImGui::End();
}

void EditorOverlay::drawEntityInspector(World& world) {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 180.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 350.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Entity Inspector", nullptr, ImGuiWindowFlags_NoCollapse)) {
        // Collect entity IDs with Transform
        auto transformView = world.view<Transform>();

        // Two-column layout: entity list on left, details on right
        const float listWidth = 120.0f;

        // Left child: scrollable entity list
        ImGui::BeginChild("EntityList", ImVec2(listWidth, 0.0f), ImGuiChildFlags_Borders);
        for (const auto entity : transformView) {
            const auto id = static_cast<u32>(entity);
            char label[32];
            snprintf(label, sizeof(label), "Entity %u", id);

            const bool isSelected = m_hasSelection && (m_selectedEntity == id);
            if (ImGui::Selectable(label, isSelected)) {
                m_selectedEntity = id;
                m_hasSelection = true;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right child: component details for selected entity
        ImGui::BeginChild("EntityDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        if (m_hasSelection) {
            const auto entityId = static_cast<EntityId>(m_selectedEntity);

            if (world.isValid(entityId) && world.hasComponent<Transform>(entityId)) {
                ImGui::Text("Entity %u", m_selectedEntity);
                ImGui::Separator();

                // Editable Transform fields
                auto& transform = world.getComponent<Transform>(entityId);
                ImGui::Text("Transform");

                float pos[3] = {transform.position.x, transform.position.y, transform.position.z};
                if (ImGui::DragFloat3("Position", pos, 0.5f)) {
                    transform.position.x = pos[0];
                    transform.position.y = pos[1];
                    transform.position.z = pos[2];
                }

                float scl[3] = {transform.scale.x, transform.scale.y, transform.scale.z};
                if (ImGui::DragFloat3("Scale", scl, 0.01f)) {
                    transform.scale.x = scl[0];
                    transform.scale.y = scl[1];
                    transform.scale.z = scl[2];
                }

                float rot = transform.rotation;
                if (ImGui::DragFloat("Rotation", &rot, 0.01f)) {
                    transform.rotation = rot;
                }

                // Read-only Sprite info
                if (world.hasComponent<Sprite>(entityId)) {
                    ImGui::Separator();
                    ImGui::Text("Sprite");

                    const auto& sprite = world.getComponent<Sprite>(entityId);
                    ImGui::Text("Texture ID: %u", sprite.texture.id);
                    ImGui::Text("Size: %.1f x %.1f",
                        static_cast<double>(sprite.size.x),
                        static_cast<double>(sprite.size.y));
                    ImGui::Text("Color: (%.2f, %.2f, %.2f, %.2f)",
                        static_cast<double>(sprite.color.r),
                        static_cast<double>(sprite.color.g),
                        static_cast<double>(sprite.color.b),
                        static_cast<double>(sprite.color.a));
                    ImGui::Text("Layer: %d  SortOrder: %d",
                        static_cast<int>(sprite.layer),
                        static_cast<int>(sprite.sortOrder));
                }
            } else {
                m_hasSelection = false;
                ImGui::TextDisabled("Entity no longer valid");
            }
        } else {
            ImGui::TextDisabled("Select an entity");
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace ffe::editor

#endif // FFE_EDITOR
