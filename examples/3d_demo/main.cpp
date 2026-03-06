// FFE -- 3D Demo
//
// Minimal C++ host for the 3D mesh demo.  All game logic lives in game.lua.
// The host creates the engine, wires up the ScriptEngine, and registers a
// system that calls the Lua update() function each tick.
//
// The demo exercises the Phase 2 3D Mesh Rendering API:
//   ffe.loadMesh / ffe.unloadMesh
//   ffe.createEntity3D
//   ffe.setTransform3D / ffe.setMeshColor
//   ffe.set3DCamera
//   ffe.setLightDirection / ffe.setLightColor / ffe.setAmbientColor
//
// Pattern: identical to examples/lua_demo/main.cpp.
// Only the window title and the script root name differ.
//
// Coordinate system: right-handed, Y-up.
// Window: 1280 x 720.

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

// ---------------------------------------------------------------------------
// Context stored in the ECS registry so the system can reach shared state
// without global mutable objects.
// ---------------------------------------------------------------------------
namespace {

struct Demo3DContext {
    ffe::EntityId      hostEntity   = ffe::NULL_ENTITY;
    ffe::ScriptEngine* scripts      = nullptr;
    bool               sceneReady   = false;
    bool               startupDone  = false;
};

// ---------------------------------------------------------------------------
// demo3DSystem -- registered at priority 100 (gameplay)
// ---------------------------------------------------------------------------
void demo3DSystem(ffe::World& world, const float dt)
{
    auto* ctx = world.registry().ctx().find<Demo3DContext>();
    if (ctx == nullptr) { return; }

    // -----------------------------------------------------------------------
    // One-time scene setup: create a host entity, set the asset root.
    // The host entity is passed to Lua's update(entityId, dt) each tick.
    // The Lua script does not use it for 3D work -- it manages its own
    // entities via ffe.createEntity3D.  We still pass a valid ID so the
    // Lua host contract (update receives an entity ID) is satisfied.
    // -----------------------------------------------------------------------
    if (!ctx->sceneReady) {
        // Set the global asset root so that ffe.loadMesh / ffe.loadTexture
        // work from Lua.  setAssetRoot() is write-once -- safe to call here.
        static char assetRootBuf[512];
        if (!demoAssetRoot(assetRootBuf, sizeof(assetRootBuf))) {
            FFE_LOG_ERROR("3DDemo", "Failed to resolve asset root");
            return;
        }
        ffe::renderer::setAssetRoot(assetRootBuf);

        // Create a minimal host entity (no sprite -- the Lua script owns all
        // visible geometry).
        ctx->hostEntity = world.createEntity();
        auto& tf        = world.addComponent<ffe::Transform>(ctx->hostEntity);
        tf.position     = {0.0f, 0.0f, 0.0f};

        ctx->sceneReady = true;
        FFE_LOG_INFO("3DDemo", "Host entity created (id=%u)", ctx->hostEntity);
    }

    // -----------------------------------------------------------------------
    // One-time: register the World with ScriptEngine and load game.lua.
    // Deferred to the first tick so the renderer / RHI is fully initialised.
    // -----------------------------------------------------------------------
    if (!ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised() && ctx->hostEntity != ffe::NULL_ENTITY)
    {
        ctx->scripts->setWorld(&world);

        static char scriptRootBuf[512];
        if (!demoScriptRoot("3d_demo", scriptRootBuf, sizeof(scriptRootBuf))) {
            FFE_LOG_ERROR("3DDemo", "Failed to resolve script root");
            return;
        }
        const char* SCRIPT_ROOT = scriptRootBuf;
        ctx->scripts->setScriptRoot(SCRIPT_ROOT);

        // Set save root to project root (saves go in <projectRoot>/saves/).
        static char saveRootBuf[512];
        if (demoProjectRoot(saveRootBuf, sizeof(saveRootBuf))) {
            ctx->scripts->setSaveRoot(saveRootBuf);
        }

        const bool ok = ctx->scripts->doFile("game.lua", SCRIPT_ROOT);
        if (!ok) {
            FFE_LOG_ERROR("3DDemo", "game.lua failed to load -- 3D demo disabled");
        } else {
            FFE_LOG_INFO("3DDemo", "game.lua loaded; 3D demo active");
        }

        ctx->startupDone = true;
    }

    // -----------------------------------------------------------------------
    // Per-frame: call the Lua update(entityId, dt) function.
    // -----------------------------------------------------------------------
    if (ctx->startupDone && ctx->hostEntity != ffe::NULL_ENTITY &&
        ctx->scripts != nullptr && ctx->scripts->isInitialised())
    {
        ctx->scripts->callFunction("update",
                                   static_cast<ffe::i64>(ctx->hostEntity),
                                   static_cast<double>(dt));

        ctx->scripts->tickTimers(dt);
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    ffe::ApplicationConfig config;
    config.windowTitle  = "FFE 3D Demo";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    // Audio is not used by this demo but init/shutdown must still be called
    // because ScriptEngine registers audio bindings unconditionally.
    if (!ffe::audio::init(false)) {
        FFE_LOG_WARN("3DDemo", "Audio init failed -- audio disabled");
    }

    // Inject context into the ECS registry.
    Demo3DContext demoCtx;
    app.world().registry().ctx().emplace<Demo3DContext>(demoCtx);

    // Initialise the scripting engine.
    // ScriptEngine must outlive app.run() -- declared static for simplicity.
    static ffe::ScriptEngine scriptEngine;
    if (!scriptEngine.init()) {
        FFE_LOG_ERROR("3DDemo", "ScriptEngine init failed -- Lua features disabled");
    } else {
        app.world().registry().ctx().get<Demo3DContext>().scripts = &scriptEngine;
    }

    // Register game system at priority 100 (before render prepare at 500).
    app.world().registerSystem(FFE_SYSTEM(
        "3DDemo",
        demo3DSystem,
        100
    ));
    app.world().sortSystems();

    // Run -- blocks until shutdown.
    const int32_t result = app.run();

    // ScriptEngine destructor calls shutdown() automatically (calls Lua shutdown()).
    ffe::audio::shutdown();
    return result;
}
