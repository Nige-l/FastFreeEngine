// FFE -- Lua Demo
//
// Demonstrates game logic driven from a Lua script.
// The C++ host is minimal: it sets up the engine, creates one entity,
// loads game.lua, and registers a system that calls the Lua update()
// function each tick.
//
// FRICTION NOTE (documented in usage report):
//   ScriptEngine does not expose getLuaState() or a callFunction() helper.
//   The Lua state is stored as void* intentionally to prevent raw C API access
//   from outside the scripting subsystem. As a consequence, per-frame Lua
//   function calls must go through doString(), which reconstructs a short call
//   string each tick. The function body (update) is compiled once at load time;
//   only the call site string is rebuilt per frame. This is suboptimal --
//   a dedicated ScriptEngine::callFunction(name, args...) API is needed.
//
// Coordinate system: centered origin, x: -640..640, y: -360..360.
// Window: 1280x720.

#include "core/application.h"
#include "core/ecs.h"
#include "core/input.h"
#include "core/logging.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/texture_loader.h"
#include "scripting/script_engine.h"

#include <cstdio>    // snprintf
#include <cstdint>

// ---------------------------------------------------------------------------
// Demo constants
// ---------------------------------------------------------------------------
namespace {

static constexpr float PLAYER_SIZE = 48.0f;

// ---------------------------------------------------------------------------
// Context stored in the ECS registry so the system can reach shared state
// without global mutable objects.
// ---------------------------------------------------------------------------
struct LuaDemoContext {
    ffe::EntityId          player       = ffe::NULL_ENTITY;
    ffe::ScriptEngine*     scripts      = nullptr;
    bool                   sceneReady   = false;
    bool                   startupDone  = false;
};

// ---------------------------------------------------------------------------
// Helper: create a solid-colour 4x4 RGBA8 texture (fallback when PNG fails).
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
// Static storage for GPU handles -- created once, destroyed after run().
// ---------------------------------------------------------------------------
static ffe::rhi::TextureHandle s_playerTex = {0};
static bool                    s_texOwned  = false; // true if we created fallback

// ---------------------------------------------------------------------------
// luaDemoSystem -- registered at priority 100 (gameplay)
// ---------------------------------------------------------------------------
void luaDemoSystem(ffe::World& world, const float dt)
{
    auto* ctx = world.registry().ctx().find<LuaDemoContext>();
    if (ctx == nullptr) { return; }

    // -----------------------------------------------------------------------
    // One-time scene setup: create player entity, load texture
    // -----------------------------------------------------------------------
    if (!ctx->sceneReady) {
        // Attempt to load checkerboard.png.
        // setAssetRoot is write-once; it may already be set in test environments.
        // We use the two-argument loadTexture overload so we can pass assetRoot
        // directly without depending on a prior setAssetRoot() call.
        static constexpr const char* ASSET_ROOT =
            "/home/nigel/FastFreeEngine/assets/textures";

        ffe::rhi::TextureHandle loaded =
            ffe::renderer::loadTexture("checkerboard.png", ASSET_ROOT);

        if (ffe::rhi::isValid(loaded)) {
            s_playerTex = loaded;
            s_texOwned  = true; // we own this handle, must unload it
            FFE_LOG_INFO("LuaDemo", "checkerboard.png loaded successfully");
        } else {
            // Fallback: bright magenta solid colour so the failure is obvious.
            FFE_LOG_ERROR("LuaDemo",
                "Failed to load checkerboard.png -- using solid-colour fallback");
            s_playerTex = makeSolidTexture(0xFF, 0x00, 0xFF, 0xFF);
            s_texOwned  = true;
        }

        // Create the player entity in C++ -- Lua cannot create entities yet.
        ctx->player = world.createEntity();
        auto& tf    = world.addComponent<ffe::Transform>(ctx->player);
        tf.position = {0.0f, 0.0f, 0.0f};

        auto& sp  = world.addComponent<ffe::Sprite>(ctx->player);
        sp.texture = s_playerTex;
        sp.size    = {PLAYER_SIZE, PLAYER_SIZE};
        sp.color   = {1.0f, 1.0f, 1.0f, 1.0f};
        sp.layer   = 5;

        ctx->sceneReady = true;
        FFE_LOG_INFO("LuaDemo", "Player entity created (id=%u)", ctx->player);
    }

    // -----------------------------------------------------------------------
    // One-time: register the World with ScriptEngine and load game.lua.
    // Done here (not in main) so that the renderer/RHI is initialised first.
    // -----------------------------------------------------------------------
    if (!ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised() && ctx->player != ffe::NULL_ENTITY)
    {
        ctx->scripts->setWorld(&world);

        // game.lua lives next to this binary's source. We load it from the
        // source tree using the known absolute path. In a shipped game this
        // would be relative to the executable or a content root.
        static constexpr const char* SCRIPT_ROOT =
            "/home/nigel/FastFreeEngine/examples/lua_demo";

        const bool ok = ctx->scripts->doFile("game.lua", SCRIPT_ROOT);
        if (!ok) {
            FFE_LOG_ERROR("LuaDemo", "game.lua failed to load -- Lua movement disabled");
        } else {
            FFE_LOG_INFO("LuaDemo", "game.lua loaded; Lua movement active");
        }

        ctx->startupDone = true;
    }

    // -----------------------------------------------------------------------
    // Per-frame: call the Lua update() function.
    //
    // FRICTION: ScriptEngine has no callFunction() helper and getLuaState()
    // is not exposed (void* is intentional -- see header comment). We use
    // doString() with a pre-formatted call string. The function body is
    // compiled once (by doFile above); only this 40-byte call string is
    // re-evaluated per tick. The per-tick cost is:
    //   - luaL_loadstring on ~40 chars (trivial parse of a function call)
    //   - lua_pcall to invoke the already-compiled update() closure
    // This is measurably worse than a direct lua_getglobal + lua_pcall.
    // A ScriptEngine::callFunction(name, ...) API would eliminate this cost.
    // -----------------------------------------------------------------------
    if (ctx->startupDone && ctx->player != ffe::NULL_ENTITY &&
        ctx->scripts != nullptr && ctx->scripts->isInitialised())
    {
        // Build: "update(PLAYERID, DT)"
        // The entity ID is a u32; dt is a float. Both fit easily in 64 chars.
        char callBuf[64];
        const int written = std::snprintf(
            callBuf, sizeof(callBuf),
            "update(%u, %.6f)", ctx->player, static_cast<double>(dt));

        if (written > 0 && written < static_cast<int>(sizeof(callBuf))) {
            ctx->scripts->doString(callBuf);
        }
    }

    // -----------------------------------------------------------------------
    // ESC to quit -- ShutdownSignal in ECS context.
    // ESC is not accessible from Lua in the current API: the ShutdownSignal
    // lives in the ECS registry context with no Lua binding.
    // The recommended solution is ffe.requestShutdown() binding.
    // -----------------------------------------------------------------------
    if (ffe::isKeyPressed(ffe::Key::ESCAPE)) {
        FFE_LOG_INFO("LuaDemo", "ESC pressed -- requesting shutdown");
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
    config.windowTitle  = "FFE - Lua Demo (WASD to move, ESC to quit)";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    // -----------------------------------------------------------------------
    // Inject context into the ECS registry
    // -----------------------------------------------------------------------
    LuaDemoContext demoCtx;
    app.world().registry().ctx().emplace<LuaDemoContext>(demoCtx);

    // -----------------------------------------------------------------------
    // Initialise the scripting engine.
    // ScriptEngine must outlive app.run() -- declared static for simplicity.
    // In a larger game, it would be a member of the game state object.
    // setWorld() is called inside luaDemoSystem on the first tick, after
    // the renderer is live and the player entity exists.
    // -----------------------------------------------------------------------
    static ffe::ScriptEngine scriptEngine;
    if (!scriptEngine.init()) {
        FFE_LOG_ERROR("LuaDemo", "ScriptEngine init failed -- Lua features disabled");
    } else {
        // Store pointer in context so the system can reach it
        app.world().registry().ctx().get<LuaDemoContext>().scripts = &scriptEngine;
    }

    // -----------------------------------------------------------------------
    // Register game system
    // -----------------------------------------------------------------------
    app.world().registerSystem(FFE_SYSTEM(
        "LuaDemo",
        luaDemoSystem,
        100  // Gameplay priority (runs before render prepare at 500)
    ));
    app.world().sortSystems();

    // -----------------------------------------------------------------------
    // Run -- blocks until shutdown
    // -----------------------------------------------------------------------
    const int32_t result = app.run();

    // -----------------------------------------------------------------------
    // Cleanup GPU resources after run() returns
    // -----------------------------------------------------------------------
    if (s_texOwned && ffe::rhi::isValid(s_playerTex)) {
        ffe::renderer::unloadTexture(s_playerTex);
    }

    // ScriptEngine destructor calls shutdown() automatically.
    return result;
}
