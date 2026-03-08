// FFE -- Showcase Demo: "Echoes of the Ancients"
//
// Standalone executable for the showcase game.  All game logic lives in Lua.
// The host creates the engine, wires up the ScriptEngine, sets the asset root
// to the showcase's own assets/ directory (next to the executable), and loads
// game.lua from the showcase script directory.
//
// Pattern: identical to examples/3d_demo/main.cpp.
//
// Controls: see game.lua — WASD/arrows, ENTER, ESC.
// Window: 1280 x 720.

#include "core/application.h"
#include "core/ecs.h"
#include "core/logging.h"
#include "renderer/texture_loader.h"
#include "audio/audio.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "scripting/script_engine.h"
#include "../demo_paths.h"

namespace {

// ---------------------------------------------------------------------------
// Context stored in the ECS registry so the system can reach shared state.
// ---------------------------------------------------------------------------
struct ShowcaseContext {
    ffe::EntityId      hostEntity   = ffe::NULL_ENTITY;
    ffe::ScriptEngine* scripts      = nullptr;
    bool               sceneReady   = false;
    bool               startupDone  = false;
};

// ---------------------------------------------------------------------------
// Resolve the showcase asset root: <exeDir>/assets
// The CMake target copies showcase assets next to the executable.
// ---------------------------------------------------------------------------
bool showcaseAssetRoot(char* buf, const size_t bufSize) {
    char exePath[512];

#ifdef _WIN32
    const DWORD len = GetModuleFileNameA(nullptr, exePath, static_cast<DWORD>(sizeof(exePath) - 1));
    if (len == 0 || len >= sizeof(exePath) - 1) { return false; }
    exePath[len] = '\0';
    for (DWORD i = 0; i < len; ++i) {
        if (exePath[i] == '\\') { exePath[i] = '/'; }
    }
#elif defined(__APPLE__)
    char rawPath[512];
    uint32_t rawSize = static_cast<uint32_t>(sizeof(rawPath));
    if (_NSGetExecutablePath(rawPath, &rawSize) != 0) { return false; }
    char* resolved = ::realpath(rawPath, nullptr);
    if (!resolved) { return false; }
    const size_t rlen = ::strlen(resolved);
    if (rlen >= sizeof(exePath) - 1) { ::free(resolved); return false; }
    ::memcpy(exePath, resolved, rlen + 1);
    ::free(resolved);
#else
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0) { return false; }
    exePath[len] = '\0';
#endif

    // Strip the executable filename to get the directory.
    char* lastSlash = nullptr;
    for (char* p = exePath; *p != '\0'; ++p) {
        if (*p == '/') { lastSlash = p; }
    }
    if (lastSlash == nullptr) { return false; }
    *lastSlash = '\0';

    snprintf(buf, bufSize, "%s/assets", exePath);
    return true;
}

