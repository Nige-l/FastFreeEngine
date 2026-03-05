// Lua C API headers ONLY in this translation unit.
// Never include lua.h from engine headers.
// sol2 is not used in this file — include the raw Lua C API directly.
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "scripting/script_engine.h"
#include "core/logging.h"

#include <climits>    // PATH_MAX
#include <cstring>    // strnlen, strstr, strlen, memcpy
#include <cstdlib>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Returns true if the path is safe to open:
//   - non-null and non-empty
//   - not an absolute path (no leading '/' or '\', no drive letter)
//   - contains no '..' traversal components
bool isPathSafe(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    // Absolute paths
    if (path[0] == '/' || path[0] == '\\') {
        return false;
    }
    // Windows drive letter (e.g. C:)
    const std::size_t len = strnlen(path, PATH_MAX);
    if (len >= 2 && path[1] == ':') {
        return false;  // drive letter
    }
    // Directory traversal sequences
    if (std::strstr(path, "../") != nullptr) { return false; }
    if (std::strstr(path, "..\\") != nullptr) { return false; }
    if (std::strstr(path, "/..") != nullptr)  { return false; }
    if (std::strstr(path, "\\..") != nullptr) { return false; }
    // Bare ".." at end of string
    if (len >= 2 && path[len - 2] == '.' && path[len - 1] == '.') {
        return false;
    }
    return true;
}

// Instruction count hook — kills scripts that exceed the budget.
// This is registered via lua_sethook and runs in C, so it can call luaL_error.
void instructionHook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "script exceeded instruction budget (possible infinite loop)");
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ffe::ScriptEngine
// ---------------------------------------------------------------------------

namespace ffe {

ScriptEngine::ScriptEngine() = default;

ScriptEngine::~ScriptEngine() {
    shutdown();
}

bool ScriptEngine::init() {
    if (m_initialised) {
        FFE_LOG_WARN("ScriptEngine", "init() called on already-initialised ScriptEngine");
        return true;
    }

    // Step 1: Create the Lua state.
    // lua_open / luaL_newstate allocates from the C heap.
    // This is acceptable — the state is created once at startup, not per-frame.
    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        FFE_LOG_ERROR("ScriptEngine", "luaL_newstate() failed — out of memory");
        return false;
    }

    // Step 2: Set the instruction budget hook BEFORE opening any libraries.
    // 1,000,000 instructions per yield point — protects against infinite loops.
    // Active in ALL builds (not just debug) — this was a CRITICAL ADR-004 finding.
    lua_sethook(L, instructionHook, LUA_MASKCOUNT, 1'000'000);

    m_luaState = static_cast<void*>(L);

    // Step 3: Open ONLY safe standard library subsets.
    // Do NOT open: io, os, package, debug, ffi, jit.
    // Use luaopen_* directly to avoid opening everything and removing later.
    lua_pushcfunction(L, luaopen_base);
    lua_call(L, 0, 0);

    lua_pushcfunction(L, luaopen_math);
    lua_pushliteral(L, LUA_MATHLIBNAME);
    lua_call(L, 1, 0);

    lua_pushcfunction(L, luaopen_string);
    lua_pushliteral(L, LUA_STRLIBNAME);
    lua_call(L, 1, 0);

    lua_pushcfunction(L, luaopen_table);
    lua_pushliteral(L, LUA_TABLIBNAME);
    lua_call(L, 1, 0);

    // Step 4: Set up the sandbox (remove dangerous globals, install ffe.* API).
    setupSandbox();

    // Step 5: Register ffe.* bindings.
    registerEcsBindings();

    m_initialised = true;
    FFE_LOG_INFO("ScriptEngine", "Lua scripting initialised (budget: 1,000,000 instructions/call)");
    return true;
}

void ScriptEngine::shutdown() {
    if (m_luaState == nullptr) {
        return;
    }

    lua_State* L = static_cast<lua_State*>(m_luaState);
    lua_close(L);
    m_luaState   = nullptr;
    m_initialised = false;
    FFE_LOG_INFO("ScriptEngine", "Lua scripting shut down");
}

bool ScriptEngine::doString(const char* script) {
    if (!m_initialised || m_luaState == nullptr) {
        FFE_LOG_ERROR("ScriptEngine", "doString() called before init()");
        return false;
    }
    if (script == nullptr) {
        FFE_LOG_ERROR("ScriptEngine", "doString() called with null script");
        return false;
    }

    lua_State* L = static_cast<lua_State*>(m_luaState);

    // luaL_loadstring compiles the chunk; lua_pcall executes it.
    // lua_pcall catches all Lua errors — this cannot crash the engine.
    const int loadResult = luaL_loadstring(L, script);
    if (loadResult != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        FFE_LOG_ERROR("ScriptEngine", "Lua compile error: %s", err != nullptr ? err : "(no message)");
        lua_pop(L, 1);
        return false;
    }

    const int callResult = lua_pcall(L, 0, 0, 0);
    if (callResult != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        FFE_LOG_ERROR("ScriptEngine", "Lua runtime error: %s", err != nullptr ? err : "(no message)");
        lua_pop(L, 1);
        return false;
    }

    return true;
}

