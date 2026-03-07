#pragma once

// gizmo_renderer.h -- Draws translation/rotation/scale gizmo handles over the
// selected entity in the editor viewport using ImGui's ImDrawList API.
//
// No GL calls are made here; all drawing goes through ImDrawList overlay
// primitives (AddLine, AddCircle, AddTriangleFilled, AddRectFilled).

#include <glm/glm.hpp>
#include <imgui.h>

namespace ffe::editor {

enum class GizmoMode { TRANSLATE, ROTATE, SCALE };
enum class GizmoAxis { NONE, X, Y, Z };

class GizmoRenderer {
public:
    /// Draw gizmo handles onto the given ImDrawList.
    /// @param drawList     Foreground draw list for the viewport overlay.
    /// @param viewProj     Combined view * projection matrix.
    /// @param position     World-space position of the selected entity.
    /// @param mode         Which gizmo to draw (translate / rotate / scale).
    /// @param activeAxis   Currently active (hovered or dragged) axis to highlight.
    /// @param viewportPos  Screen-space top-left of the viewport panel.
    /// @param viewportSize Screen-space dimensions of the viewport panel.
    void render(ImDrawList* drawList, const glm::mat4& viewProj,
                const glm::vec3& position, GizmoMode mode,
                GizmoAxis activeAxis, const ImVec2& viewportPos,
                const ImVec2& viewportSize);

private:
    /// Project a 3D world position to 2D screen coordinates.
    /// Returns {-1,-1} when the point is behind the camera.
    ImVec2 worldToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj,
                         const ImVec2& vpPos, const ImVec2& vpSize) const;

    void renderTranslate(ImDrawList* drawList, const glm::mat4& viewProj,
                         const glm::vec3& position, GizmoAxis activeAxis,
                         const ImVec2& vpPos, const ImVec2& vpSize);

    void renderRotate(ImDrawList* drawList, const glm::mat4& viewProj,
                      const glm::vec3& position, GizmoAxis activeAxis,
                      const ImVec2& vpPos, const ImVec2& vpSize);

    void renderScale(ImDrawList* drawList, const glm::mat4& viewProj,
                     const glm::vec3& position, GizmoAxis activeAxis,
                     const ImVec2& vpPos, const ImVec2& vpSize);
};

} // namespace ffe::editor
