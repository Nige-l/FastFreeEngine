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
#include "core/ecs.h"
#include "core/input.h"
#include "renderer/render_system.h"
#include "renderer/texture_loader.h"

#include <algorithm>  // std::max, std::min
#include <cinttypes>  // PRId64
#include <climits>    // PATH_MAX
#include <cmath>      // std::isfinite
#include <cstring>    // strnlen, strstr, strlen, memcpy
#include <cstdlib>

// ---------------------------------------------------------------------------
// Registry key for the World pointer
// ---------------------------------------------------------------------------

// A file-static variable whose address is used as a unique key in the Lua
// registry. Scripts never see this — it is never exposed to Lua.
// The value stored at this key is a light userdata pointing to the World.
static int s_worldRegistryKey = 0;

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
    if (len >= PATH_MAX) return false;
    // Note: embedded null bytes in 'path' are safe on POSIX — open() stops at the first '\0',
    // so any ../ sequences after an embedded null are invisible to both this check AND the OS.
    // The strnlen guard above ensures we read at most PATH_MAX bytes.
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
        FFE_LOG_ERROR("ScriptEngine", "doFile() path too long (relative path: %s)", path);
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

bool ScriptEngine::callFunction(const char* funcName, ffe::i64 entityId, double dt) {
    if (!m_initialised || !m_luaState) return false;
    auto* L = static_cast<lua_State*>(m_luaState);

    lua_getglobal(L, funcName);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        // Don't log here — function not existing is not an error (may be optional)
        return false;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(entityId));
    lua_pushnumber(L, static_cast<lua_Number>(dt));

    if (lua_pcall(L, 2, 0, 0) != 0) {
        const char* err = lua_tostring(L, -1);
        FFE_LOG_ERROR("ScriptEngine", "callFunction(%s) error: %s",
                      funcName, err != nullptr ? err : "unknown error");
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool ScriptEngine::isInitialised() const {
    return m_initialised;
}

void ScriptEngine::setWorld(ffe::World* world) {
    if (!m_initialised || m_luaState == nullptr) {
        FFE_LOG_ERROR("ScriptEngine", "setWorld() called before init()");
        return;
    }

    lua_State* L = static_cast<lua_State*>(m_luaState);

    // Store the World pointer in the Lua registry under s_worldRegistryKey.
    // Scripts never hold the pointer directly — they use integer entity IDs
    // and go through ffe.getTransform / ffe.setTransform which retrieve the
    // pointer from the registry on each call.
    lua_pushlightuserdata(L, &s_worldRegistryKey);  // key
    if (world != nullptr) {
        lua_pushlightuserdata(L, world);              // value: the World pointer
    } else {
        lua_pushnil(L);                               // value: clear it
    }
    lua_settable(L, LUA_REGISTRYINDEX);
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

    // ----------------------------------------------------------------
    // ffe.log(message) — safe logging from Lua scripts.
    // Messages go through the FFE logging system; scripts cannot access stderr/stdout.
    // ----------------------------------------------------------------
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const char* msg = lua_tostring(state, 1);
        FFE_LOG_INFO("Lua", "%s", msg != nullptr ? msg : "(nil)");
        return 0; // no return values
    });
    lua_setfield(L, -2, "log");

    // ----------------------------------------------------------------
    // Input queries — read-only, no mutation possible from scripts.
    // Key codes are integers matching GLFW_KEY_* values (see input.h Key enum).
    //
    // ffe.isKeyHeld(keyCode)     -> bool
    // ffe.isKeyPressed(keyCode)  -> bool
    // ffe.isKeyReleased(keyCode) -> bool
    // ffe.getMouseX()            -> number (f32)
    // ffe.getMouseY()            -> number (f32)
    // ----------------------------------------------------------------

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const int code = static_cast<int>(luaL_checkinteger(state, 1));
        if (code < 0 || code >= static_cast<int>(ffe::MAX_KEYS)) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const bool held = ffe::isKeyHeld(static_cast<ffe::Key>(code));
        lua_pushboolean(state, held ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isKeyHeld");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const int code = static_cast<int>(luaL_checkinteger(state, 1));
        if (code < 0 || code >= static_cast<int>(ffe::MAX_KEYS)) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const bool pressed = ffe::isKeyPressed(static_cast<ffe::Key>(code));
        lua_pushboolean(state, pressed ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isKeyPressed");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const int code = static_cast<int>(luaL_checkinteger(state, 1));
        if (code < 0 || code >= static_cast<int>(ffe::MAX_KEYS)) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const bool released = ffe::isKeyReleased(static_cast<ffe::Key>(code));
        lua_pushboolean(state, released ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isKeyReleased");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushnumber(state, static_cast<lua_Number>(ffe::mouseX()));
        return 1;
    });
    lua_setfield(L, -2, "getMouseX");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushnumber(state, static_cast<lua_Number>(ffe::mouseY()));
        return 1;
    });
    lua_setfield(L, -2, "getMouseY");

    // ----------------------------------------------------------------
    // Key code constants — match Key enum values in input.h (= GLFW_KEY_*).
    // Scripts use these to avoid magic numbers.
    // ----------------------------------------------------------------
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::W));      lua_setfield(L, -2, "KEY_W");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::A));      lua_setfield(L, -2, "KEY_A");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::S));      lua_setfield(L, -2, "KEY_S");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::D));      lua_setfield(L, -2, "KEY_D");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::SPACE));  lua_setfield(L, -2, "KEY_SPACE");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::ESCAPE)); lua_setfield(L, -2, "KEY_ESCAPE");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::ENTER));  lua_setfield(L, -2, "KEY_ENTER");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::UP));     lua_setfield(L, -2, "KEY_UP");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::DOWN));   lua_setfield(L, -2, "KEY_DOWN");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::LEFT));   lua_setfield(L, -2, "KEY_LEFT");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::RIGHT));  lua_setfield(L, -2, "KEY_RIGHT");

    // ----------------------------------------------------------------
    // ECS component access — World pointer is stored in the Lua registry.
    // Scripts pass entity IDs as integers (the underlying u32 value of EntityId).
    // Invalid IDs return nil — scripts must check for nil before use.
    //
    // ffe.getTransform(entityId) -> table {x, y, z, scaleX, scaleY, scaleZ, rotation}
    //                               or nil if entity is invalid or has no Transform.
    //
    // ffe.setTransform(entityId, x, y, rotation, scaleX, scaleY) -> nil
    //                               No-op if entity is invalid or has no Transform.
    // ----------------------------------------------------------------

    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Retrieve World pointer from the registry.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushnil(state);
            return 1; // no World registered — return nil
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // Validate entity ID — all Lua input is untrusted.
        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0) {
            lua_pushnil(state);
            return 1;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushnil(state);
            return 1;
        }
        if (!world->hasComponent<ffe::Transform>(entityId)) {
            lua_pushnil(state);
            return 1;
        }

        const ffe::Transform& t = world->getComponent<ffe::Transform>(entityId);

        // Return a table: {x, y, z, scaleX, scaleY, scaleZ, rotation}
        lua_newtable(state);
        lua_pushnumber(state, static_cast<lua_Number>(t.position.x)); lua_setfield(state, -2, "x");
        lua_pushnumber(state, static_cast<lua_Number>(t.position.y)); lua_setfield(state, -2, "y");
        lua_pushnumber(state, static_cast<lua_Number>(t.position.z)); lua_setfield(state, -2, "z");
        lua_pushnumber(state, static_cast<lua_Number>(t.scale.x));    lua_setfield(state, -2, "scaleX");
        lua_pushnumber(state, static_cast<lua_Number>(t.scale.y));    lua_setfield(state, -2, "scaleY");
        lua_pushnumber(state, static_cast<lua_Number>(t.scale.z));    lua_setfield(state, -2, "scaleZ");
        lua_pushnumber(state, static_cast<lua_Number>(t.rotation));   lua_setfield(state, -2, "rotation");
        return 1;
    });
    lua_setfield(L, -2, "getTransform");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Retrieve World pointer from the registry.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0; // no World registered — silently no-op
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // Validate entity ID — all Lua input is untrusted.
        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0) {
            return 0;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            return 0;
        }
        if (!world->hasComponent<ffe::Transform>(entityId)) {
            return 0;
        }

        // Arguments: entityId, x, y, rotation, scaleX, scaleY
        const lua_Number x        = luaL_checknumber(state, 2);
        const lua_Number y        = luaL_checknumber(state, 3);
        const lua_Number rotation = luaL_checknumber(state, 4);
        const lua_Number scaleX   = luaL_checknumber(state, 5);
        const lua_Number scaleY   = luaL_checknumber(state, 6);

        // Reject non-finite values — NaN/Infinity would silently corrupt Transform.
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(rotation) ||
            !std::isfinite(scaleX) || !std::isfinite(scaleY)) {
            FFE_LOG_ERROR("ScriptEngine", "setTransform: non-finite value rejected");
            lua_pushboolean(state, 0);
            return 1;
        }

        ffe::Transform& t    = world->getComponent<ffe::Transform>(entityId);
        t.position.x         = static_cast<ffe::f32>(x);
        t.position.y         = static_cast<ffe::f32>(y);
        t.rotation           = static_cast<ffe::f32>(rotation);
        t.scale.x            = static_cast<ffe::f32>(scaleX);
        t.scale.y            = static_cast<ffe::f32>(scaleY);
        return 0;
    });
    lua_setfield(L, -2, "setTransform");

    // ----------------------------------------------------------------
    // ffe.requestShutdown() — request engine shutdown from a Lua script.
    // Sets ShutdownSignal.requested in the ECS registry context so that
    // the application loop exits cleanly on the next tick.
    // No-op if no World is registered.
    // ----------------------------------------------------------------
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0; // no-op if no world registered
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        if (world != nullptr) {
            world->registry().ctx().get<ffe::ShutdownSignal>().requested = true;
        }
        return 0;
    });
    lua_setfield(L, -2, "requestShutdown");

    // ----------------------------------------------------------------
    // Entity lifecycle bindings
    //
    // ffe.createEntity()
    //   → integer entity ID on success, nil if ECS is out of space
    //
    // ffe.destroyEntity(entityId)
    //   → nothing; no-op for invalid or out-of-range IDs
    //
    // ffe.addTransform(entityId, x, y, rotation, scaleX, scaleY)
    //   → true on success, false on failure
    //
    // ffe.addSprite(entityId, texHandle, width, height, r, g, b, a, layer)
    //   → true on success, false on failure
    //
    // ffe.addPreviousTransform(entityId)
    //   → true on success, false on failure
    //
    // Security: all entity IDs use the full two-sided range check (H-1).
    // Overwrite guard: uses emplace_or_replace via registry() escape hatch (H-2).
    // Texture handle: rejects rawHandle <= 0 (M-1).
    // ----------------------------------------------------------------

    // Helper: two-sided entity ID range check.
    // Returns false (and pushes nothing) if the ID is in range.
    // Callers use: if (rawId < 0 || rawId > MAX_ENTITY_ID) { push nil/false; return N; }
    static constexpr lua_Integer MAX_ENTITY_ID =
        static_cast<lua_Integer>(UINT32_MAX - 1u);

    // ffe.createEntity() -> integer or nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushnil(state);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const ffe::EntityId entityId = world->createEntity();
        if (entityId == ffe::NULL_ENTITY) {
            FFE_LOG_ERROR("ScriptEngine", "createEntity: ECS out of space");
            lua_pushnil(state);
            return 1;
        }
        lua_pushinteger(state, static_cast<lua_Integer>(entityId));
        return 1;
    });
    lua_setfield(L, -2, "createEntity");

    // ffe.destroyEntity(entityId) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        // Full two-sided range check (H-1): reject negatives and values above UINT32_MAX-1.
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            return 0; // no-op for out-of-range IDs
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            return 0; // no-op for invalid IDs
        }
        world->destroyEntity(entityId);
        return 0;
    });
    lua_setfield(L, -2, "destroyEntity");

    // ffe.addTransform(entityId, x, y, rotation, scaleX, scaleY) -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        // Full two-sided range check (H-1).
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }

        const lua_Number x        = luaL_checknumber(state, 2);
        const lua_Number y        = luaL_checknumber(state, 3);
        const lua_Number rotation = luaL_checknumber(state, 4);
        const lua_Number scaleX   = luaL_checknumber(state, 5);
        const lua_Number scaleY   = luaL_checknumber(state, 6);

        // Reject non-finite values — same pattern as setTransform.
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(rotation) ||
            !std::isfinite(scaleX) || !std::isfinite(scaleY)) {
            FFE_LOG_ERROR("ScriptEngine", "addTransform: non-finite value rejected");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Overwrite guard (H-2): use emplace_or_replace to safely handle duplicate components.
        if (world->hasComponent<ffe::Transform>(entityId)) {
            FFE_LOG_WARN("ScriptEngine", "addTransform: entity already has Transform — overwriting");
        }
        ffe::Transform& t = world->registry().emplace_or_replace<ffe::Transform>(
            static_cast<entt::entity>(entityId));
        t.position.x = static_cast<ffe::f32>(x);
        t.position.y = static_cast<ffe::f32>(y);
        t.rotation   = static_cast<ffe::f32>(rotation);
        t.scale.x    = static_cast<ffe::f32>(scaleX);
        t.scale.y    = static_cast<ffe::f32>(scaleY);

        lua_pushboolean(state, 1);
        return 1;
    });
    lua_setfield(L, -2, "addTransform");

    // ffe.addSprite(entityId, texHandle, width, height, r, g, b, a, layer) -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        // Full two-sided range check (H-1).
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }

        // Texture handle validation (M-1): reject <= 0 (0 is null sentinel).
        const lua_Integer rawHandle = luaL_checkinteger(state, 2);
        if (rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(UINT32_MAX)) {
            FFE_LOG_ERROR("ScriptEngine", "addSprite: invalid texture handle %" PRId64,
                          static_cast<long long>(rawHandle));
            lua_pushboolean(state, 0);
            return 1;
        }
        const ffe::rhi::TextureHandle texHandle{static_cast<ffe::u32>(rawHandle)};

        const lua_Number width  = luaL_checknumber(state, 3);
        const lua_Number height = luaL_checknumber(state, 4);

        // Width and height must be finite and positive (SEC-E-6).
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) {
            FFE_LOG_ERROR("ScriptEngine", "addSprite: invalid width/height (must be finite and > 0)");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Colour channels: clamp to [0.0, 1.0].
        // NaN-safe: std::max/std::min have unspecified behaviour with NaN; treat
        // any non-finite value as 0.0 (safe black/transparent default).
        const lua_Number rawR = luaL_checknumber(state, 5);
        const lua_Number rawG = luaL_checknumber(state, 6);
        const lua_Number rawB = luaL_checknumber(state, 7);
        const lua_Number rawA = luaL_checknumber(state, 8);

        auto clampColor = [](lua_Number v) -> ffe::f32 {
            if (!std::isfinite(v)) { return 0.0f; }
            return static_cast<ffe::f32>(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
        };
        const ffe::f32 r = clampColor(rawR);
        const ffe::f32 g = clampColor(rawG);
        const ffe::f32 b = clampColor(rawB);
        const ffe::f32 a = clampColor(rawA);

        // Layer: clamp to [0, 15] before casting to i16 (LOW-2: static_cast<i16>
        // on an unclamped lua_Integer is UB for values outside [-32768, 32767]).
        const lua_Integer rawLayer = luaL_checkinteger(state, 9);
        if (rawLayer < 0 || rawLayer > 15) {
            FFE_LOG_WARN("ScriptEngine",
                         "addSprite: layer %" PRId64 " is outside [0, 15] — clamping",
                         static_cast<long long>(rawLayer));
        }
        const ffe::i16 layerClamped =
            static_cast<ffe::i16>(rawLayer < 0 ? 0 : (rawLayer > 15 ? 15 : rawLayer));

        // Overwrite guard (H-2): use emplace_or_replace to safely handle duplicate components.
        if (world->hasComponent<ffe::Sprite>(entityId)) {
            FFE_LOG_WARN("ScriptEngine", "addSprite: entity already has Sprite — overwriting");
        }
        ffe::Sprite& sprite = world->registry().emplace_or_replace<ffe::Sprite>(
            static_cast<entt::entity>(entityId));
        sprite.texture   = texHandle;
        sprite.size      = glm::vec2(static_cast<ffe::f32>(width), static_cast<ffe::f32>(height));
        sprite.color     = glm::vec4(r, g, b, a);
        sprite.layer     = layerClamped;
        sprite.uvMin     = glm::vec2(0.0f, 0.0f);
        sprite.uvMax     = glm::vec2(1.0f, 1.0f);
        sprite.sortOrder = 0;

        lua_pushboolean(state, 1);
        return 1;
    });
    lua_setfield(L, -2, "addSprite");

    // ffe.addPreviousTransform(entityId) -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        // Full two-sided range check (H-1).
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }

        // Overwrite guard (H-2): use emplace_or_replace.
        if (world->hasComponent<ffe::PreviousTransform>(entityId)) {
            FFE_LOG_WARN("ScriptEngine", "addPreviousTransform: entity already has PreviousTransform — overwriting");
        }

        // Initialise from Transform if it exists, otherwise zero with a warning.
        ffe::PreviousTransform pt{};
        if (world->hasComponent<ffe::Transform>(entityId)) {
            const ffe::Transform& t = world->getComponent<ffe::Transform>(entityId);
            pt.position = t.position;
            pt.scale    = t.scale;
            pt.rotation = t.rotation;
        } else {
            FFE_LOG_WARN("ScriptEngine",
                         "addPreviousTransform: entity has no Transform — initialising to zero. "
                         "Call addTransform first.");
        }

        world->registry().emplace_or_replace<ffe::PreviousTransform>(
            static_cast<entt::entity>(entityId), pt);

        lua_pushboolean(state, 1);
        return 1;
    });
    lua_setfield(L, -2, "addPreviousTransform");

    // ----------------------------------------------------------------
    // Texture lifecycle bindings — load and unload GPU textures.
    // These bindings have NO World dependency — texture loading is a
    // renderer function, not an ECS function.
    //
    // ffe.loadTexture(path)
    //   path must be a relative string (no traversal, no absolute paths).
    //   Uses the global asset root set by renderer::setAssetRoot() in C++.
    //   Returns an integer handle id on success, nil on failure.
    //   Return nil if the argument is not a string (MEDIUM-1 type guard).
    //   Scripts must NOT be able to specify an asset root (LOW-5).
    //
    // ffe.unloadTexture(handle)
    //   handle must be a positive integer in (0, UINT32_MAX] (LOW-2).
    //   Destroys the GPU texture. After this call handle.id must not be used.
    //   Returns nothing.
    // ----------------------------------------------------------------

    // ffe.loadTexture(path) -> integer or nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // MEDIUM-1: explicit type guard — do NOT use lua_tostring without checking
        // type first. lua_tostring silently coerces numbers and booleans to strings
        // (e.g. ffe.loadTexture(0) would forward "0" to C++ without this guard).
        if (lua_type(state, 1) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "loadTexture: argument must be a string");
            lua_pushnil(state);
            return 1;
        }
        const char* path = lua_tostring(state, 1);

        // LOW-5: call the single-argument overload — scripts must not control the
        // asset root. The two-argument loadTexture(path, assetRoot) overload is
        // intentionally NOT called from this binding.
        const ffe::rhi::TextureHandle handle = ffe::renderer::loadTexture(path);

        if (handle.id == 0u) {
            lua_pushnil(state);
            return 1;
        }
        lua_pushinteger(state, static_cast<lua_Integer>(handle.id));
        return 1;
    });
    lua_setfield(L, -2, "loadTexture");

    // ffe.unloadTexture(handle) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const lua_Integer rawHandle = luaL_checkinteger(state, 1);
        // LOW-2: validate handle range before calling C++.
        // 0 is the null sentinel (invalid handle); values above UINT32_MAX overflow u32.
        if (rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(UINT32_MAX)) {
            FFE_LOG_ERROR("ScriptEngine",
                          "unloadTexture: handle %" PRId64 " is out of range — no-op",
                          static_cast<long long>(rawHandle));
            return 0;
        }
        const ffe::rhi::TextureHandle handle{static_cast<ffe::u32>(rawHandle)};
        ffe::renderer::unloadTexture(handle);
        return 0;
    });
    lua_setfield(L, -2, "unloadTexture");

    // Set the 'ffe' table as a global.
    lua_setglobal(L, "ffe");
}

} // namespace ffe
