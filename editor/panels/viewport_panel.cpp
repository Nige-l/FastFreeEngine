// viewport_panel.cpp -- Scene viewport rendered via FBO into an ImGui panel.
//
// Renders the engine scene into an off-screen FBO, then draws the resulting
// colour texture inside an ImGui window titled "Scene Viewport".
// FBO resize is driven by the ImGui panel size each frame.

#include "panels/viewport_panel.h"
#include "core/application.h"

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

void ViewportPanel::render(Application& app) {
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene Viewport")) {
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
