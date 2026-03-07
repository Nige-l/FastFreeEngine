#include "core/application.h"
#include "core/input.h"
#include "renderer/rhi.h"
#include "renderer/render_system.h"
#include "renderer/animation_system.h"
#include "renderer/mesh_loader.h"
#include "renderer/mesh_renderer.h"
#include "renderer/shadow_map.h"
#include "renderer/text_renderer.h"
#include "renderer/texture_loader.h"
#include "physics/collider2d.h"
#include "physics/collision_system.h"
#include "physics/physics3d.h"
#include "physics/physics3d_system.h"
#include "audio/audio.h"

#include <chrono>
#include <cmath>

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

        // In headless mode, ensure at least one tick per frame so systems always run.
        // Without this, frame time is near-zero and accumulator never reaches fixedDt.
        if (m_config.headless && accumulator < fixedDt) {
            accumulator = fixedDt;
        }

        // Reset render queue for this frame. The render queue is repopulated
        // by renderPrepareSystem during render(), so it must always be cleared.
        m_renderQueue.clear();

        // Only clear the text glyph buffer when at least one tick will run.
        // Text is queued via drawText() during tick(), so if zero ticks execute
        // (frame completed faster than fixedDt), the previous frame's text must
        // persist — otherwise text flickers on/off frame-to-frame.
        if (accumulator >= fixedDt) {
            renderer::beginText(m_textRenderer);
        }

        // --- Fixed-rate update ---
        while (accumulator >= fixedDt) {
            ZoneScopedN("FixedTick");
            tick(fixedDt);
            accumulator -= fixedDt;

#ifdef FFE_EDITOR
            // F1 toggles editor overlay — checked immediately after tick() so
            // updateInput() (priority 0) has processed the key event into
            // current/previous state. Must be inside the tick loop, not after it,
            // because a second tick would copy current→previous and convert
            // "pressed" into "held", making the post-loop check miss the event.
            if (ffe::isKeyPressed(ffe::Key::F1)) {
                m_editorOverlay.toggle();
            }
#endif

            // Check if a system requested shutdown via the ECS context signal.
            // Break out of the tick loop immediately so we don't execute more
            // ticks after a shutdown has been requested.
            if (m_world.registry().ctx().get<ShutdownSignal>().requested) {
                m_running.store(false, std::memory_order_relaxed);
                break;
            }
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

const renderer::Camera& Application::camera3d() const {
    return m_camera3d;
}

const renderer::Camera& Application::camera2d() const {
    return m_camera;
}

GLFWwindow* Application::window() const {
    return m_window;
}

void Application::setWindow(GLFWwindow* win) {
    m_window = win;
}

bool Application::initSubsystems() {
    const Result result = initSubsystemsInternal();
    if (!result) {
        FFE_LOG_FATAL("Core", "initSubsystems failed: %s", result.message());
        return false;
    }
    m_running.store(true, std::memory_order_relaxed);
    return true;
}

void Application::shutdownSubsystems() {
    shutdown();
}

void Application::tickOnce(const float dt) {
    tick(dt);
}

void Application::renderOnce(const float alpha) {
    // Reset render queue for this frame
    m_renderQueue.clear();

    // Clear text glyph buffer for fresh text this frame
    renderer::beginText(m_textRenderer);

    render(alpha);

    // Per-frame cleanup
    m_frameAllocator.reset();
}

// ---------------------------------------------------------------------------
// initSubsystemsInternal — shared implementation for both standalone startup()
// and editor-hosted initSubsystems(). Initialises all engine subsystems
// (renderer, audio, physics, ECS context, built-in systems) assuming that
// a GLFW window (if needed) already exists in m_window.
// ---------------------------------------------------------------------------
Result Application::initSubsystemsInternal() {
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

    // 4b. Initialize the input system (handles nullptr window in headless mode)
    initInput(m_window);

#ifdef FFE_EDITOR
    // 4c. Initialize editor overlay (after window + GL, before renderer objects)
    if (!m_config.headless) {
        m_editorOverlay.init(m_window);
    }
#endif

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

    // 5cc. Create a 1x1 white default texture for untextured sprites.
    // The sprite shader multiplies texel * color, so a white pixel lets color through.
    {
        static constexpr u8 WHITE_PIXEL[] = {255, 255, 255, 255};
        rhi::TextureDesc texDesc;
        texDesc.width     = 1;
        texDesc.height    = 1;
        texDesc.format    = rhi::TextureFormat::RGBA8;
        texDesc.filter    = rhi::TextureFilter::NEAREST;
        texDesc.wrap      = rhi::TextureWrap::CLAMP_TO_EDGE;
        texDesc.pixelData = WHITE_PIXEL;
        m_defaultWhiteTexture = rhi::createTexture(texDesc);
        m_world.registry().ctx().emplace<rhi::TextureHandle>(m_defaultWhiteTexture);
    }

    // 5cc2. Initialize text renderer for screen-space HUD text
    renderer::initTextRenderer(m_textRenderer,
        static_cast<f32>(m_config.windowWidth),
        static_cast<f32>(m_config.windowHeight));
    m_world.registry().ctx().emplace<renderer::TextRenderer*>(&m_textRenderer);

    // 5cd. Emplace HudTextBuffer into ECS context so Lua/systems can set HUD text
    // without a direct pointer to the editor overlay.
    m_world.registry().ctx().emplace<HudTextBuffer>();

    // 5ce. Emplace ShutdownSignal into ECS context so systems can request shutdown
    // without holding an Application pointer.  Application::run() checks this
    // after every tick and exits the loop if requested = true.
    m_world.registry().ctx().emplace<ShutdownSignal>();

    // 5ce2. Emplace CameraShake into ECS context for Lua-driven camera effects.
    m_world.registry().ctx().emplace<CameraShake>();

    // 5ce3. Emplace ClearColor into ECS context for Lua-driven background color.
    m_world.registry().ctx().emplace<ClearColor>();

    // 5cf. Emplace frame arena pointer into ECS context so systems (e.g. collision)
    // can use per-frame arena allocation without holding an Application pointer.
    m_world.registry().ctx().emplace<ArenaAllocator*>(&m_frameAllocator);

    // 5cg. Emplace collision event list and callback ref into ECS context.
    m_world.registry().ctx().emplace<CollisionEventList>();
    m_world.registry().ctx().emplace<CollisionCallbackRef>();

    // 5d. Setup camera for 2D (orthographic, pixel-space)
    m_camera.projType = renderer::ProjectionType::ORTHOGRAPHIC;
    m_camera.orthoLeft   = static_cast<f32>(-m_config.windowWidth)  / 2.0f;
    m_camera.orthoRight  = static_cast<f32>( m_config.windowWidth)  / 2.0f;
    m_camera.orthoBottom = static_cast<f32>(-m_config.windowHeight) / 2.0f;
    m_camera.orthoTop    = static_cast<f32>( m_config.windowHeight) / 2.0f;
    m_camera.viewportWidth  = static_cast<f32>(m_config.windowWidth);
    m_camera.viewportHeight = static_cast<f32>(m_config.windowHeight);

    // 5d2. Setup 3D perspective camera defaults (ADR-007 Section 8.1)
    m_camera3d.projType      = renderer::ProjectionType::PERSPECTIVE;
    m_camera3d.fovDegrees    = 60.0f;
    m_camera3d.nearPlane     = 0.1f;
    m_camera3d.farPlane      = 1000.0f;
    m_camera3d.position      = {0.0f, 0.0f, 5.0f};
    m_camera3d.target        = {0.0f, 0.0f, 0.0f};
    m_camera3d.up            = {0.0f, 1.0f, 0.0f};
    m_camera3d.viewportWidth  = static_cast<f32>(m_config.windowWidth);
    m_camera3d.viewportHeight = static_cast<f32>(m_config.windowHeight);

    // Emplace 3D camera pointer and SceneLighting3D into ECS context for Lua access
    m_world.registry().ctx().emplace<renderer::Camera*>(&m_camera3d);
    m_world.registry().ctx().emplace<renderer::SceneLighting3D>(m_sceneLighting);

    // Emplace shadow config and shadow map pointers into ECS context for Lua access.
    // Shadow mapping is disabled by default (ShadowConfig::enabled = false).
    m_world.registry().ctx().emplace<ShadowConfig*>(&m_shadowConfig);
    m_world.registry().ctx().emplace<ShadowMap*>(&m_shadowMap);

    // Emplace skybox config pointer into ECS context for Lua access.
    // Skybox rendering is disabled by default (SkyboxConfig::enabled = false).
    m_world.registry().ctx().emplace<renderer::SkyboxConfig*>(&m_skyboxConfig);

    // 5e0. Initialize 3D physics (Jolt)
    if (!physics::initPhysics3D()) {
        FFE_LOG_WARN("Core", "3D physics initialization failed — physics disabled");
    }

    // 5e. Initialize render queue (pre-allocated, persistent — not from the frame arena)
    renderer::initRenderQueue(m_renderQueue, renderer::MAX_DRAW_COMMANDS_LEGACY);

    // 5f. Register shader library and render queue pointer in ECS context
    m_world.registry().ctx().emplace<renderer::ShaderLibrary>(m_shaderLibrary);
    m_world.registry().ctx().emplace<renderer::RenderQueue*>(&m_renderQueue);

    // 6. Initialize the scripting engine — not yet implemented

    // 7. Register built-in systems
    m_world.registerSystem(FFE_SYSTEM(
        "InputUpdate",
        [](World& /*world*/, float /*dt*/) { updateInput(); },
        0   // Priority 0 -- runs before all gameplay systems
    ));
    // CopyTransformSystem runs at priority 5, before all gameplay systems (>= 100).
    // It snapshots Transform into PreviousTransform so that renderPrepareSystem can
    // lerp between the previous and current positions when building DrawCommands.
    m_world.registerSystem(FFE_SYSTEM(
        "CopyTransform",
        renderer::copyTransformSystem,
        renderer::COPY_TRANSFORM_PRIORITY
    ));
    // AnimationUpdateSystem runs at priority 50, after CopyTransform (5) and
    // before gameplay systems (>= 100). Advances sprite animation timers and
    // writes updated UV coordinates into the Sprite component each tick.
    m_world.registerSystem(FFE_SYSTEM(
        "AnimationUpdate",
        renderer::animationUpdateSystem,
        renderer::ANIMATION_UPDATE_PRIORITY
    ));
    // 3D AnimationUpdateSystem runs at priority 52, after 2D sprite animation (50)
    // and before particles (55) and gameplay systems (>= 100). Advances skeletal
    // animation time, samples keyframes, and writes bone matrices.
    m_world.registerSystem(FFE_SYSTEM(
        "Animation3DUpdate",
        renderer::animationUpdateSystem3D,
        renderer::ANIMATION_3D_UPDATE_PRIORITY
    ));
    // ParticleUpdateSystem runs at priority 55, after animation (50, 52) and
    // before gameplay systems (>= 100). Emits new particles, applies
    // velocity + gravity, kills expired particles.
    m_world.registerSystem(FFE_SYSTEM(
        "ParticleUpdate",
        renderer::particleUpdateSystem,
        renderer::PARTICLE_UPDATE_PRIORITY
    ));
    // Physics3DSyncSystem runs at priority 60, after animation (50, 52) and
    // particles (55), before gameplay systems (>= 100). Syncs Jolt body
    // transforms into ECS Transform3D components.
    m_world.registerSystem(FFE_SYSTEM(
        "Physics3DSync",
        physics3dSyncSystem,
        PHYSICS_3D_SYNC_PRIORITY
    ));
    // CollisionSystem runs at priority 200 (physics band), after gameplay
    // systems have moved entities. Detects overlaps and writes CollisionEventList
    // to ECS context for Lua callback delivery.
    m_world.registerSystem(FFE_SYSTEM(
        "Collision",
        collisionSystem,
        COLLISION_SYSTEM_PRIORITY
    ));
    // renderPrepareSystem is NOT registered in the system list — it is called
    // explicitly from Application::render() with the interpolation alpha.
    // This ensures it has access to alpha (computed after the tick loop) and
    // runs outside the fixed-rate tick for correct interpolation.

    // 8. Sort system list by priority
    m_world.sortSystems();

    // 9. Call user init callback (if any) — not yet implemented

    return Result::ok();
}

Result Application::startup() {
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
        // Explicitly request a 24-bit depth buffer for the 3D pass (ADR-007 Section 7.1).
        // GLFW defaults to 24 on most platforms for GL 3.3 core, but explicit is safer.
        glfwWindowHint(GLFW_DEPTH_BITS, 24);

#ifdef __APPLE__
        // Disable Retina framebuffer scaling. Without this, a 1280×720 window
        // gets a 2560×1440 framebuffer on Retina displays, breaking the 2D
        // coordinate system (all positions appear at half their expected location).
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

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

    // Delegate to shared subsystem init (renderer, audio, physics, ECS, systems)
    return initSubsystemsInternal();
}

void Application::shutdown() {
    m_running.store(false, std::memory_order_relaxed);

    // Reverse order of startup
    // 9. Call user shutdown callback — not yet implemented
    // 8. (nothing)
    // 7. Unregister systems (clear handled by World destructor)
    // 6. Shutdown scripting — not yet implemented

    // 5d0. Shutdown 3D physics (before RHI shutdown — physics has no GPU deps,
    // but clean teardown order is important).
    physics::shutdownPhysics3D();

    // 5e0. Destroy skybox cubemap texture (if any) before RHI shutdown.
    if (m_skyboxConfig.cubemapTexture != 0) {
        renderer::unloadCubemap(m_skyboxConfig.cubemapTexture);
        m_skyboxConfig.cubemapTexture = 0;
        m_skyboxConfig.enabled = false;
    }

    // 5e1. Destroy shadow map GPU resources (if any) before RHI shutdown.
    if (m_shadowMap.fbo != 0) {
        destroyShadowMap(m_shadowMap);
        m_shadowConfig.enabled = false;
    }

    // 5e2. Unload all 3D meshes before shutting down the RHI
    renderer::unloadAllMeshes();

    // 5e. Destroy render queue
    renderer::destroyRenderQueue(m_renderQueue);

    // 5cc2. Shutdown text renderer
    renderer::shutdownTextRenderer(m_textRenderer);

    // 5cc. Destroy default white texture
    if (rhi::isValid(m_defaultWhiteTexture)) {
        rhi::destroyTexture(m_defaultWhiteTexture);
        m_defaultWhiteTexture = {};
    }

    // 5c. Shutdown sprite batch
    renderer::shutdownSpriteBatch(m_spriteBatch);

    // 5b. Shutdown shader library
    renderer::shutdownShaderLibrary(m_shaderLibrary);

    // 5. Shutdown renderer
    rhi::shutdown();

#ifdef FFE_EDITOR
    // 4c. Shutdown editor overlay (before input and window destruction)
    m_editorOverlay.shutdown();
#endif

    // 4b. Shutdown input system (before window destruction)
    shutdownInput();

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
    // Step 3D physics simulation before ECS systems run.
    // physics3dSyncSystem (priority 60) will read back the results.
    physics::stepPhysics3D(dt);

    // Dispatch 3D collision events through the registered callback (if any).
    // Must happen after stepPhysics3D (which populates the event buffer) and
    // before ECS systems run (so Lua onCollision3D callbacks fire this frame).
    physics::dispatchCollisionEvents3D();

    for (const auto& system : m_world.systems()) {
        ZoneScoped;
        ZoneName(system.name, system.nameLength);
        system.updateFn(m_world, dt);
    }

    // Tick camera shake timer
    auto& shake = m_world.registry().ctx().get<CameraShake>();
    if (shake.duration > 0.0f) {
        shake.elapsed += dt;
        shake.duration -= dt;
        if (shake.duration <= 0.0f) {
            shake.duration = 0.0f;
            shake.intensity = 0.0f;
            shake.elapsed = 0.0f;
        }
    }

    // Sync 3D audio listener with the 3D camera (only if perspective projection is active).
    // Forward = normalize(target - position). If the distance is degenerate, skip the sync.
    if (m_camera3d.projType == renderer::ProjectionType::PERSPECTIVE) {
        const glm::vec3 diff = m_camera3d.target - m_camera3d.position;
        const float len = glm::length(diff);
        if (len > 1e-6f) {
            const glm::vec3 fwd = diff / len;
            audio::updateListenerFromCamera(
                m_camera3d.position.x, m_camera3d.position.y, m_camera3d.position.z,
                fwd.x, fwd.y, fwd.z,
                m_camera3d.up.x, m_camera3d.up.y, m_camera3d.up.z);
        }
    }
}

void Application::render(const float alpha) {
    // Populate the render queue with interpolated DrawCommands.
    // renderPrepareSystem lerps between PreviousTransform and Transform using alpha.
    // Called before sortRenderQueue() so the queue is populated before sorting.
    renderer::renderPrepareSystem(m_world, alpha);

    if (m_config.headless) return;

    // Sort the render queue now that renderPrepareSystem has populated it.
    renderer::sortRenderQueue(m_renderQueue);

    // Begin frame — use ClearColor from ECS context (settable from Lua)
    const auto& cc = m_world.registry().ctx().get<ClearColor>();
    rhi::beginFrame({cc.r, cc.g, cc.b, 1.0f});

    // --- 3D pass: render all mesh entities before 2D sprites ---
    // meshRenderSystem sets its own pipeline state (depth LESS, cull BACK, no blend),
    // draws all Transform3D + Mesh entities, then restores 2D-compatible state.
    // If no 3D entities exist, meshRenderSystem returns immediately with no state changes.
    {
        // Sync the ECS context camera pointer with the current m_camera3d values
        // (Lua may have updated m_camera3d via ffe.set3DCamera — the pointer
        // in ctx points directly to m_camera3d so it's always current)
        renderer::meshRenderSystem(m_world, m_camera3d, m_shadowConfig, m_shadowMap);
    }

    // --- Skybox pass: render cubemap environment after 3D meshes, before 2D ---
    // Uses depth func LEQUAL so skybox writes at max depth (behind all 3D geometry).
    // meshRenderSystem already restored 2D-compatible state; renderSkybox temporarily
    // re-enables depth test for the skybox draw and restores state after.
    renderer::renderSkybox(m_world, m_camera3d, m_skyboxConfig);

    // Apply camera shake (if active) and compute VP matrix.
    // Shake uses exponential decay for a punchy start that fades naturally.
    // Low-frequency sin/cos produce a visible rumble rather than jitter.
    // Sub-pixel offsets are kept (no rounding) for smooth motion.
    // Effective offset is capped at 3 pixels regardless of intensity.
    auto& shake = m_world.registry().ctx().get<CameraShake>();
    renderer::Camera shakeCamera = m_camera;
    if (shake.duration > 0.0f) {
        const f32 t = shake.elapsed / (shake.elapsed + shake.duration);
        const f32 strength = shake.intensity * std::exp(-5.0f * t);
        const f32 ox = strength * std::sin(shake.elapsed * 15.0f);
        const f32 oy = strength * std::cos(shake.elapsed * 11.0f);
        constexpr f32 MAX_OFFSET = 3.0f;
        shakeCamera.position.x += std::clamp(ox, -MAX_OFFSET, MAX_OFFSET);
        shakeCamera.position.y += std::clamp(oy, -MAX_OFFSET, MAX_OFFSET);
    }
    const glm::mat4 vp = renderer::computeViewProjectionMatrix(shakeCamera);
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
            sprite.uvMin    = {cmd.uvMinX, cmd.uvMinY};
            sprite.uvMax    = {cmd.uvMaxX, cmd.uvMaxY};
            sprite.color    = {
                static_cast<f32>(cmd.colorR) / 255.0f,
                static_cast<f32>(cmd.colorG) / 255.0f,
                static_cast<f32>(cmd.colorB) / 255.0f,
                static_cast<f32>(cmd.colorA) / 255.0f
            };
            sprite.rotation = cmd.rotation;
            sprite.depth    = 0.0f;

            // Use the default white texture if the sprite has no texture assigned
            const rhi::TextureHandle tex = rhi::isValid(cmd.texture)
                ? cmd.texture : m_defaultWhiteTexture;
            renderer::addSprite(m_spriteBatch, tex, sprite);
        }

        // Render tilemaps directly into the sprite batch (bypasses render queue).
        renderer::renderTilemaps(m_world, m_spriteBatch);

        // Render particles directly into the sprite batch (bypasses render queue).
        renderer::renderParticles(m_world, m_spriteBatch, m_defaultWhiteTexture);

        renderer::endSpriteBatch(m_spriteBatch);

        // Flush queued HUD text in screen space.
        // flushText sets its own screen-space VP matrix internally.
        renderer::flushText(m_textRenderer, m_spriteBatch, staging);
    }

#ifdef FFE_EDITOR
    // Editor overlay — renders ImGui on top of the sprite batch
    m_editorOverlay.beginFrame();
    m_editorOverlay.render(m_world);
#endif

    // End frame — swap buffers
    rhi::endFrame(m_window);
}

} // namespace ffe
