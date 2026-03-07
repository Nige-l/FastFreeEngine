#pragma once

// viewport_panel.h -- Scene viewport rendered via FBO into an ImGui panel.
//
// The ViewportPanel owns an EditorFramebuffer. Each frame it binds the FBO,
// asks the engine Application to render the scene into it, unbinds, and
// displays the colour attachment as an ImGui::Image() inside the
// "Scene Viewport" window.
//
// Resize is handled automatically: when the ImGui panel dimensions change,
// the FBO is recreated at the new size.

#include "rendering/editor_framebuffer.h"

namespace ffe {
class Application;
}

namespace ffe::editor {
class PlayMode;
}

namespace ffe::editor_app {

class ViewportPanel {
public:
    /// Create the backing FBO at the given initial resolution.
    void init(int width, int height);

    /// Destroy GPU resources.
    void shutdown();

    /// Render the scene into the FBO and display it in an ImGui window.
    /// @param app       The engine Application whose renderOnce() drives the scene.
    /// @param playMode  Play-in-editor state (drives toolbar buttons).
    void render(Application& app, ffe::editor::PlayMode& playMode);

private:
    ffe::editor::EditorFramebuffer m_fbo;
    int m_lastWidth  = 0;
    int m_lastHeight = 0;
};

} // namespace ffe::editor_app
