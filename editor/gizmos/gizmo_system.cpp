#include "gizmos/gizmo_system.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace ffe::editor {

// ── Configuration ───────────────────────────────────────────────────────────

static constexpr float HIT_THRESHOLD_PX  = 10.0f;  // max pixel distance for axis pick
static constexpr float AXIS_LENGTH       = 1.2f;    // must match gizmo_renderer.cpp
static constexpr float TRANSLATE_SPEED   = 0.01f;   // world units per pixel of drag
static constexpr float SCALE_SPEED       = 0.005f;  // scale factor per pixel of drag
static constexpr float ROTATE_SPEED      = 0.01f;   // radians per pixel of drag

// ── Setters / getters ───────────────────────────────────────────────────────

void GizmoSystem::setMode(GizmoMode mode) { m_mode = mode; }
GizmoMode GizmoSystem::mode() const        { return m_mode; }
glm::vec3 GizmoSystem::delta() const       { return m_delta; }
GizmoAxis GizmoSystem::activeAxis() const  { return m_activeAxis; }
GizmoAxis GizmoSystem::hoveredAxis() const { return m_hoveredAxis; }
bool GizmoSystem::isDragging() const       { return m_dragging; }

// ── Screen projection (duplicated from GizmoRenderer — keep in sync) ────────

ImVec2 GizmoSystem::worldToScreen(const glm::vec3& worldPos,
                                   const glm::mat4& viewProj,
                                   const ImVec2& vpPos,
                                   const ImVec2& vpSize) const {
    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0001f) {
        return ImVec2(-1.0f, -1.0f);
    }
    const float invW = 1.0f / clip.w;
    const float ndcX = clip.x * invW * 0.5f + 0.5f;
    const float ndcY = 1.0f - (clip.y * invW * 0.5f + 0.5f);
    return ImVec2(vpPos.x + ndcX * vpSize.x,
                  vpPos.y + ndcY * vpSize.y);
}

// ── Geometry helper ─────────────────────────────────────────────────────────

float GizmoSystem::pointToSegmentDist(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float apx = p.x - a.x;
    const float apy = p.y - a.y;
    const float ab2 = abx * abx + aby * aby;
    if (ab2 < 0.0001f) {
        return std::sqrt(apx * apx + apy * apy);
    }
    float t = (apx * abx + apy * aby) / ab2;
    t = std::max(0.0f, std::min(1.0f, t));
    const float cx = a.x + t * abx - p.x;
    const float cy = a.y + t * aby - p.y;
    return std::sqrt(cx * cx + cy * cy);
}

// ── Hit testing ─────────────────────────────────────────────────────────────

GizmoAxis GizmoSystem::hitTest(const glm::mat4& viewProj,
                                const glm::vec3& entityPos,
                                const ImVec2& mousePos,
                                const ImVec2& vpPos,
                                const ImVec2& vpSize) const {
    const ImVec2 origin = worldToScreen(entityPos, viewProj, vpPos, vpSize);
    if (origin.x < 0.0f) return GizmoAxis::NONE;

    struct AxisDef {
        GizmoAxis axis;
        glm::vec3 dir;
    };
    const AxisDef axes[] = {
        { GizmoAxis::X, glm::vec3(AXIS_LENGTH, 0.0f, 0.0f) },
        { GizmoAxis::Y, glm::vec3(0.0f, AXIS_LENGTH, 0.0f) },
        { GizmoAxis::Z, glm::vec3(0.0f, 0.0f, AXIS_LENGTH) },
    };

    GizmoAxis closest = GizmoAxis::NONE;
    float bestDist = HIT_THRESHOLD_PX;

    for (const auto& a : axes) {
        const ImVec2 tip = worldToScreen(entityPos + a.dir, viewProj, vpPos, vpSize);
        if (tip.x < 0.0f) continue;

        const float dist = pointToSegmentDist(mousePos, origin, tip);
        if (dist < bestDist) {
            bestDist = dist;
            closest = a.axis;
        }
    }
    return closest;
}

// ── Per-frame update ────────────────────────────────────────────────────────

bool GizmoSystem::update(const glm::mat4& view, const glm::mat4& proj,
                          const glm::vec3& entityPos,
                          const ImVec2& mousePos, const ImVec2& viewportPos,
                          const ImVec2& viewportSize,
                          bool mouseDown, bool mouseJustPressed) {
    m_delta = glm::vec3(0.0f);
    const glm::mat4 viewProj = proj * view;

    // Always update hover so the renderer can highlight
    m_hoveredAxis = hitTest(viewProj, entityPos, mousePos, viewportPos, viewportSize);

    // ── Start drag ──────────────────────────────────────────────────────
    if (mouseJustPressed && !m_dragging) {
        const GizmoAxis hit = m_hoveredAxis;
        if (hit != GizmoAxis::NONE) {
            m_dragging      = true;
            m_activeAxis    = hit;
            m_dragStart     = glm::vec2(mousePos.x, mousePos.y);
            m_prevMouse     = m_dragStart;
            m_startPosition = entityPos;
            return true; // consumed
        }
    }

    // ── Continue drag ───────────────────────────────────────────────────
    if (m_dragging && mouseDown) {
        const glm::vec2 currentMouse(mousePos.x, mousePos.y);
        const glm::vec2 mouseDelta = currentMouse - m_prevMouse;

        // Project the world-space axis direction into screen space to determine
        // which screen-space motion corresponds to movement along the axis.
        const ImVec2 origin = worldToScreen(entityPos, viewProj, viewportPos, viewportSize);
        glm::vec3 axisDir(0.0f);
        switch (m_activeAxis) {
            case GizmoAxis::X: axisDir = glm::vec3(1.0f, 0.0f, 0.0f); break;
            case GizmoAxis::Y: axisDir = glm::vec3(0.0f, 1.0f, 0.0f); break;
            case GizmoAxis::Z: axisDir = glm::vec3(0.0f, 0.0f, 1.0f); break;
            default: break;
        }

        const ImVec2 axisEnd = worldToScreen(entityPos + axisDir, viewProj,
                                              viewportPos, viewportSize);
        if (origin.x >= 0.0f && axisEnd.x >= 0.0f) {
            // Screen-space axis direction
            glm::vec2 screenAxis(axisEnd.x - origin.x, axisEnd.y - origin.y);
            const float screenLen = glm::length(screenAxis);
            if (screenLen > 0.001f) {
                screenAxis /= screenLen;
                // Project mouse delta onto the screen-space axis direction
                const float projected = glm::dot(mouseDelta, screenAxis);

                switch (m_mode) {
                    case GizmoMode::TRANSLATE:
                        m_delta = axisDir * projected * TRANSLATE_SPEED;
                        break;
                    case GizmoMode::SCALE:
                        m_delta = axisDir * projected * SCALE_SPEED;
                        break;
                    case GizmoMode::ROTATE:
                        // Store rotation angle in the component matching the axis
                        m_delta = axisDir * projected * ROTATE_SPEED;
                        break;
                }
            }
        }

        m_prevMouse = currentMouse;
        return true; // consumed
    }

    // ── End drag ────────────────────────────────────────────────────────
    if (m_dragging && !mouseDown) {
        m_dragging   = false;
        m_activeAxis = GizmoAxis::NONE;
        return false;
    }

    // Not dragging, not starting — did not consume input
    return (m_hoveredAxis != GizmoAxis::NONE);
}

} // namespace ffe::editor