bool ScriptEngine::doFile(const char* path, const char* assetRoot) {
    if (!m_initialised || m_luaState == nullptr) {
        FFE_LOG_ERROR("ScriptEngine", "doFile() called before init()");
        return false;
    }

    // Path safety check runs BEFORE any file system operation.
    if (!isPathSafe(path)) {
        FFE_LOG_ERROR("ScriptEngine", "doFile() rejected unsafe path: %s",
                      path != nullptr ? path : "(null)");
        return false;
    }

    if (assetRoot == nullptr || assetRoot[0] == '\0') {
        FFE_LOG_ERROR("ScriptEngine", "doFile() requires a non-empty assetRoot");
        return false;
    }

    // Construct the full path: assetRoot + '/' + path
    // Use a fixed-size stack buffer to avoid heap allocation on the hot path.
    // 512 bytes is sufficient for any sane asset path.
    static constexpr std::size_t PATH_BUF_SIZE = 512u;
    char fullPath[PATH_BUF_SIZE];

    const std::size_t rootLen = std::strlen(assetRoot);
    const std::size_t fileLen = std::strlen(path);

    // +2 for separator and null terminator
    if (rootLen + fileLen + 2u > PATH_BUF_SIZE) {
        FFE_LOG_ERROR("ScriptEngine", "doFile() path too long: %s/%s", assetRoot, path);
        return false;
    }

    std::size_t pos = 0u;
    std::memcpy(fullPath + pos, assetRoot, rootLen);
    pos += rootLen;
    fullPath[pos++] = '/';
    std::memcpy(fullPath + pos, path, fileLen);
    pos += fileLen;
    fullPath[pos] = '\0';

    lua_State* L = static_cast<lua_State*>(m_luaState);

    const int loadResult = luaL_loadfile(L, fullPath);
    if (loadResult != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        FFE_LOG_ERROR("ScriptEngine", "Lua load error (%s): %s",
                      path, err != nullptr ? err : "(no message)");
        lua_pop(L, 1);
        return false;
    }

    const int callResult = lua_pcall(L, 0, 0, 0);
    if (callResult != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        FFE_LOG_ERROR("ScriptEngine", "Lua runtime error (%s): %s",
                      path, err != nullptr ? err : "(no message)");
        lua_pop(L, 1);
        return false;
    }

    return true;
}

bool ScriptEngine::isInitialised() const {
    return m_initialised;
}

// ---------------------------------------------------------------------------
// Private: sandbox setup
// ---------------------------------------------------------------------------

void ScriptEngine::setupSandbox() {
    lua_State* L = static_cast<lua_State*>(m_luaState);

    // Remove dangerous globals that were brought in by luaopen_base.
    // Set each to nil so scripts cannot access them.
    //
    // Critical removals (ADR-004 Revision 1 — CRITICAL and HIGH findings):
    //   - dofile, loadfile: direct file system access
    //   - load:             can load bytecode with arbitrary upvalues (CRITICAL finding)
    //   - require:          package system access
    //   - collectgarbage:   DoS vector via GC pressure
    //   - rawset/rawget:    can bypass __newindex/__index sandbox guards

    static const char* const BLOCKED_GLOBALS[] = {
        "dofile",
        "loadfile",
        "load",          // CRITICAL: bytecode loading with upvalues
        "loadstring",    // LuaJIT/Lua 5.1 alias for load — code execution vector
        "require",
        "collectgarbage",
        "rawset",        // HIGH: bypasses __newindex metamethod
        "rawget",        // HIGH: bypasses __index metamethod
        "rawequal",
        "rawlen",
        "setmetatable",  // HIGH: sandbox escape via metamethods
        "getmetatable",
        "debug",         // should not be present, but ensure removal
        nullptr
    };

    for (const char* const* g = BLOCKED_GLOBALS; *g != nullptr; ++g) {
        lua_pushnil(L);
        lua_setglobal(L, *g);
    }
}

// ---------------------------------------------------------------------------
// Private: ECS and ffe.* bindings
// ---------------------------------------------------------------------------

void ScriptEngine::registerEcsBindings() {
    lua_State* L = static_cast<lua_State*>(m_luaState);

    // Create the 'ffe' table and populate it.
    lua_newtable(L);

    // ffe.log(message) — safe logging from Lua scripts.
    // Messages go through the FFE logging system; scripts cannot access stderr/stdout.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const char* msg = lua_tostring(state, 1);
        FFE_LOG_INFO("Lua", "%s", msg != nullptr ? msg : "(nil)");
        return 0; // no return values
    });
    lua_setfield(L, -2, "log");

    // Set the 'ffe' table as a global.
    lua_setglobal(L, "ffe");
}

} // namespace ffe
