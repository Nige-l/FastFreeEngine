// FFE -- Breakout Demo
//
// Classic Breakout: paddle, ball, destructible bricks.
// Demonstrates mass entity destruction and creation from Lua.
//
// Controls:
//   A/D or LEFT/RIGHT  move paddle
//   SPACE              launch ball / restart
//   ESC                quit
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

struct BreakoutContext {
    ffe::EntityId      paddle   = ffe::NULL_ENTITY;
    ffe::ScriptEngine* scripts  = nullptr;
    bool               sceneReady  = false;
    bool               startupDone = false;
};

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

static ffe::rhi::TextureHandle s_whiteTex = {0};
static bool                    s_texOwned = false;

void breakoutSystem(ffe::World& world, const float dt)
{
    auto* ctx = world.registry().ctx().find<BreakoutContext>();
    if (ctx == nullptr) { return; }

    if (!ctx->sceneReady) {
        static char assetRootBuf[512];
        if (!demoAssetRoot(assetRootBuf, sizeof(assetRootBuf))) {
            FFE_LOG_ERROR("Breakout", "Failed to resolve asset root");
            return;
        }
        ffe::renderer::setAssetRoot(assetRootBuf);

        ffe::rhi::TextureHandle loaded = ffe::renderer::loadTexture("textures/white.png");
        if (ffe::rhi::isValid(loaded)) {
            s_whiteTex = loaded;
            s_texOwned = true;
        } else {
            s_whiteTex = makeSolidTexture(0xFF, 0xFF, 0xFF, 0xFF);
            s_texOwned = true;
        }

        ctx->paddle = world.createEntity();
        auto& tf = world.addComponent<ffe::Transform>(ctx->paddle);
        tf.position = {0.0f, 0.0f, 0.0f};
        auto& prevTf = world.addComponent<ffe::PreviousTransform>(ctx->paddle);
        prevTf.position = tf.position;
        prevTf.scale    = tf.scale;
        prevTf.rotation = tf.rotation;

        ctx->sceneReady = true;
        FFE_LOG_INFO("Breakout", "Scene ready");
    }

    if (!ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised() && ctx->paddle != ffe::NULL_ENTITY)
    {
        ctx->scripts->setWorld(&world);

        static char scriptRootBuf[512];
        if (!demoScriptRoot("breakout", scriptRootBuf, sizeof(scriptRootBuf))) {
            FFE_LOG_ERROR("Breakout", "Failed to resolve script root");
            return;
        }
        ctx->scripts->setScriptRoot(scriptRootBuf);

        // Set save root to project root — saves go in <projectRoot>/saves/
        static char saveRootBuf[512];
        if (demoProjectRoot(saveRootBuf, sizeof(saveRootBuf))) {
            ctx->scripts->setSaveRoot(saveRootBuf);
        }

        const bool ok = ctx->scripts->doFile("breakout.lua", scriptRootBuf);
        if (!ok) {
            FFE_LOG_ERROR("Breakout", "breakout.lua failed to load");
        } else {
            FFE_LOG_INFO("Breakout", "breakout.lua loaded; game active");
        }
        ctx->startupDone = true;
    }

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
    config.windowTitle  = "FFE - Breakout (A/D, Space, F1 editor, ESC quit)";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    if (!ffe::audio::init(false)) {
        FFE_LOG_WARN("Breakout", "Audio init failed — audio disabled");
    }

    BreakoutContext breakoutCtx;
    app.world().registry().ctx().emplace<BreakoutContext>(breakoutCtx);

    static ffe::ScriptEngine scriptEngine;
    if (!scriptEngine.init()) {
        FFE_LOG_ERROR("Breakout", "ScriptEngine init failed");
    } else {
        app.world().registry().ctx().get<BreakoutContext>().scripts = &scriptEngine;
    }

    app.world().registerSystem(FFE_SYSTEM("Breakout", breakoutSystem, 100));
    app.world().sortSystems();

    const int32_t result = app.run();

    if (s_texOwned && ffe::rhi::isValid(s_whiteTex)) {
        ffe::renderer::unloadTexture(s_whiteTex);
    }

    ffe::audio::shutdown();
    return result;
}
