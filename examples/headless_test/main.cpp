// FFE — Headless Test
// Creates entities with Transform and Sprite components, runs 10 frames
// in headless mode, and prints stats. Safe for CI without a display.

#include "core/application.h"
#include "core/ecs.h"
#include "core/logging.h"
#include "renderer/render_system.h"
#include "renderer/rhi_types.h"

#include <cstdio>

namespace {

static constexpr ffe::u32 ENTITY_COUNT = 50;
bool s_initialized = false;
ffe::u32 s_tickCount = 0;

void headlessSystem(ffe::World& world, const float dt) {
    (void)dt;

    if (!s_initialized) {
        for (ffe::u32 i = 0; i < ENTITY_COUNT; ++i) {
            const ffe::EntityId entity = world.createEntity();

            auto& transform    = world.addComponent<ffe::Transform>(entity);
            transform.position = {
                static_cast<float>(i) * 10.0f - 250.0f,
                static_cast<float>(i) * 5.0f  - 125.0f,
                0.0f
            };

            auto& sprite  = world.addComponent<ffe::Sprite>(entity);
            sprite.size    = {32.0f, 32.0f};
            sprite.color   = {
                static_cast<float>(i % 5) / 4.0f,
                static_cast<float>((i + 1) % 5) / 4.0f,
                static_cast<float>((i + 2) % 5) / 4.0f,
                1.0f
            };
        }

        s_initialized = true;
        std::printf("[headless] Created %u entities\n", ENTITY_COUNT);
    }

    ++s_tickCount;

    // Move all sprites slightly each tick
    const auto view = world.view<ffe::Transform>();
    for (const auto entity : view) {
        auto& transform = view.get<ffe::Transform>(entity);
        transform.position.x += 1.0f;
    }

    std::printf("[headless] Tick %u — entities with Transform+Sprite: %zu\n",
                s_tickCount,
                static_cast<std::size_t>(world.view<ffe::Transform, ffe::Sprite>().size_hint()));
}

} // anonymous namespace

int main() {
    ffe::ApplicationConfig config;
    config.windowTitle = "FFE Headless Test";
    config.headless    = true;

    ffe::Application app(config);

    const ffe::SystemDescriptor desc = {
        "HeadlessTest",
        12, // strlen("HeadlessTest")
        headlessSystem,
        100
    };
    app.world().registerSystem(desc);
    app.world().sortSystems();

    std::printf("[headless] Starting 10-frame run...\n");
    const int result = app.run();
    std::printf("[headless] Finished with exit code %d, total ticks: %u\n", result, s_tickCount);

    return result;
}
