#include "core/application.h"
#include "renderer/rhi.h"
#include "renderer/render_system.h"

#include <chrono>

// GLFW/glad — must define GLFW_INCLUDE_NONE to prevent GLFW from pulling in system GL headers
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace ffe {

// GLFW window close callback
static void glfwWindowCloseCallback(GLFWwindow* window) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app != nullptr) {
        app->requestShutdown();
    }
}

Application::Application(const ApplicationConfig& config)
    : m_config(config)
    , m_frameAllocator(arenaDefaultSize())
{
}

Application::~Application() {
    if (m_running.load(std::memory_order_relaxed)) {
        shutdown();
    }
}

int32_t Application::run() {
    const Result startResult = startup();
    if (!startResult) {
        FFE_LOG_FATAL("Core", "Startup failed: %s", startResult.message());
        return 1;
    }

    m_running.store(true, std::memory_order_relaxed);

    const float fixedDt = 1.0f / m_config.tickRate;   // e.g., 1/60 = 0.01667s
    const float maxFrameTime = 0.25f;                  // Spiral-of-death clamp

    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::duration<float>;

    auto previousTime = Clock::now();
    float accumulator = 0.0f;

    // In headless mode, run a limited number of frames then auto-shutdown
    int32_t frameCount = 0;
    static constexpr int32_t HEADLESS_MAX_FRAMES = 10;

    while (m_running.load(std::memory_order_relaxed)) {
        ZoneScoped; // Tracy: marks entire frame

        // Poll GLFW events (window close, input, etc.)
        if (!m_config.headless) {
            glfwPollEvents();
        }

        const auto currentTime = Clock::now();
        float frameTime = Duration(currentTime - previousTime).count();
        previousTime = currentTime;

        // Clamp to prevent spiral of death (e.g., debugger breakpoint)
        if (frameTime > maxFrameTime) {
            frameTime = maxFrameTime;
        }

        accumulator += frameTime;

        // Reset render queue before tick — renderPrepareSystem populates it during tick
        m_renderQueue.clear();

        // --- Fixed-rate update ---
        while (accumulator >= fixedDt) {
            ZoneScopedN("FixedTick");
            tick(fixedDt);
            accumulator -= fixedDt;
        }

        // --- Variable-rate render ---
        {
            const float alpha = accumulator / fixedDt; // Interpolation factor [0, 1)
            ZoneScopedN("Render");
            render(alpha);
        }

        // --- Per-frame cleanup ---
        m_frameAllocator.reset();

        FrameMark; // Tracy: end of frame

        // Headless mode: auto-shutdown after limited frames
        if (m_config.headless) {
            ++frameCount;
            if (frameCount >= HEADLESS_MAX_FRAMES) {
                m_running.store(false, std::memory_order_relaxed);
            }
        }
    }

    shutdown();
    return 0;
}

void Application::requestShutdown() {
    m_running.store(false, std::memory_order_relaxed);
}

World& Application::world() {
    return m_world;
}

ArenaAllocator& Application::frameAllocator() {
    return m_frameAllocator;
}

const ApplicationConfig& Application::config() const {
    return m_config;
}

