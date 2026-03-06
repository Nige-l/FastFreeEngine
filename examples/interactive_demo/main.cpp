// FFE -- Interactive Demo
//
// Demonstrates: WASD movement, texture loading (raw RGBA fallback), Lua scripting,
// multiple entities with layered rendering, and ESC to quit.
//
// No PNG assets are available in this repository, so all textures are created
// programmatically from raw RGBA pixel data using rhi::createTexture().
// This is documented as a friction point in the usage report.
//
// Coordinate system: centered origin, -640..640 (x), -360..360 (y).
// Window: 1280x720 (same as hello_sprites).

#include "core/application.h"
#include "core/ecs.h"
#include "core/input.h"
#include "core/logging.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "scripting/script_engine.h"

#include <algorithm>
#include <cstdint>

// ---------------------------------------------------------------------------
// Demo constants
// ---------------------------------------------------------------------------
namespace {

static constexpr float HALF_W       = 640.0f;
static constexpr float HALF_H       = 360.0f;
static constexpr float PLAYER_SIZE  = 48.0f;
static constexpr float PLAYER_SPEED = 220.0f;

static constexpr ffe::u32 BG_SPRITE_COUNT = 8;
static constexpr float    BG_SPRITE_SIZE  = 60.0f;

// ---------------------------------------------------------------------------
// ECS context tags stored in the registry so systems can reach shared state
// and the player entity ID without global variables.
// ---------------------------------------------------------------------------
struct DemoContext {
    ffe::EntityId player  = ffe::NULL_ENTITY;
    bool          started = false;
};

// ---------------------------------------------------------------------------
// Helper: create a solid-colour 4x4 RGBA8 texture
// Returns TextureHandle{0} on failure (caller must check).
// ---------------------------------------------------------------------------
ffe::rhi::TextureHandle makeSolidTexture(const ffe::u8 r, const ffe::u8 g,
                                         const ffe::u8 b, const ffe::u8 a)
{
    // 4x4 pixels -- small enough to be practically free in VRAM
    static constexpr ffe::u32 DIM = 4;
    ffe::u8 pixels[DIM * DIM * 4];
    for (ffe::u32 i = 0; i < DIM * DIM; ++i) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }

    ffe::rhi::TextureDesc desc;
    desc.width      = DIM;
    desc.height     = DIM;
    desc.format     = ffe::rhi::TextureFormat::RGBA8;
    desc.filter     = ffe::rhi::TextureFilter::NEAREST;
    desc.wrap       = ffe::rhi::TextureWrap::CLAMP_TO_EDGE;
    desc.pixelData  = pixels;
    return ffe::rhi::createTexture(desc);
}

// ---------------------------------------------------------------------------
// Background sprite positions (fixed decorative grid)
// ---------------------------------------------------------------------------
static constexpr float BG_POSITIONS[BG_SPRITE_COUNT][2] = {
    {-520.0f,  280.0f},
    { 520.0f,  280.0f},
    {-520.0f, -280.0f},
    { 520.0f, -280.0f},
    {   0.0f,  280.0f},
    {   0.0f, -280.0f},
    {-300.0f,    0.0f},
    { 300.0f,    0.0f},
};

static constexpr float BG_COLORS[BG_SPRITE_COUNT][4] = {
    {0.8f, 0.2f, 0.2f, 0.6f},
    {0.2f, 0.7f, 0.8f, 0.6f},
    {0.9f, 0.8f, 0.2f, 0.6f},
    {0.6f, 0.3f, 0.9f, 0.6f},
    {0.3f, 0.9f, 0.4f, 0.6f},
    {0.9f, 0.5f, 0.2f, 0.6f},
    {0.8f, 0.8f, 0.8f, 0.5f},
    {0.5f, 0.9f, 0.9f, 0.5f},
};

// ---------------------------------------------------------------------------
// Static state (follows hello_sprites pattern -- no global mutable objects,
// just plain data that avoids static constructors)
// ---------------------------------------------------------------------------
static bool s_texturesCreated  = false;
static bool s_scriptsRun       = false;

