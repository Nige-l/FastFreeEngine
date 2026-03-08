#pragma once

#include "core/types.h"
#include "core/prefab_system.h"
#include "renderer/vegetation.h"
#include "renderer/water.h"

// sol2 and lua headers are NOT included here.
// They are heavyweight — include only in script_engine.cpp.
// This header is safe to include from any engine module.

namespace ffe {

// Forward declaration — World is in core/ecs.h.
// Not included here to keep this header lightweight.
class World;

// ScriptEngine manages a sandboxed Lua state for game scripting.
//
// One ScriptEngine per game context. Not thread-safe — call from main thread only.
//
// Security: The sandbox blocks os, io, package, debug, ffi, and jit libraries.
// Scripts cannot access the filesystem, network, or OS directly. An instruction
// budget of 1,000,000 per call prevents infinite loops.
//
// Tier support: LEGACY and above.
class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    // Non-copyable, non-movable.
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;
    ScriptEngine(ScriptEngine&&) = delete;
    ScriptEngine& operator=(ScriptEngine&&) = delete;

    // Initialise the Lua state with the FFE sandbox.
    // Must be called before any other method.
    // Returns false if initialisation fails.
    bool init();

    // Shut down the Lua state and free all resources.
    // Safe to call if init() was never called or failed.
    void shutdown();

    // Register a World with the scripting engine so that Lua scripts can
    // access ECS components via ffe.getTransform / ffe.setTransform.
    //
    // Must be called AFTER init() and BEFORE any scripts execute.
    // Must NOT be called while scripts are running (not thread-safe).
    // world must remain valid for the lifetime of this ScriptEngine.
    // Passing nullptr clears the stored world pointer.
    void setWorld(ffe::World* world);

    // Execute a Lua script from a null-terminated string.
    // Returns false and logs the error if the script fails.
    // Errors are caught — this call does not crash the engine.
    bool doString(const char* script);

    // Execute a Lua script from a file.
    // path must be relative (no leading '/', no '../' components).
    // assetRoot is the absolute directory that all scripts must reside within.
    // Returns false and logs the error on failure.
    bool doFile(const char* path, const char* assetRoot);

    // Call a global Lua function by name with (entityId, dt) arguments.
    // Use this instead of doString() for per-frame Lua calls.
    // Returns true if the function exists and executed without error.
    // Returns false (and logs) if the function is not found or errors.
    // Must call init() and setWorld() before use.
    // entityId maps to lua_Integer; dt maps to lua_Number (double).
    bool callFunction(const char* funcName, ffe::i64 entityId, double dt);

    // Deliver collision events to the registered Lua callback.
    // Reads CollisionEventList from the World's ECS context and calls the
    // Lua callback (set via ffe.setCollisionCallback) for each event.
    // Call this after all systems have run (after tick()), before arena reset.
    // No-op if no callback is registered or no events exist.
    void deliverCollisionEvents(World& world);

    // Tick all active timers by dt seconds. Call once per fixed tick.
    // Fires callbacks for expired timers. One-shot timers are auto-cancelled.
    void tickTimers(float dt);

    // Returns true if init() has been called successfully.
    bool isInitialised() const;

    // Maximum number of concurrent timers.
    static constexpr u32 MAX_TIMERS = 256;

    // --- Timer storage (fixed-size, no heap per timer) ---
    // Public because Lua C-function bindings access these via ScriptEngine*.
    struct Timer {
        f32 remaining = 0.0f;   // Seconds until next fire
        f32 interval  = 0.0f;   // Original interval (for repeating timers)
        i32 luaRef    = -1;     // LUA_NOREF sentinel = -1
        bool active   = false;
        bool repeating = false;
    };
    Timer m_timers[MAX_TIMERS] = {};
    u32 m_timerCount = 0;       // High-water mark for scan optimisation

    // Allocate a timer slot. Returns slot index or -1 if full.
    i32 allocTimer();

    // Store the script asset root for ffe.loadScene() binding.
    // Write-once: set once during application setup, cannot be changed from Lua.
    static constexpr u32 ASSET_ROOT_BUF_SIZE = 512u;
    char m_assetRoot[ASSET_ROOT_BUF_SIZE] = {};

    // Set the script asset root directory. Must be an absolute path.
    // Returns true on success, false if null/empty or already set.
    bool setScriptRoot(const char* absolutePath);

    // Returns the stored script root, or nullptr if not set.
    const char* scriptRoot() const;

    // Save root for ffe.saveData / ffe.loadData.
    // Write-once, same pattern as asset root.
    static constexpr u32 SAVE_ROOT_BUF_SIZE = 512u;
    char m_saveRoot[SAVE_ROOT_BUF_SIZE] = {};

public:
    // Set the save root directory for ffe.saveData / ffe.loadData.
    // Must be an absolute path. Write-once: rejects further calls once set.
    // Returns true on success, false if null/empty or already set.
    bool setSaveRoot(const char* absolutePath);

    // Returns the stored save root, or nullptr if not set.
    const char* saveRoot() const;

    // Re-entrancy depth for loadScene (security guard against recursive loading).
    u32 m_loadSceneDepth = 0;
    static constexpr u32 MAX_LOAD_SCENE_DEPTH = 4;

private:
    // Stored as void* to avoid exposing lua_State in this header.
    // Cast to lua_State* in the .cpp file where lua.h is included.
    void* m_luaState = nullptr;
    bool  m_initialised = false;

    // Prefab system — data-driven entity template loader/instantiator.
    // setAssetRoot is called lazily from ffe.loadPrefab binding using
    // the engine's m_assetRoot. No explicit init/shutdown needed.
    ffe::PrefabSystem m_prefabSystem;

    // Vegetation system — GPU-instanced billboard grass + trees.
    // init() must be called after the OpenGL context exists (deferred until
    // first addPatch/addTree call via the render coordinator). shutdown() is
    // safe to call even if init() was never called.
    ffe::renderer::VegetationSystem m_vegetationSystem;

    // WaterManager — Phase 9 M6 animated water planes with Fresnel blending.
    // shutdown() is safe to call even if init() was never called.
    ffe::renderer::WaterManager m_waterManager;

    // Sets up the whitelist sandbox: removes dangerous globals and
    // installs the ffe.* API table.
    void setupSandbox();

    // Registers ffe.* Lua bindings (ffe.log, etc.).
    void registerEcsBindings();
};

} // namespace ffe
