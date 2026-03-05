#pragma once

#include "core/types.h"
#include "core/arena_allocator.h"
#include "core/logging.h"
#include "core/ecs.h"

#include <tracy/Tracy.hpp>

#include <atomic>

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
};

} // namespace ffe