// ---------------------------------------------------------------------------
// showcaseSystem -- registered at priority 100 (gameplay)
// ---------------------------------------------------------------------------
void showcaseSystem(ffe::World& world, const float dt)
{
    auto* ctx = world.registry().ctx().find<ShowcaseContext>();
    if (ctx == nullptr) { return; }

    // -------------------------------------------------------------------
    // One-time scene setup: set the asset root to the showcase's own
    // assets/ directory (copied next to the executable by CMake).
    // -------------------------------------------------------------------
    if (!ctx->sceneReady) {
        static char assetRootBuf[512];
        if (!showcaseAssetRoot(assetRootBuf, sizeof(assetRootBuf))) {
            // Fallback: try the global project asset root.
            if (!demoAssetRoot(assetRootBuf, sizeof(assetRootBuf))) {
                FFE_LOG_ERROR("Showcase", "Failed to resolve asset root");
                return;
            }
        }
        ffe::renderer::setAssetRoot(assetRootBuf);

        // Create a minimal host entity (Lua manages all visible geometry).
        ctx->hostEntity = world.createEntity();
        auto& tf        = world.addComponent<ffe::Transform>(ctx->hostEntity);
        tf.position     = {0.0f, 0.0f, 0.0f};

        ctx->sceneReady = true;
        FFE_LOG_INFO("Showcase", "Host entity created (id=%u), asset root: %s",
                     ctx->hostEntity, assetRootBuf);
    }

    // -------------------------------------------------------------------
    // One-time: register the World with ScriptEngine and load game.lua.
    // -------------------------------------------------------------------
    if (!ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised() && ctx->hostEntity != ffe::NULL_ENTITY)
    {
        ctx->scripts->setWorld(&world);

        // Script root is the exe directory — CMake copies all Lua files there.
        // We cannot use demoScriptRoot() because demoProjectRoot() finds
        // the build-dir assets/ folder and builds a wrong nested path.
        static char scriptRootBuf[512];
        if (!showcaseAssetRoot(scriptRootBuf, sizeof(scriptRootBuf))) {
            FFE_LOG_ERROR("Showcase", "Failed to resolve script root");
            return;
        }
        // showcaseAssetRoot gives "<exeDir>/assets" — strip "/assets" to get exeDir.
        char* lastSlash = nullptr;
        for (char* p = scriptRootBuf; *p != '\0'; ++p) {
            if (*p == '/') { lastSlash = p; }
        }
        if (lastSlash != nullptr) { *lastSlash = '\0'; }
        ctx->scripts->setScriptRoot(scriptRootBuf);

        // Set save root to the exe directory (saves go next to the binary).
        static char saveRootBuf[512];
        snprintf(saveRootBuf, sizeof(saveRootBuf), "%s", scriptRootBuf);
        ctx->scripts->setSaveRoot(saveRootBuf);

        const bool ok = ctx->scripts->doFile("game.lua", scriptRootBuf);
        if (!ok) {
            FFE_LOG_ERROR("Showcase", "game.lua failed to load");
        } else {
            FFE_LOG_INFO("Showcase", "game.lua loaded; showcase active");
        }

        ctx->startupDone = true;
    }

    // -------------------------------------------------------------------
    // Per-frame: call the Lua update(entityId, dt) function.
    // -------------------------------------------------------------------
    if (ctx->startupDone && ctx->hostEntity != ffe::NULL_ENTITY &&
        ctx->scripts != nullptr && ctx->scripts->isInitialised())
    {
        ctx->scripts->callFunction("update",
                                   static_cast<ffe::i64>(ctx->hostEntity),
                                   static_cast<double>(dt));

        ctx->scripts->tickTimers(dt);
        ctx->scripts->deliverCollisionEvents(world);
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    ffe::ApplicationConfig config;
    config.windowTitle  = "FFE - Echoes of the Ancients";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    // Initialise audio — the showcase uses music and SFX.
    if (!ffe::audio::init(false)) {
        FFE_LOG_WARN("Showcase", "Audio init failed -- audio disabled");
    }

    // Inject context into the ECS registry.
    ShowcaseContext showcaseCtx;
    app.world().registry().ctx().emplace<ShowcaseContext>(showcaseCtx);

    // Initialise the scripting engine.
    static ffe::ScriptEngine scriptEngine;
    if (!scriptEngine.init()) {
        FFE_LOG_ERROR("Showcase", "ScriptEngine init failed");
    } else {
        scriptEngine.setCommandLineArgs(argc, argv);
        app.world().registry().ctx().get<ShowcaseContext>().scripts = &scriptEngine;
    }

    // Register game system at priority 100 (before render prepare at 500).
    app.world().registerSystem(FFE_SYSTEM(
        "Showcase",
        showcaseSystem,
        100
    ));
    app.world().sortSystems();

    // Run -- blocks until shutdown.
    const int32_t result = app.run();

    ffe::audio::shutdown();
    return result;
}
