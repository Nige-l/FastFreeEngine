#pragma once

// gizmo_system.h -- Handles mouse interaction with translation/rotation/scale
// gizmos in the editor viewport.
//
// Each frame the viewport calls update() with the current mouse state. The
// system performs hit-testing against projected axis handles and, while
// dragging, computes a world-space delta that the caller applies to the
// selected entity's transform.

#include "gizmos/gizmo_renderer.h"   // GizmoMode, GizmoAxis

#include <glm/glm.hpp>
#include <imgui.h>

namespace ffe::editor {

class GizmoSystem {
public:
    void setMode(GizmoMode mode);
    GizmoMode mode() const;

    /// Process mouse input for gizmo interaction.
    /// @return true if the gizmo consumed the mouse input (caller should not
    ///         forward it to the scene camera, etc.).
    bool update(const glm::mat4& view, const glm::mat4& proj,
                const glm::vec3& entityPos,
                const ImVec2& mousePos, const ImVec2& viewportPos,
                const ImVec2& viewportSize, bool mouseDown, bool mouseJustPressed);

    /// World-space delta produced during the current frame's drag.
    /// For TRANSLATE: translation offset.
    /// For SCALE: per-axis scale delta (only the active axis is non-zero).
    /// For ROTATE: rotation in radians around Y stored in delta().y (placeholder).
    glm::vec3 delta() const;

    GizmoAxis activeAxis() const;
    GizmoAxis hoveredAxis() const;
    bool isDragging() const;

private:
    GizmoMode  m_mode       = GizmoMode::TRANSLATE;
    GizmoAxis  m_activeAxis = GizmoAxis::NONE;
    GizmoAxis  m_hoveredAxis = GizmoAxis::NONE;
    bool       m_dragging   = false;
    glm::vec3  m_delta{0.0f};
    glm::vec2  m_dragStart{0.0f};
    glm::vec2  m_prevMouse{0.0f};
    glm::vec3  m_startPosition{0.0f};

    /// Project each axis endpoint to screen space and return the axis whose
    /// line is closest to the mouse position (within a pixel threshold).
    GizmoAxis hitTest(const glm::mat4& viewProj, const glm::vec3& entityPos,
                      const ImVec2& mousePos, const ImVec2& vpPos,
                      const ImVec2& vpSize) const;

    /// Helper: project a world point to screen space (same logic as GizmoRenderer).
    ImVec2 worldToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj,
                         const ImVec2& vpPos, const ImVec2& vpSize) const;

    /// Compute the signed distance from a point to a line segment (2D, screen space).
    static float pointToSegmentDist(const ImVec2& p, const ImVec2& a, const ImVec2& b);
};

} // namespace ffe::editor
