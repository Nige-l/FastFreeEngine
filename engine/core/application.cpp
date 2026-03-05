#include "core/application.h"

#include <chrono>

namespace ffe {

Application::Application(const ApplicationConfig& config)
    : m_config(config)
    , m_frameAllocator(arenaDefaultSize())
{
}

Application::~Application() {
    if (m_running) {
        shutdown();
    }
}

int32_t Application::run() {
    const Result startResult = startup();
    if (!startResult) {
        FFE_LOG_FATAL("Core", "Startup failed: %s", startResult.message());
        return 1;
    }

    m_running = true;

    const float fixedDt = 1.0f / m_config.tickRate;   // e.g., 1/60 = 0.01667s
    const float maxFrameTime = 0.25f;                  // Spiral-of-death clamp

    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::duration<float>;

    auto previousTime = Clock::now();
    float accumulator = 0.0f;

    // In headless mode, run a limited number of frames then auto-shutdown
    int32_t frameCount = 0;
    static constexpr int32_t HEADLESS_MAX_FRAMES = 10;

    while (m_running) {
        ZoneScoped; // Tracy: marks entire frame

        const auto currentTime = Clock::now();
        float frameTime = Duration(currentTime - previousTime).count();
        previousTime = currentTime;

        // Clamp to prevent spiral of death (e.g., debugger breakpoint)
        if (frameTime > maxFrameTime) {
            frameTime = maxFrameTime;
        }

        accumulator += frameTime;

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
                m_running = false;
            }
        }
    }

    shutdown();
    return 0;
}

void Application::requestShutdown() {
    m_running = false;
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

    // 4. Create the window (unless headless) — not yet implemented
    // 5. Initialize the renderer (unless headless) — not yet implemented
    // 6. Initialize the scripting engine — not yet implemented

    // 7. Register built-in systems — none yet

    // 8. Sort system list by priority
    m_world.sortSystems();

    // 9. Call user init callback (if any) — not yet implemented

    return Result::ok();
}

void Application::shutdown() {
    m_running = false;

    // Reverse order of startup
    // 9. Call user shutdown callback — not yet implemented
    // 8. (nothing)
    // 7. Unregister systems (clear handled by World destructor)
    // 6. Shutdown scripting — not yet implemented
    // 5. Shutdown renderer — not yet implemented
    // 4. Destroy window — not yet implemented
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

void Application::render([[maybe_unused]] const float alpha) {
    // Renderer not yet implemented — placeholder
}

} // namespace ffe