Result Application::startup() {
    // 1. Initialize logging (must be first — everything else logs)
    initLogging();

    // 2. Log the hardware tier and build config
    static constexpr const char* TIER_NAMES[] = {
        "RETRO", "LEGACY", "STANDARD", "MODERN"
    };
    FFE_LOG_INFO("Core", "FastFreeEngine v%d.%d.%d starting", 0, 1, 0);
    FFE_LOG_INFO("Core", "Hardware tier: %s", TIER_NAMES[static_cast<u8>(m_config.tier)]);
    FFE_LOG_INFO("Core", "Headless: %s", m_config.headless ? "yes" : "no");

    // 3. Initialize the frame allocator (already constructed, just log)
    FFE_LOG_INFO("Core", "Frame arena: %zu bytes", m_frameAllocator.capacity());

    // 4. Create the window (unless headless)
    if (!m_config.headless) {
        if (glfwInit() == GLFW_FALSE) {
            return Result::fail("GLFW initialization failed");
        }

        // OpenGL 3.3 core profile for LEGACY tier
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES, 0);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_window = glfwCreateWindow(
            m_config.windowWidth,
            m_config.windowHeight,
            m_config.windowTitle,
            nullptr,
            nullptr
        );

        if (m_window == nullptr) {
            glfwTerminate();
            return Result::fail("Failed to create GLFW window");
        }

        glfwMakeContextCurrent(m_window);

        // VSync ON by default
        glfwSwapInterval(1);

        // Set window close callback
        glfwSetWindowUserPointer(m_window, this);
        glfwSetWindowCloseCallback(m_window, glfwWindowCloseCallback);

        // Load OpenGL function pointers via glad
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
            glfwTerminate();
            return Result::fail("Failed to load OpenGL function pointers via glad");
        }

        if (GLVersion_major < 3 || (GLVersion_major == 3 && GLVersion_minor < 3)) {
            FFE_LOG_FATAL("Renderer", "OpenGL 3.3 required, got %d.%d", GLVersion_major, GLVersion_minor);
            glfwDestroyWindow(m_window);
            m_window = nullptr;
            glfwTerminate();
            return Result::fail("OpenGL version too low");
        }
    }

    // 5. Initialize the renderer
    {
        rhi::RhiConfig rhiConfig;
        rhiConfig.viewportWidth  = m_config.windowWidth;
        rhiConfig.viewportHeight = m_config.windowHeight;
        rhiConfig.headless       = m_config.headless;
        rhiConfig.vsync          = true;
#if defined(FFE_DEBUG)
        rhiConfig.debugGL        = true;
#else
        rhiConfig.debugGL        = false;
#endif

        const rhi::RhiResult rhiResult = rhi::init(rhiConfig);
        if (rhiResult != rhi::RhiResult::OK) {
            return Result::fail("RHI initialization failed");
        }
    }

    // 5b. Initialize shader library
    if (!renderer::initShaderLibrary(m_shaderLibrary)) {
        return Result::fail("Shader library initialization failed");
    }

    // 5c. Initialize sprite batch
    renderer::initSpriteBatch(m_spriteBatch,
        renderer::getShader(m_shaderLibrary, renderer::BuiltinShader::SPRITE));

    // 5d. Setup camera for 2D (orthographic, pixel-space)
    m_camera.projType = renderer::ProjectionType::ORTHOGRAPHIC;
    m_camera.orthoLeft   = static_cast<f32>(-m_config.windowWidth)  / 2.0f;
    m_camera.orthoRight  = static_cast<f32>( m_config.windowWidth)  / 2.0f;
    m_camera.orthoBottom = static_cast<f32>(-m_config.windowHeight) / 2.0f;
    m_camera.orthoTop    = static_cast<f32>( m_config.windowHeight) / 2.0f;
    m_camera.viewportWidth  = static_cast<f32>(m_config.windowWidth);
    m_camera.viewportHeight = static_cast<f32>(m_config.windowHeight);

    // 5e. Initialize render queue (pre-allocated, persistent — not from the frame arena)
    renderer::initRenderQueue(m_renderQueue, renderer::MAX_DRAW_COMMANDS_LEGACY);

    // 5f. Register shader library and render queue pointer in ECS context
    m_world.registry().ctx().emplace<renderer::ShaderLibrary>(m_shaderLibrary);
    m_world.registry().ctx().emplace<renderer::RenderQueue*>(&m_renderQueue);

    // 6. Initialize the scripting engine — not yet implemented

    // 7. Register built-in systems
    const SystemDescriptor renderPrepDesc = {
        "RenderPrepare",
        13, // strlen("RenderPrepare")
        renderer::renderPrepareSystem,
        renderer::RENDER_PREPARE_PRIORITY
    };
    m_world.registerSystem(renderPrepDesc);

    // 8. Sort system list by priority
    m_world.sortSystems();

    // 9. Call user init callback (if any) — not yet implemented

    return Result::ok();
}

void Application::shutdown() {
    m_running.store(false, std::memory_order_relaxed);

    // Reverse order of startup
    // 9. Call user shutdown callback — not yet implemented
    // 8. (nothing)
    // 7. Unregister systems (clear handled by World destructor)
    // 6. Shutdown scripting — not yet implemented

    // 5e. Destroy render queue
    renderer::destroyRenderQueue(m_renderQueue);

    // 5c. Shutdown sprite batch
    renderer::shutdownSpriteBatch(m_spriteBatch);

    // 5b. Shutdown shader library
    renderer::shutdownShaderLibrary(m_shaderLibrary);

    // 5. Shutdown renderer
    rhi::shutdown();

    // 4. Destroy window
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
    }

    // 3. (arena allocator destructor handles it)
    // 2. (nothing)

    // 1. Flush and close log file
    shutdownLogging();
}

void Application::tick(const float dt) {
    for (const auto& system : m_world.systems()) {
        ZoneScoped;
        ZoneName(system.name, system.nameLength);
        system.updateFn(m_world, dt);
    }
}

void Application::render(const float alpha) {
    (void)alpha; // Interpolation not yet implemented

    if (m_config.headless) return;

    // Sort the render queue (renderPrepareSystem already populated it during tick)
    renderer::sortRenderQueue(m_renderQueue);

    // Begin frame
    rhi::beginFrame(m_clearColor);

    // Set camera matrices
    const glm::mat4 vp = renderer::computeViewProjectionMatrix(m_camera);
    rhi::setViewProjection(vp);

    // Use the sprite batch to submit 2D geometry from the render queue.
    // Allocate vertex staging from the frame arena (valid for the duration of render).
    auto* staging = static_cast<rhi::SpriteVertex*>(
        m_frameAllocator.allocate(
            renderer::MAX_BATCH_VERTICES * sizeof(rhi::SpriteVertex),
            alignof(rhi::SpriteVertex)));

    if (staging != nullptr) {
        renderer::beginSpriteBatch(m_spriteBatch, staging);

        for (u32 i = 0; i < m_renderQueue.count; ++i) {
            const renderer::DrawCommand& cmd = m_renderQueue.commands[i];

            renderer::SpriteInstance sprite;
            sprite.position = {cmd.posX, cmd.posY};
            sprite.size     = {cmd.scaleX, cmd.scaleY};
            sprite.uvMin    = {0.0f, 0.0f};
            sprite.uvMax    = {1.0f, 1.0f};
            sprite.color    = {1.0f, 1.0f, 1.0f, 1.0f};
            sprite.rotation = 0.0f;
            sprite.depth    = 0.0f;

            renderer::addSprite(m_spriteBatch, cmd.texture, sprite);
        }

        renderer::endSpriteBatch(m_spriteBatch);
    }

    // End frame — swap buffers
    rhi::endFrame(m_window);
}

} // namespace ffe
