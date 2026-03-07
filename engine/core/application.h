#pragma once

#include "core/types.h"
#include "core/arena_allocator.h"
#include "core/logging.h"
#include "core/ecs.h"
#include "renderer/camera.h"
#include "renderer/mesh_renderer.h"
#include "renderer/render_queue.h"
#include "renderer/shader_library.h"
#include "renderer/shadow_map.h"
#include "renderer/sprite_batch.h"
#include "renderer/text_renderer.h"

#include <tracy/Tracy.hpp>

#include <atomic>

#ifdef FFE_EDITOR
#include "editor/editor.h"
#endif

struct GLFWwindow;

namespace ffe {

struct ApplicationConfig {
    const char* windowTitle = "FastFreeEngine";
    int32_t windowWidth     = 1280;
    int32_t windowHeight    = 720;
    float tickRate          = 60.0f;     // Fixed update Hz
    HardwareTier tier       = HardwareTier::LEGACY;
    bool headless           = false;     // true for tests / CI
};

class Application {
public:
    explicit Application(const ApplicationConfig& config);
    ~Application();

    // Non-copyable, non-movable — there is exactly one
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Run the main loop. Returns exit code.
    int32_t run();

    // Request shutdown at end of current frame
    void requestShutdown();

    // Access subsystems
    World& world();
    ArenaAllocator& frameAllocator();
    const ApplicationConfig& config() const;

private:
    Result startup();
    void shutdown();
    void tick(float dt);      // Fixed-rate update
    void render(float alpha); // Variable-rate render with interpolation factor

    ApplicationConfig m_config;
    World m_world;
    ArenaAllocator m_frameAllocator;
    std::atomic<bool> m_running = false;

    // Renderer state
    GLFWwindow* m_window = nullptr;
    renderer::Camera m_camera;         // 2D orthographic camera (existing)
    renderer::Camera m_camera3d;       // 3D perspective camera (set via ffe.set3DCamera)
    renderer::SceneLighting3D m_sceneLighting;  // 3D scene lighting (set via Lua)
    renderer::RenderQueue m_renderQueue;
    renderer::ShaderLibrary m_shaderLibrary;
    renderer::SpriteBatch m_spriteBatch;
    rhi::TextureHandle m_defaultWhiteTexture;
    renderer::TextRenderer m_textRenderer;
    ShadowConfig m_shadowConfig;
    ShadowMap m_shadowMap;
    glm::vec4 m_clearColor = {0.1f, 0.1f, 0.12f, 1.0f};

#ifdef FFE_EDITOR
    editor::EditorOverlay m_editorOverlay;
#endif
};

} // namespace ffe