static ffe::rhi::TextureHandle s_playerTex = {0};
static ffe::rhi::TextureHandle s_bgTex     = {0};

// ScriptEngine lives here -- it must outlive the demo system's calls.
// It is not per-frame state; it is created once before run() and destroyed
// after run() returns.  Declared outside the system so it can be destroyed
// cleanly in main().
static ffe::ScriptEngine* s_scripts = nullptr;

// ---------------------------------------------------------------------------
// interactiveDemoSystem -- registered at priority 100 (gameplay)
// ---------------------------------------------------------------------------
void interactiveDemoSystem(ffe::World& world, const float dt)
{
    // -----------------------------------------------------------------------
    // One-time setup: create textures, entities, run startup Lua script
    // -----------------------------------------------------------------------
    auto* ctx = world.registry().ctx().find<DemoContext>();
    if (ctx == nullptr) {
        // Context not yet injected -- skip this tick (shouldn't happen)
        return;
    }

    if (!s_texturesCreated) {
        // Create solid-colour textures (no PNG assets available).
        // This is the raw-RGBA fallback path documented in the usage report.
        s_playerTex = makeSolidTexture(0x40, 0xFF, 0x40, 0xFF); // bright green
        s_bgTex     = makeSolidTexture(0x60, 0x60, 0xA0, 0xFF); // slate blue

        if (!ffe::rhi::isValid(s_playerTex)) {
            FFE_LOG_ERROR("Demo", "Failed to create player texture");
        }
        if (!ffe::rhi::isValid(s_bgTex)) {
            FFE_LOG_ERROR("Demo", "Failed to create background texture");
        }

        // ------------------------------------------------------------------
        // Background sprites -- static decorative layer (layer 0)
        // ------------------------------------------------------------------
        for (ffe::u32 i = 0; i < BG_SPRITE_COUNT; ++i) {
            const ffe::EntityId bg = world.createEntity();

            auto& t = world.addComponent<ffe::Transform>(bg);
            t.position = {BG_POSITIONS[i][0], BG_POSITIONS[i][1], 0.0f};

            // Background sprites are static — omit PreviousTransform so
            // renderPrepareSystem renders them without lerp (no interpolation needed).
            auto& s  = world.addComponent<ffe::Sprite>(bg);
            s.texture = s_bgTex;
            s.size    = {BG_SPRITE_SIZE, BG_SPRITE_SIZE};
            s.color   = {BG_COLORS[i][0], BG_COLORS[i][1],
                         BG_COLORS[i][2], BG_COLORS[i][3]};
            s.layer   = 0;
        }

        // ------------------------------------------------------------------
        // Player sprite -- starts at center (layer 5)
        // ------------------------------------------------------------------
        ctx->player = world.createEntity();

        auto& pt = world.addComponent<ffe::Transform>(ctx->player);
        pt.position = {0.0f, 0.0f, 0.0f};

        // Opt in to render interpolation so the player moves smoothly at any frame rate.
        auto& ppt = world.addComponent<ffe::PreviousTransform>(ctx->player);
        ppt.position = pt.position;
        ppt.scale    = pt.scale;
        ppt.rotation = pt.rotation;

        auto& ps  = world.addComponent<ffe::Sprite>(ctx->player);
        ps.texture = s_playerTex;
        ps.size    = {PLAYER_SIZE, PLAYER_SIZE};
        ps.color   = {1.0f, 1.0f, 1.0f, 1.0f}; // white tint -- shows true texture colour
        ps.layer   = 5;

        s_texturesCreated = true;
        FFE_LOG_INFO("Demo", "Scene created: %u bg sprites + 1 player", BG_SPRITE_COUNT);
    }

    // -----------------------------------------------------------------------
    // One-time Lua startup message (run after textures created so the state
    // is visible to the log reader in the correct order)
    // -----------------------------------------------------------------------
    if (!s_scriptsRun && s_scripts != nullptr && s_scripts->isInitialised()) {
        const bool ok = s_scripts->doString(
            "ffe.log('Interactive demo started! Use WASD to move. Press ESC to quit.')");
        if (!ok) {
            FFE_LOG_WARN("Demo", "Lua startup message failed (logged above)");
        }
        s_scriptsRun = true;
    }

    // -----------------------------------------------------------------------
    // Player movement -- WASD, clamped to screen bounds
    // -----------------------------------------------------------------------
    if (ctx->player != ffe::NULL_ENTITY && world.isValid(ctx->player)) {
        auto& transform = world.getComponent<ffe::Transform>(ctx->player);

        const float step = PLAYER_SPEED * dt;

        if (ffe::isKeyHeld(ffe::Key::W)) { transform.position.y += step; }
        if (ffe::isKeyHeld(ffe::Key::S)) { transform.position.y -= step; }
        if (ffe::isKeyHeld(ffe::Key::A)) { transform.position.x -= step; }
        if (ffe::isKeyHeld(ffe::Key::D)) { transform.position.x += step; }

        // Clamp to visible area (centered coordinate system, half-extents)
        const float xMax = HALF_W - PLAYER_SIZE * 0.5f;
        const float yMax = HALF_H - PLAYER_SIZE * 0.5f;
        transform.position.x = std::clamp(transform.position.x, -xMax, xMax);
        transform.position.y = std::clamp(transform.position.y, -yMax, yMax);
    }

    // -----------------------------------------------------------------------
    // ESC to quit -- set ShutdownSignal in ECS context.
    // Application::run() checks this after every tick and exits the loop.
    // No Application* needed -- the signal lives in the registry context.
    // -----------------------------------------------------------------------
    if (ffe::isKeyPressed(ffe::Key::ESCAPE)) {
        FFE_LOG_INFO("Demo", "ESC pressed -- requesting shutdown");
        world.registry().ctx().get<ffe::ShutdownSignal>().requested = true;
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    ffe::ApplicationConfig config;
    config.windowTitle  = "FFE - Interactive Demo (WASD to move, ESC to quit)";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    // -----------------------------------------------------------------------
    // Inject DemoContext into the ECS registry so the system can share
    // the player entity ID without global state.
    // ShutdownSignal is already emplaced by Application::startup().
    // -----------------------------------------------------------------------
    DemoContext ctx;
    app.world().registry().ctx().emplace<DemoContext>(ctx);

    // -----------------------------------------------------------------------
    // Lua scripting setup -- must happen before run() so the startup message
    // fires on the first tick.
    // -----------------------------------------------------------------------
    static ffe::ScriptEngine scriptEngine; // static duration -- safe for the demo
    s_scripts = &scriptEngine;

    if (!scriptEngine.init()) {
        FFE_LOG_ERROR("Demo", "ScriptEngine init failed -- Lua features disabled");
        s_scripts = nullptr;
    }

    // -----------------------------------------------------------------------
    // Register the demo system
    // -----------------------------------------------------------------------
    app.world().registerSystem(FFE_SYSTEM(
        "InteractiveDemo",
        interactiveDemoSystem,
        100  // Gameplay priority -- runs before render prepare (500)
    ));
    app.world().sortSystems();

    // -----------------------------------------------------------------------
    // Run -- blocks until shutdown (ESC or window close)
    // -----------------------------------------------------------------------
    const int32_t result = app.run();

    // -----------------------------------------------------------------------
    // Cleanup GPU resources after run() returns
    // -----------------------------------------------------------------------
    if (ffe::rhi::isValid(s_playerTex)) {
        ffe::rhi::destroyTexture(s_playerTex);
    }
    if (ffe::rhi::isValid(s_bgTex)) {
        ffe::rhi::destroyTexture(s_bgTex);
    }

    // ScriptEngine destructor calls shutdown() automatically
    return result;
}
