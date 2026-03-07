// viewport_panel.cpp -- Scene viewport rendered via FBO into an ImGui panel.
//
// Renders the engine scene into an off-screen FBO, then draws the resulting
// colour texture inside an ImGui window titled "Scene Viewport".
// FBO resize is driven by the ImGui panel size each frame.

#include "panels/viewport_panel.h"
#include "core/application.h"
#include "play_mode.h"

#include <glad/glad.h>
#include <imgui.h>

#include <cstdint>  // uintptr_t

namespace ffe::editor_app {

void ViewportPanel::init(const int width, const int height) {
    m_fbo.init(width, height);
    m_lastWidth  = width;
    m_lastHeight = height;
}

void ViewportPanel::shutdown() {
    m_fbo.shutdown();
    m_lastWidth  = 0;
    m_lastHeight = 0;
}

void ViewportPanel::render(Application& app, ffe::editor::PlayMode& playMode) {
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene Viewport")) {
        // --- Play-in-editor toolbar ---
        const auto currentState = playMode.state();
        const bool isEditing = (currentState == ffe::editor::PlayState::EDITING);
        const bool isPlaying = (currentState == ffe::editor::PlayState::PLAYING);
        const bool isPaused  = (currentState == ffe::editor::PlayState::PAUSED);

        // Play button — disabled when already playing or paused
        if (isEditing) {
            if (ImGui::Button("Play")) {
                playMode.play(app.world());
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("Play");
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        // Pause / Resume button — toggles depending on state
        if (isPlaying) {
            if (ImGui::Button("Pause")) {
                playMode.pause();
            }
        } else if (isPaused) {
            if (ImGui::Button("Resume")) {
                playMode.resume();
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("Pause");
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        // Stop button — enabled when playing or paused
        if (isPlaying || isPaused) {
            if (ImGui::Button("Stop")) {
                playMode.stop(app.world());
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("Stop");
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        // State indicator
        if (isEditing) {
            ImGui::Text("EDITING");
        } else if (isPlaying) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "PLAYING");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "PAUSED");
        }

        ImGui::Separator();

        // --- Viewport image ---
        const ImVec2 avail = ImGui::GetContentRegionAvail();

        // Clamp to at least 1x1 to avoid creating a zero-size FBO.
        const int panelW = (avail.x > 1.0f) ? static_cast<int>(avail.x) : 1;
        const int panelH = (avail.y > 1.0f) ? static_cast<int>(avail.y) : 1;

        // Resize FBO if the panel dimensions changed.
        if (panelW != m_lastWidth || panelH != m_lastHeight) {
            m_fbo.resize(panelW, panelH);
            m_lastWidth  = panelW;
            m_lastHeight = panelH;
        }

        // Render scene into FBO.
        if (m_fbo.isValid()) {
            m_fbo.bind();

            glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            app.renderOnce(1.0f);

            m_fbo.unbind();

            // Display the FBO colour texture, filling the available panel area.
            // UV is flipped vertically (uv0 = top-left of texture = 0,1;
            // uv1 = bottom-right = 1,0) because OpenGL textures have origin
            // at the bottom-left while ImGui expects top-left.
            const auto texId = static_cast<ImTextureID>(
                static_cast<uintptr_t>(m_fbo.textureId()));
            ImGui::Image(texId,
                         ImVec2(static_cast<float>(panelW),
                                static_cast<float>(panelH)),
                         ImVec2(0.0f, 1.0f),   // uv0 (top-left)
                         ImVec2(1.0f, 0.0f));   // uv1 (bottom-right)
        } else {
            ImGui::TextDisabled("Viewport FBO unavailable");
        }
    }
    ImGui::End();
}

} // namespace ffe::editor_app
