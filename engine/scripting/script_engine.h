#pragma once

#include "core/types.h"

// sol2 and lua headers are NOT included here.
// They are heavyweight — include only in script_engine.cpp.
// This header is safe to include from any engine module.

namespace ffe {

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

    // Execute a Lua script from a null-terminated string.
    // Returns false and logs the error if the script fails.
    // Errors are caught — this call does not crash the engine.
    bool doString(const char* script);

    // Execute a Lua script from a file.
    // path must be relative (no leading '/', no '../' components).
    // assetRoot is the absolute directory that all scripts must reside within.
    // Returns false and logs the error on failure.
    bool doFile(const char* path, const char* assetRoot);

    // Returns true if init() has been called successfully.
    bool isInitialised() const;

private:
    // Stored as void* to avoid exposing lua_State in this header.
    // Cast to lua_State* in the .cpp file where lua.h is included.
    void* m_luaState = nullptr;
    bool  m_initialised = false;

    // Sets up the whitelist sandbox: removes dangerous globals and
    // installs the ffe.* API table.
    void setupSandbox();

    // Registers ffe.* Lua bindings (ffe.log, etc.).
    void registerEcsBindings();
};

} // namespace ffe
