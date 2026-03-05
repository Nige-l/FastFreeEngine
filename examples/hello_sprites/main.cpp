// FFE — Hello Sprites Demo
// Opens a window and renders 20 colored sprites bouncing around.
// This is the first visual output from FastFreeEngine.

#include "core/application.h"
#include "core/ecs.h"
#include "core/logging.h"
#include "renderer/render_system.h"
#include "renderer/rhi_types.h"

#include <cmath>

namespace {

// Simple velocity component for the demo
struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
};

// Palette of distinct, saturated colors
struct ColorEntry {
    float r, g, b;
};

static constexpr ColorEntry PALETTE[] = {
    {1.0f, 0.2f, 0.2f},   // Red
    {0.2f, 0.8f, 0.2f},   // Green
    {0.3f, 0.4f, 1.0f},   // Blue
    {1.0f, 0.9f, 0.1f},   // Yellow
    {1.0f, 0.5f, 0.0f},   // Orange
    {0.8f, 0.2f, 0.8f},   // Magenta
    {0.0f, 0.8f, 0.8f},   // Cyan
    {1.0f, 1.0f, 1.0f},   // White
    {0.6f, 1.0f, 0.4f},   // Lime
    {1.0f, 0.4f, 0.6f},   // Pink
};
static constexpr ffe::u32 PALETTE_SIZE = sizeof(PALETTE) / sizeof(PALETTE[0]);

static constexpr ffe::u32 SPRITE_COUNT = 20;
static constexpr float SPRITE_SIZE     = 40.0f;
static constexpr float HALF_WIDTH      = 640.0f;
static constexpr float HALF_HEIGHT     = 360.0f;

bool s_initialized = false;

// Simple LCG for deterministic initial positions (no <random> header needed)
ffe::u32 s_seed = 12345;
float randomFloat(const float lo, const float hi) {
    s_seed = s_seed * 1664525u + 1013904223u;
    const float t = static_cast<float>(s_seed & 0xFFFF) / 65535.0f;
    return lo + t * (hi - lo);
}

void helloSpritesSystem(ffe::World& world, const float dt) {
    if (!s_initialized) {
        // Get the default white texture from context (created by Application)
        const auto* defaultTex = world.registry().ctx().find<ffe::rhi::TextureHandle>();
        const ffe::rhi::TextureHandle tex = (defaultTex != nullptr) ? *defaultTex : ffe::rhi::TextureHandle{0};

        for (ffe::u32 i = 0; i < SPRITE_COUNT; ++i) {
            const ffe::EntityId entity = world.createEntity();

            auto& transform     = world.addComponent<ffe::Transform>(entity);
            transform.position  = {
                randomFloat(-HALF_WIDTH + SPRITE_SIZE, HALF_WIDTH - SPRITE_SIZE),
                randomFloat(-HALF_HEIGHT + SPRITE_SIZE, HALF_HEIGHT - SPRITE_SIZE),
                0.0f
            };

            const ColorEntry& c = PALETTE[i % PALETTE_SIZE];
            auto& sprite  = world.addComponent<ffe::Sprite>(entity);
            sprite.texture = tex;
            sprite.size    = {SPRITE_SIZE, SPRITE_SIZE};
            sprite.color   = {c.r, c.g, c.b, 1.0f};

            auto& vel = world.addComponent<Velocity>(entity);
            vel.vx = randomFloat(-200.0f, 200.0f);
            vel.vy = randomFloat(-200.0f, 200.0f);
        }

        s_initialized = true;
        FFE_LOG_INFO("Demo", "Created %u sprites", SPRITE_COUNT);
    }

    // Move sprites and bounce off window edges
    const auto view = world.view<ffe::Transform, Velocity>();
    for (const auto entity : view) {
        auto& transform = view.get<ffe::Transform>(entity);
        auto& vel       = view.get<Velocity>(entity);

        transform.position.x += vel.vx * dt;
        transform.position.y += vel.vy * dt;

        const float edge = SPRITE_SIZE * 0.5f;
        if (transform.position.x < -HALF_WIDTH + edge) {
            transform.position.x = -HALF_WIDTH + edge;
            vel.vx = std::abs(vel.vx);
        } else if (transform.position.x > HALF_WIDTH - edge) {
            transform.position.x = HALF_WIDTH - edge;
            vel.vx = -std::abs(vel.vx);
        }

        if (transform.position.y < -HALF_HEIGHT + edge) {
            transform.position.y = -HALF_HEIGHT + edge;
            vel.vy = std::abs(vel.vy);
        } else if (transform.position.y > HALF_HEIGHT - edge) {
            transform.position.y = HALF_HEIGHT - edge;
            vel.vy = -std::abs(vel.vy);
        }
    }
}

} // anonymous namespace

int main() {
    ffe::ApplicationConfig config;
    config.windowTitle = "FFE - Hello Sprites";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    const ffe::SystemDescriptor desc = {
        "HelloSprites",
        12, // strlen("HelloSprites")
        helloSpritesSystem,
        100  // Gameplay priority — runs before render prepare (500)
    };
    app.world().registerSystem(desc);
    app.world().sortSystems();

    return app.run();
}
