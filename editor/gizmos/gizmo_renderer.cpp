#include "gizmos/gizmo_renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace ffe::editor {

// ── Colour constants ────────────────────────────────────────────────────────
static constexpr ImU32 COLOR_X        = IM_COL32(220, 50, 50, 255);
static constexpr ImU32 COLOR_Y        = IM_COL32(50, 200, 50, 255);
static constexpr ImU32 COLOR_Z        = IM_COL32(50, 100, 220, 255);
static constexpr ImU32 COLOR_X_ACTIVE = IM_COL32(255, 120, 120, 255);
static constexpr ImU32 COLOR_Y_ACTIVE = IM_COL32(120, 255, 120, 255);
static constexpr ImU32 COLOR_Z_ACTIVE = IM_COL32(120, 170, 255, 255);

static constexpr float AXIS_LENGTH   = 1.2f;   // world-space length of each handle
static constexpr float ARROW_SIZE    = 8.0f;    // pixels for arrowhead / box
static constexpr float LINE_THICK    = 2.0f;
static constexpr float LINE_THICK_HI = 3.5f;    // highlighted thickness
static constexpr int   CIRCLE_SEGS   = 48;      // segments for rotation rings
static constexpr float ROTATE_RADIUS = 1.0f;    // world-space circle radius

// ── Helpers ─────────────────────────────────────────────────────────────────

static ImU32 axisColor(GizmoAxis axis, GizmoAxis active) {
    const bool hi = (axis == active);
    switch (axis) {
        case GizmoAxis::X: return hi ? COLOR_X_ACTIVE : COLOR_X;
        case GizmoAxis::Y: return hi ? COLOR_Y_ACTIVE : COLOR_Y;
        case GizmoAxis::Z: return hi ? COLOR_Z_ACTIVE : COLOR_Z;
        default: return IM_COL32(180, 180, 180, 255);
    }
}

static float axisThickness(GizmoAxis axis, GizmoAxis active) {
    return (axis == active) ? LINE_THICK_HI : LINE_THICK;
}

// ── worldToScreen ───────────────────────────────────────────────────────────

ImVec2 GizmoRenderer::worldToScreen(const glm::vec3& worldPos,
                                     const glm::mat4& viewProj,
                                     const ImVec2& vpPos,
                                     const ImVec2& vpSize) const {
    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0001f) {
        return ImVec2(-1.0f, -1.0f); // behind camera
    }
    const float invW = 1.0f / clip.w;
    // NDC -> [0,1]
    const float ndcX = clip.x * invW * 0.5f + 0.5f;
    const float ndcY = 1.0f - (clip.y * invW * 0.5f + 0.5f); // flip Y for screen
    return ImVec2(vpPos.x + ndcX * vpSize.x,
                  vpPos.y + ndcY * vpSize.y);
}

// ── Translate gizmo ─────────────────────────────────────────────────────────

static void drawArrowhead(ImDrawList* dl, const ImVec2& tip, const ImVec2& base, ImU32 col) {
    // Direction from base to tip
    const float dx = tip.x - base.x;
    const float dy = tip.y - base.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    const float nx = dx / len;
    const float ny = dy / len;
    // Perpendicular
    const float px = -ny * ARROW_SIZE * 0.5f;
    const float py =  nx * ARROW_SIZE * 0.5f;
    const ImVec2 backPt(tip.x - nx * ARROW_SIZE, tip.y - ny * ARROW_SIZE);
    dl->AddTriangleFilled(tip,
                          ImVec2(backPt.x + px, backPt.y + py),
                          ImVec2(backPt.x - px, backPt.y - py),
                          col);
}

void GizmoRenderer::renderTranslate(ImDrawList* drawList,
                                     const glm::mat4& viewProj,
                                     const glm::vec3& position,
                                     GizmoAxis activeAxis,
                                     const ImVec2& vpPos,
                                     const ImVec2& vpSize) {
    const ImVec2 origin = worldToScreen(position, viewProj, vpPos, vpSize);
    if (origin.x < 0.0f) return;

    struct AxisDef {
        GizmoAxis axis;
        glm::vec3 dir;
    };
    const AxisDef axes[] = {
        { GizmoAxis::X, glm::vec3(AXIS_LENGTH, 0.0f, 0.0f) },
        { GizmoAxis::Y, glm::vec3(0.0f, AXIS_LENGTH, 0.0f) },
        { GizmoAxis::Z, glm::vec3(0.0f, 0.0f, AXIS_LENGTH) },
    };

    for (const auto& a : axes) {
        const ImVec2 tip = worldToScreen(position + a.dir, viewProj, vpPos, vpSize);
        if (tip.x < 0.0f) continue;

        const ImU32 col   = axisColor(a.axis, activeAxis);
        const float thick = axisThickness(a.axis, activeAxis);

        drawList->AddLine(origin, tip, col, thick);
        drawArrowhead(drawList, tip, origin, col);
    }
}

