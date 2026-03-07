// FFE -- Generic Runtime Binary
//
// A minimal Lua game runner that the editor exports as the game binary.
// It initialises the engine, loads a Lua script (main.lua in CWD or a path
// passed as argv[1]), and enters the main loop. All game logic lives in Lua.
//
// Usage:
//   ./ffe_runtime                 # loads ./main.lua
//   ./ffe_runtime path/to/game.lua

#include "core/application.h"
#include "core/ecs.h"
#include "core/logging.h"
#include "audio/audio.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "scripting/script_engine.h"

#include <cstdint>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// Context stored in the ECS registry so the system can reach shared state.
// ---------------------------------------------------------------------------
struct RuntimeContext {
    ffe::ScriptEngine* scripts     = nullptr;
    bool               startupDone = false;
};

// ---------------------------------------------------------------------------
// Path helpers — extract directory and filename from a full path.
// ---------------------------------------------------------------------------

// Write the directory portion of `path` into `buf` (up to bufSize).
// Returns false if the path has no directory separator (i.e. bare filename).
bool extractDirectory(const char* path, char* buf, size_t bufSize) {
    const char* lastSlash = nullptr;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') { lastSlash = p; }
    }
    if (lastSlash == nullptr) { return false; }
    const size_t dirLen = static_cast<size_t>(lastSlash - path);
    if (dirLen + 1 > bufSize) { return false; }
    std::memcpy(buf, path, dirLen);
    buf[dirLen] = '\0';
    return true;
}

// Return a pointer to the filename portion of `path` (after the last slash).
const char* extractFilename(const char* path) {
    const char* name = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') { name = p + 1; }
    }
    return name;
}

// Globals set from argv before the main loop.
static char        s_scriptRoot[512] = {};
static const char* s_scriptFile      = "main.lua";

// ---------------------------------------------------------------------------
// runtimeSystem — registered at priority 100 (gameplay)
// ---------------------------------------------------------------------------
void runtimeSystem(ffe::World& world, const float dt)
{
    auto* ctx = world.registry().ctx().find<RuntimeContext>();
    if (ctx == nullptr) { return; }

    // -----------------------------------------------------------------------
    // One-time: set up the script engine and load the game script.
    // Done on the first tick so the renderer/RHI is initialised.
    // -----------------------------------------------------------------------
    if (!ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised())
    {
        ctx->scripts->setWorld(&world);
        ctx->scripts->setScriptRoot(s_scriptRoot);

        const bool ok = ctx->scripts->doFile(s_scriptFile, s_scriptRoot);
        if (!ok) {
            FFE_LOG_ERROR("Runtime", "Failed to load script: %s", s_scriptFile);
        } else {
            FFE_LOG_INFO("Runtime", "Script loaded: %s", s_scriptFile);
        }

        ctx->startupDone = true;
    }

    // -----------------------------------------------------------------------
    // Per-frame: call the Lua update() function if it exists.
    // -----------------------------------------------------------------------
    if (ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised())
    {
        // Pass entity 0 as a placeholder — the script manages its own entities.
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
int main(const int argc, const char* argv[])
{
    // Determine script path: argv[1] or "main.lua" in CWD.
    const char* scriptPath = (argc >= 2) ? argv[1] : "main.lua";

    // Extract directory and filename from the script path.
    if (!extractDirectory(scriptPath, s_scriptRoot, sizeof(s_scriptRoot))) {
        // No directory — use current working directory.
        s_scriptRoot[0] = '.';
        s_scriptRoot[1] = '\0';
    }
    s_scriptFile = extractFilename(scriptPath);

    FFE_LOG_INFO("Runtime", "Script root: %s", s_scriptRoot);
    FFE_LOG_INFO("Runtime", "Script file: %s", s_scriptFile);

    ffe::ApplicationConfig config;
    config.windowTitle  = "FFE Runtime";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    // Initialise audio.
    if (!ffe::audio::init(false)) {
        FFE_LOG_WARN("Runtime", "Audio init failed — audio disabled");
    }

    // Inject runtime context into ECS.
    RuntimeContext runtimeCtx;
    app.world().registry().ctx().emplace<RuntimeContext>(runtimeCtx);

    // Initialise scripting engine.
    static ffe::ScriptEngine scriptEngine;
    if (!scriptEngine.init()) {
        FFE_LOG_ERROR("Runtime", "ScriptEngine init failed");
    } else {
        app.world().registry().ctx().get<RuntimeContext>().scripts = &scriptEngine;
    }

    // Register game system.
    app.world().registerSystem(FFE_SYSTEM(
        "Runtime",
        runtimeSystem,
        100
    ));
    app.world().sortSystems();

    // Run — blocks until shutdown.
    const int32_t result = app.run();

    ffe::audio::shutdown();
    return result;
}
