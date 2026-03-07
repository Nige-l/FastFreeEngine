// viewport_panel.cpp -- Scene viewport rendered via FBO into an ImGui panel.
//
// Renders the engine scene into an off-screen FBO, then draws the resulting
// colour texture inside an ImGui window titled "Scene Viewport".
// After the FBO image, gizmo handles are drawn over the selected entity
// using ImDrawList overlay primitives (no GL calls for gizmos).
// FBO resize is driven by the ImGui panel size each frame.

#include "panels/viewport_panel.h"
#include "core/application.h"
#include "play_mode.h"
#include "commands/component_commands.h"

#include "renderer/camera.h"

#include <glad/glad.h>
#include <imgui.h>

#include <cstdint>  // uintptr_t
#include <memory>

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

ffe::editor::GizmoSystem& ViewportPanel::gizmoSystem() {
    return m_gizmoSystem;
}

void ViewportPanel::render(Application& app, ffe::editor::PlayMode& playMode,
                           const EntityId selectedEntity,
                           ffe::editor::CommandHistory& history) {
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
        ImGui::Text("|");
        ImGui::SameLine();

        // --- Gizmo mode toolbar ---
        const auto gizmoMode = m_gizmoSystem.mode();

        // Translate button [T]
        if (gizmoMode == ffe::editor::GizmoMode::TRANSLATE) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        }
        if (ImGui::Button("[T] Move")) {
            m_gizmoSystem.setMode(ffe::editor::GizmoMode::TRANSLATE);
        }
        if (gizmoMode == ffe::editor::GizmoMode::TRANSLATE) {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();

        // Rotate button [R]
        if (gizmoMode == ffe::editor::GizmoMode::ROTATE) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        }
        if (ImGui::Button("[R] Rotate")) {
            m_gizmoSystem.setMode(ffe::editor::GizmoMode::ROTATE);
        }
        if (gizmoMode == ffe::editor::GizmoMode::ROTATE) {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();

        // Scale button [S]
        if (gizmoMode == ffe::editor::GizmoMode::SCALE) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        }
        if (ImGui::Button("[S] Scale")) {
            m_gizmoSystem.setMode(ffe::editor::GizmoMode::SCALE);
        }
        if (gizmoMode == ffe::editor::GizmoMode::SCALE) {
            ImGui::PopStyleColor();
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

            // Remember the cursor position before drawing the image —
            // this is the top-left of the viewport image in screen space.
            const ImVec2 viewportPos = ImGui::GetCursorScreenPos();
            const ImVec2 viewportSize(static_cast<float>(panelW),
                                      static_cast<float>(panelH));

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

            // --- Gizmo overlay ---
            // Only draw gizmos when editing and an entity is selected.
            World& world = app.world();
            const bool hasSelection = (selectedEntity != NULL_ENTITY &&
                                       world.isValid(selectedEntity));

            if (isEditing && hasSelection) {
                // Determine entity position (3D takes priority over 2D).
                const bool has3D = world.hasComponent<Transform3D>(selectedEntity);
                const bool has2D = world.hasComponent<Transform>(selectedEntity);

                glm::vec3 entityPos{0.0f};
                if (has3D) {
                    entityPos = world.getComponent<Transform3D>(selectedEntity).position;
                } else if (has2D) {
                    const auto& t2d = world.getComponent<Transform>(selectedEntity);
                    entityPos = t2d.position;
                }

                // Get camera matrices for projection.
                const auto& cam = app.camera3d();
                const glm::mat4 viewMat = renderer::computeViewMatrix(cam);
                const glm::mat4 projMat = renderer::computeProjectionMatrix(cam);
                const glm::mat4 viewProj = projMat * viewMat;

                // Mouse state relative to the viewport.
                const ImVec2 mousePos = ImGui::GetMousePos();
                const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
                const bool mouseJustPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

                // Only forward mouse to gizmo if the mouse is over the viewport image.
                const bool mouseInViewport =
                    (mousePos.x >= viewportPos.x &&
                     mousePos.y >= viewportPos.y &&
                     mousePos.x < viewportPos.x + viewportSize.x &&
                     mousePos.y < viewportPos.y + viewportSize.y);

                // If not in viewport and not already dragging, skip gizmo update.
                const bool shouldUpdate = mouseInViewport || m_gizmoSystem.isDragging();

                if (shouldUpdate) {
                    m_gizmoSystem.update(viewMat, projMat, entityPos,
                                         mousePos, viewportPos, viewportSize,
                                         mouseDown, mouseJustPressed);
                }

                // Apply gizmo delta to selected entity's transform while dragging.
                const bool currentlyDragging = m_gizmoSystem.isDragging();

                // Capture pre-drag transform on drag start.
                if (currentlyDragging && !m_wasGizmoDragging) {
                    if (has3D) {
                        m_preDragTransform3D = world.getComponent<Transform3D>(selectedEntity);
                    }
                    if (has2D) {
                        m_preDragTransform2D = world.getComponent<Transform>(selectedEntity);
                    }
                }

                // Apply delta each frame during drag.
                if (currentlyDragging) {
                    const glm::vec3 delta = m_gizmoSystem.delta();

                    if (has3D) {
                        auto& t3d = world.getComponent<Transform3D>(selectedEntity);
                        switch (gizmoMode) {
                            case ffe::editor::GizmoMode::TRANSLATE:
                                t3d.position += delta;
                                break;
                            case ffe::editor::GizmoMode::SCALE:
                                t3d.scale += delta;
                                break;
                            case ffe::editor::GizmoMode::ROTATE:
                                // delta.y contains rotation in radians around Y axis
                                t3d.rotation = glm::angleAxis(delta.y,
                                                              glm::vec3(0.0f, 1.0f, 0.0f))
                                               * t3d.rotation;
                                break;
                        }
                    } else if (has2D) {
                        auto& t2d = world.getComponent<Transform>(selectedEntity);
                        switch (gizmoMode) {
                            case ffe::editor::GizmoMode::TRANSLATE:
                                t2d.position += delta;
                                break;
                            case ffe::editor::GizmoMode::SCALE:
                                t2d.scale += delta;
                                break;
                            case ffe::editor::GizmoMode::ROTATE:
                                t2d.rotation += delta.y;
                                break;
                        }
                    }
                }

                // On drag release, create an undo command with pre/post values.
                if (m_wasGizmoDragging && !currentlyDragging) {
                    if (has3D) {
                        const auto& postDrag = world.getComponent<Transform3D>(selectedEntity);
                        auto cmd = std::make_unique<ffe::editor::ModifyComponentCommand<Transform3D>>(
                            world,
                            static_cast<entt::entity>(selectedEntity),
                            m_preDragTransform3D,
                            postDrag);
                        // Don't re-execute — the transform is already at the post-drag value.
                        history.executeCommand(std::move(cmd));
                    } else if (has2D) {
                        const auto& postDrag = world.getComponent<Transform>(selectedEntity);
                        auto cmd = std::make_unique<ffe::editor::ModifyComponentCommand<Transform>>(
                            world,
                            static_cast<entt::entity>(selectedEntity),
                            m_preDragTransform2D,
                            postDrag);
                        history.executeCommand(std::move(cmd));
                    }
                }

                m_wasGizmoDragging = currentlyDragging;

                // Draw gizmo overlay using ImDrawList.
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const auto activeAxis = currentlyDragging
                    ? m_gizmoSystem.activeAxis()
                    : m_gizmoSystem.hoveredAxis();

                m_gizmoRenderer.render(drawList, viewProj, entityPos,
                                       gizmoMode, activeAxis,
                                       viewportPos, viewportSize);
            } else {
                m_wasGizmoDragging = false;
            }
        } else {
            ImGui::TextDisabled("Viewport FBO unavailable");
        }
    }
    ImGui::End();
}

} // namespace ffe::editor_app