// ── Rotate gizmo ────────────────────────────────────────────────────────────

void GizmoRenderer::renderRotate(ImDrawList* drawList,
                                  const glm::mat4& viewProj,
                                  const glm::vec3& position,
                                  GizmoAxis activeAxis,
                                  const ImVec2& vpPos,
                                  const ImVec2& vpSize) {
    // Draw a circle of points for each axis plane.
    // X-axis rotation: circle in YZ plane
    // Y-axis rotation: circle in XZ plane
    // Z-axis rotation: circle in XY plane

    struct RingDef {
        GizmoAxis axis;
        glm::vec3 u; // first basis vector of the circle plane
        glm::vec3 v; // second basis vector
    };
    const RingDef rings[] = {
        { GizmoAxis::X, glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) },
        { GizmoAxis::Y, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1) },
        { GizmoAxis::Z, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0) },
    };

    for (const auto& r : rings) {
        const ImU32 col   = axisColor(r.axis, activeAxis);
        const float thick = axisThickness(r.axis, activeAxis);

        ImVec2 prev{-1.0f, -1.0f};
        for (int i = 0; i <= CIRCLE_SEGS; ++i) {
            const float angle = (static_cast<float>(i) / static_cast<float>(CIRCLE_SEGS))
                                * 2.0f * 3.14159265f;
            const glm::vec3 pt = position
                                 + r.u * (std::cos(angle) * ROTATE_RADIUS)
                                 + r.v * (std::sin(angle) * ROTATE_RADIUS);
            const ImVec2 sp = worldToScreen(pt, viewProj, vpPos, vpSize);
            if (sp.x >= 0.0f && prev.x >= 0.0f) {
                drawList->AddLine(prev, sp, col, thick);
            }
            prev = sp;
        }
    }
}

// ── Scale gizmo ─────────────────────────────────────────────────────────────

void GizmoRenderer::renderScale(ImDrawList* drawList,
                                 const glm::mat4& viewProj,
                                 const glm::vec3& position,
                                 GizmoAxis activeAxis,
                                 const ImVec2& vpPos,
                                 const ImVec2& vpSize) {
    const ImVec2 origin = worldToScreen(position, viewProj, vpPos, vpSize);
    if (origin.x < 0.0f) return;

    struct AxisDef {
        GizmoAxis axis;
        glm::vec3 dir;
    };
    const AxisDef axes[] = {
        { GizmoAxis::X, glm::vec3(AXIS_LENGTH, 0.0f, 0.0f) },
        { GizmoAxis::Y, glm::vec3(0.0f, AXIS_LENGTH, 0.0f) },
        { GizmoAxis::Z, glm::vec3(0.0f, 0.0f, AXIS_LENGTH) },
    };

    const float halfBox = ARROW_SIZE * 0.5f;

    for (const auto& a : axes) {
        const ImVec2 tip = worldToScreen(position + a.dir, viewProj, vpPos, vpSize);
        if (tip.x < 0.0f) continue;

        const ImU32 col   = axisColor(a.axis, activeAxis);
        const float thick = axisThickness(a.axis, activeAxis);

        drawList->AddLine(origin, tip, col, thick);
        // Box endpoint
        drawList->AddRectFilled(
            ImVec2(tip.x - halfBox, tip.y - halfBox),
            ImVec2(tip.x + halfBox, tip.y + halfBox),
            col);
    }
}

// ── Public render ───────────────────────────────────────────────────────────

void GizmoRenderer::render(ImDrawList* drawList, const glm::mat4& viewProj,
                            const glm::vec3& position, GizmoMode mode,
                            GizmoAxis activeAxis, const ImVec2& viewportPos,
                            const ImVec2& viewportSize) {
    if (!drawList) return;

    switch (mode) {
        case GizmoMode::TRANSLATE:
            renderTranslate(drawList, viewProj, position, activeAxis, viewportPos, viewportSize);
            break;
        case GizmoMode::ROTATE:
            renderRotate(drawList, viewProj, position, activeAxis, viewportPos, viewportSize);
            break;
        case GizmoMode::SCALE:
            renderScale(drawList, viewProj, position, activeAxis, viewportPos, viewportSize);
            break;
    }
}

} // namespace ffe::editor
