// FFE -- Net Arena Demo
//
// A minimal networked multiplayer demo. Two instances on the same machine:
//   Instance 1: press S to host on port 7777
//   Instance 2: press C to connect to localhost:7777
//
// Players are colored squares that move with WASD. The server is authoritative
// and replicates entity state via snapshots. Clients use prediction for their
// local player.
//
// The C++ host initialises the engine and loads net_arena.lua. All game logic
// (server, client, input, rendering) lives in Lua.

#include "core/application.h"
#include "core/ecs.h"
#include "core/logging.h"
#include "audio/audio.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "scripting/script_engine.h"
#include "../demo_paths.h"

#include <cstdint>

namespace {

// ---------------------------------------------------------------------------
// Context stored in the ECS registry so the system can reach shared state.
// ---------------------------------------------------------------------------
struct NetDemoContext {
    ffe::ScriptEngine* scripts     = nullptr;
    bool               startupDone = false;
};

// ---------------------------------------------------------------------------
// netDemoSystem — registered at priority 100 (gameplay)
// ---------------------------------------------------------------------------
void netDemoSystem(ffe::World& world, const float dt)
{
    auto* ctx = world.registry().ctx().find<NetDemoContext>();
    if (ctx == nullptr) { return; }

    // One-time: load the Lua script once the renderer is up.
    if (!ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised())
    {
        ctx->scripts->setWorld(&world);

        static char scriptRootBuf[512];
        if (!demoScriptRoot("net_demo", scriptRootBuf, sizeof(scriptRootBuf))) {
            FFE_LOG_ERROR("NetDemo", "Failed to resolve script root");
            return;
        }
        ctx->scripts->setScriptRoot(scriptRootBuf);

        const bool ok = ctx->scripts->doFile("net_arena.lua", scriptRootBuf);
        if (!ok) {
            FFE_LOG_ERROR("NetDemo", "net_arena.lua failed to load");
        } else {
            FFE_LOG_INFO("NetDemo", "net_arena.lua loaded");
        }

        ctx->startupDone = true;
    }

    // Per-frame: call the Lua update() function.
    if (ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised())
    {
        ctx->scripts->callFunction("update",
                                   static_cast<ffe::i64>(0),
                                   static_cast<double>(dt));

        ctx->scripts->tickTimers(dt);
        ctx->scripts->deliverCollisionEvents(world);
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    ffe::ApplicationConfig config;
    config.windowTitle  = "FFE - Net Arena (S=host, C=connect, WASD=move, ESC=quit)";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    // Initialize audio (not critical for this demo).
    if (!ffe::audio::init(false)) {
        FFE_LOG_WARN("NetDemo", "Audio init failed -- audio disabled");
    }

    // Inject context into ECS.
    NetDemoContext demoCtx;
    app.world().registry().ctx().emplace<NetDemoContext>(demoCtx);

    // Initialise scripting engine.
    static ffe::ScriptEngine scriptEngine;
    if (!scriptEngine.init()) {
        FFE_LOG_ERROR("NetDemo", "ScriptEngine init failed");
    } else {
        app.world().registry().ctx().get<NetDemoContext>().scripts = &scriptEngine;
    }

    // Register game system.
    app.world().registerSystem(FFE_SYSTEM(
        "NetDemo",
        netDemoSystem,
        100
    ));
    app.world().sortSystems();

    // Run — blocks until shutdown.
    const int32_t result = app.run();

    ffe::audio::shutdown();
    return result;
}
