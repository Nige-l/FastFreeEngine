#pragma once

// viewport_panel.h -- Scene viewport rendered via FBO into an ImGui panel.
//
// The ViewportPanel owns an EditorFramebuffer. Each frame it binds the FBO,
// asks the engine Application to render the scene into it, unbinds, and
// displays the colour attachment as an ImGui::Image() inside the
// "Scene Viewport" window.
//
// Gizmo overlay: after the FBO image is drawn, the panel renders
// translation/rotation/scale gizmo handles over the selected entity using
// GizmoRenderer (ImDrawList-based, no GL calls). Mouse interaction is
// handled by GizmoSystem, which produces world-space deltas applied to
// the selected entity's transform with undo/redo support.
//
// Resize is handled automatically: when the ImGui panel dimensions change,
// the FBO is recreated at the new size.

#include "rendering/editor_framebuffer.h"
#include "gizmos/gizmo_renderer.h"
#include "gizmos/gizmo_system.h"
#include "commands/command_history.h"

#include "core/ecs.h"
#include "core/types.h"
#include "renderer/render_system.h"  // Transform, Transform3D

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
    /// @param app            The engine Application whose renderOnce() drives the scene.
    /// @param playMode       Play-in-editor state (drives toolbar buttons).
    /// @param selectedEntity Currently selected entity (NULL_ENTITY if none).
    /// @param history        Command history for undo/redo of gizmo transforms.
    void render(Application& app, ffe::editor::PlayMode& playMode,
                EntityId selectedEntity, ffe::editor::CommandHistory& history);

    /// Access the gizmo system (so EditorApp can set gizmo mode from shortcuts).
    ffe::editor::GizmoSystem& gizmoSystem();

private:
    ffe::editor::EditorFramebuffer m_fbo;
    int m_lastWidth  = 0;
    int m_lastHeight = 0;

    // Gizmo overlay
    ffe::editor::GizmoRenderer m_gizmoRenderer;
    ffe::editor::GizmoSystem   m_gizmoSystem;

    // Pre-drag transform snapshot for undo/redo
    bool       m_wasGizmoDragging = false;
    Transform  m_preDragTransform2D;
    Transform3D m_preDragTransform3D;
};

} // namespace ffe::editor_app
