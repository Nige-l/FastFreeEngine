// FFE -- Pong Demo
//
// Classic two-player Pong game driven from a Lua script.
// Demonstrates: input, collision, entity lifecycle, HUD, audio, transforms.
//
// Controls:
//   W/S        left paddle
//   UP/DOWN    right paddle
//   SPACE      serve the ball
//   ESC        quit
//
// Coordinate system: centered origin, x: -640..640, y: -360..360.
// Window: 1280x720.

#include "core/application.h"
#include "core/ecs.h"
#include "core/logging.h"
#include "audio/audio.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/texture_loader.h"
#include "scripting/script_engine.h"
#include "../demo_paths.h"

#include <cstdint>

namespace {

// ---------------------------------------------------------------------------
// Context stored in the ECS registry
// ---------------------------------------------------------------------------
struct PongContext {
    ffe::EntityId      paddle   = ffe::NULL_ENTITY;
    ffe::ScriptEngine* scripts  = nullptr;
    bool               sceneReady  = false;
    bool               startupDone = false;
};

// ---------------------------------------------------------------------------
// Helper: create a solid-colour 4x4 RGBA8 texture.
// ---------------------------------------------------------------------------
ffe::rhi::TextureHandle makeSolidTexture(const ffe::u8 r, const ffe::u8 g,
                                         const ffe::u8 b, const ffe::u8 a)
{
    static constexpr ffe::u32 DIM = 4u;
    ffe::u8 pixels[DIM * DIM * 4u];
    for (ffe::u32 i = 0u; i < DIM * DIM; ++i) {
        pixels[i * 4u + 0u] = r;
        pixels[i * 4u + 1u] = g;
        pixels[i * 4u + 2u] = b;
        pixels[i * 4u + 3u] = a;
    }
    ffe::rhi::TextureDesc desc;
    desc.width     = DIM;
    desc.height    = DIM;
    desc.format    = ffe::rhi::TextureFormat::RGBA8;
    desc.filter    = ffe::rhi::TextureFilter::NEAREST;
    desc.wrap      = ffe::rhi::TextureWrap::CLAMP_TO_EDGE;
    desc.pixelData = pixels;
    return ffe::rhi::createTexture(desc);
}

// ---------------------------------------------------------------------------
// Static storage for GPU handles
// ---------------------------------------------------------------------------
static ffe::rhi::TextureHandle s_whiteTex = {0};
static bool                    s_texOwned = false;

// ---------------------------------------------------------------------------
// pongSystem -- registered at priority 100
// ---------------------------------------------------------------------------
void pongSystem(ffe::World& world, const float dt)
{
    auto* ctx = world.registry().ctx().find<PongContext>();
    if (ctx == nullptr) { return; }

    // Scene setup: create a dummy entity for the Lua script to key off of,
    // and load the white texture for paddles/ball.
    if (!ctx->sceneReady) {
        static char assetRootBuf[512];
        if (!demoAssetRoot(assetRootBuf, sizeof(assetRootBuf))) {
            FFE_LOG_ERROR("Pong", "Failed to resolve asset root");
            return;
        }
        ffe::renderer::setAssetRoot(assetRootBuf);

        ffe::rhi::TextureHandle loaded =
            ffe::renderer::loadTexture("textures/white.png");

        if (ffe::rhi::isValid(loaded)) {
            s_whiteTex = loaded;
            s_texOwned = true;
        } else {
            s_whiteTex = makeSolidTexture(0xFF, 0xFF, 0xFF, 0xFF);
            s_texOwned = true;
        }

        // Create a "controller" entity — Lua uses it as the entry point.
        ctx->paddle = world.createEntity();
        auto& tf = world.addComponent<ffe::Transform>(ctx->paddle);
        tf.position = {0.0f, 0.0f, 0.0f};
        auto& prevTf = world.addComponent<ffe::PreviousTransform>(ctx->paddle);
        prevTf.position = tf.position;
        prevTf.scale    = tf.scale;
        prevTf.rotation = tf.rotation;
        // Invisible — no Sprite component. Lua creates all visible entities.

        ctx->sceneReady = true;
        FFE_LOG_INFO("Pong", "Scene ready, white texture loaded");
    }

    // Load pong.lua
    if (!ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised() && ctx->paddle != ffe::NULL_ENTITY)
    {
        ctx->scripts->setWorld(&world);

        static char scriptRootBuf[512];
        if (!demoScriptRoot("pong", scriptRootBuf, sizeof(scriptRootBuf))) {
            FFE_LOG_ERROR("Pong", "Failed to resolve script root");
            return;
        }
        ctx->scripts->setScriptRoot(scriptRootBuf);

        // Set save root to project root — saves go in <projectRoot>/saves/
        static char saveRootBuf[512];
        if (demoProjectRoot(saveRootBuf, sizeof(saveRootBuf))) {
            ctx->scripts->setSaveRoot(saveRootBuf);
        }

        const bool ok = ctx->scripts->doFile("pong.lua", scriptRootBuf);
        if (!ok) {
            FFE_LOG_ERROR("Pong", "pong.lua failed to load");
        } else {
            FFE_LOG_INFO("Pong", "pong.lua loaded; game active");
        }
        ctx->startupDone = true;
    }

    // Per-frame: call Lua update()
    if (ctx->startupDone && ctx->paddle != ffe::NULL_ENTITY &&
        ctx->scripts != nullptr && ctx->scripts->isInitialised())
    {
        ctx->scripts->callFunction("update",
                                   static_cast<ffe::i64>(ctx->paddle),
                                   static_cast<double>(dt));

        ctx->scripts->tickTimers(dt);
        ctx->scripts->deliverCollisionEvents(world);
    }
}

} // anonymous namespace

int main()
{
    ffe::ApplicationConfig config;
    config.windowTitle  = "FFE - Pong (W/S, Up/Down, Space, M music, F1 editor, ESC quit)";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    if (!ffe::audio::init(false)) {
        FFE_LOG_WARN("Pong", "Audio init failed — audio disabled");
    }

    PongContext pongCtx;
    app.world().registry().ctx().emplace<PongContext>(pongCtx);

    static ffe::ScriptEngine scriptEngine;
    if (!scriptEngine.init()) {
        FFE_LOG_ERROR("Pong", "ScriptEngine init failed");
    } else {
        app.world().registry().ctx().get<PongContext>().scripts = &scriptEngine;
    }

    app.world().registerSystem(FFE_SYSTEM("Pong", pongSystem, 100));
    app.world().sortSystems();

    const int32_t result = app.run();

    if (s_texOwned && ffe::rhi::isValid(s_whiteTex)) {
        ffe::renderer::unloadTexture(s_whiteTex);
    }

    ffe::audio::shutdown();
    return result;
}
