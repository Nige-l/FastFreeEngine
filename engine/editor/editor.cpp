#ifdef FFE_EDITOR

#include "editor/editor.h"
#include "core/ecs.h"
#include "core/logging.h"
#include "core/types.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "physics/collider2d.h"
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
        drawConsolePanel();
    }

    // HUD is always drawn (even when inspector panels are hidden).
    // Reads directly from the HudTextBuffer in ECS context.
    if (m_showHud) {
        const auto* hudBuf = world.registry().ctx().find<HudTextBuffer>();
        if (hudBuf != nullptr && hudBuf->text[0] != '\0') {
            ImGui::SetNextWindowPos(
                ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 10.0f),
                ImGuiCond_Always, ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.6f);
            ImGui::Begin("##HUD", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
            ImGui::TextUnformatted(hudBuf->text);
            ImGui::End();
        }
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

void EditorOverlay::setShowHud(const bool show) {
    m_showHud = show;
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

        // VRAM usage
        const u32 vramBytes = ffe::rhi::textureVramUsed();
        if (vramBytes < 1024 * 1024) {
            ImGui::Text("Texture VRAM: %.1f KB", static_cast<double>(vramBytes) / 1024.0);
        } else {
            ImGui::Text("Texture VRAM: %.1f MB", static_cast<double>(vramBytes) / (1024.0 * 1024.0));
        }

        // Audio voice count
        const u32 voices = ffe::audio::getActiveVoiceCount();
        ImGui::Text("Audio voices: %u", voices);

        // Collision events this frame
        const auto* collisionList = world.registry().ctx().find<CollisionEventList>();
        if (collisionList != nullptr) {
            ImGui::Text("Collisions: %u", collisionList->count);
        }
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

                // Editable Sprite
                if (world.hasComponent<Sprite>(entityId)) {
                    ImGui::Separator();
                    ImGui::Text("Sprite");

                    auto& sprite = world.getComponent<Sprite>(entityId);
                    ImGui::Text("Texture ID: %u", sprite.texture.id);

                    float size[2] = {sprite.size.x, sprite.size.y};
                    if (ImGui::DragFloat2("Size", size, 0.5f, 1.0f, 512.0f)) {
                        sprite.size.x = size[0];
                        sprite.size.y = size[1];
                    }

                    float col[4] = {sprite.color.r, sprite.color.g,
                                    sprite.color.b, sprite.color.a};
                    if (ImGui::ColorEdit4("Color", col)) {
                        sprite.color = {col[0], col[1], col[2], col[3]};
                    }

                    ImGui::Text("Layer: %d  SortOrder: %d",
                        static_cast<int>(sprite.layer),
                        static_cast<int>(sprite.sortOrder));
                }

                // SpriteAnimation info
                if (world.hasComponent<SpriteAnimation>(entityId)) {
                    ImGui::Separator();
                    ImGui::Text("SpriteAnimation");

                    const auto& anim = world.getComponent<SpriteAnimation>(entityId);
                    ImGui::Text("Frame: %u / %u", anim.currentFrame, anim.frameCount);
                    ImGui::Text("Frame time: %.3f s", static_cast<double>(anim.frameTime));
                    ImGui::Text("Playing: %s  Loop: %s",
                        anim.playing ? "yes" : "no",
                        anim.looping ? "yes" : "no");
                }

                // Collider2D info
                if (world.hasComponent<Collider2D>(entityId)) {
                    ImGui::Separator();
                    ImGui::Text("Collider2D");

                    const auto& col = world.getComponent<Collider2D>(entityId);
                    const char* shapeStr = (col.shape == ColliderShape::CIRCLE) ? "Circle" : "AABB";
                    ImGui::Text("Shape: %s", shapeStr);
                    if (col.shape == ColliderShape::CIRCLE) {
                        ImGui::Text("Radius: %.1f", static_cast<double>(col.halfWidth));
                    } else {
                        ImGui::Text("Half-extents: %.1f x %.1f",
                            static_cast<double>(col.halfWidth),
                            static_cast<double>(col.halfHeight));
                    }
                    ImGui::Text("Trigger: %s", col.isTrigger ? "yes" : "no");
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

void EditorOverlay::drawConsolePanel() {
    const auto* ring = ffe::getLogRingBuffer();
    if (ring == nullptr) { return; }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(
        ImVec2(10.0f, io.DisplaySize.y - 260.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 20.0f, 250.0f),
                             ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoCollapse)) {
        // Scrollable log area
        ImGui::BeginChild("LogScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);

        const uint32_t count = ring->count;
        const uint32_t start = (count < LOG_RING_CAPACITY)
            ? 0u
            : ring->writeIndex;  // oldest entry when buffer is full

        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t idx = (start + i) % LOG_RING_CAPACITY;
            const auto& entry = ring->entries[idx];

            // Color by level
            ImVec4 color;
            switch (entry.level) {
                case LogLevel::TRACE: color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;
                case LogLevel::DEBUG: color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); break;
                case LogLevel::INFO:  color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); break;
                case LogLevel::WARN:  color = ImVec4(1.0f, 0.9f, 0.4f, 1.0f); break;
                case LogLevel::ERR:   color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break;
                case LogLevel::FATAL: color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); break;
                default:              color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
            }

            static constexpr const char* LEVEL_TAGS[] = {
                "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
            };
            const char* tag = LEVEL_TAGS[static_cast<uint8_t>(entry.level)];

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("[%s] [%s] %s", tag, entry.system, entry.message);
            ImGui::PopStyleColor();
        }

        // Auto-scroll to bottom
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace ffe::editor

#endif // FFE_EDITOR
