// Lua C API headers ONLY in this translation unit.
// Never include lua.h from engine headers.
// sol2 is not used in this file — include the raw Lua C API directly.
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "scripting/script_engine.h"
#include "core/prefab_system.h"
#include "core/logging.h"
#include "core/ecs.h"
#include "core/input.h"
#include "renderer/render_system.h"
#include "renderer/mesh_loader.h"
#include "renderer/mesh_renderer.h"
#include "renderer/skeleton.h"
#include "renderer/shadow_map.h"
#include "renderer/screenshot.h"
#include "renderer/rhi.h"
#include "renderer/texture_atlas.h"
#include "renderer/texture_loader.h"
#include "renderer/camera.h"
#include "renderer/pbr_material.h"
#include "renderer/post_process.h"
#include "renderer/ssao.h"
#include "renderer/terrain.h"
#include "renderer/water.h"
#include "renderer/vegetation.h"

#include "audio/audio.h"
#include "renderer/text_renderer.h"
#include "physics/collider2d.h"
#include "physics/physics3d.h"
#include "physics/physics3d_system.h"
#include "scene/scene_serialiser.h"
#include "networking/network_system.h"

#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>  // std::max, std::min
#include <array>
#include <string>
#include <cinttypes>  // PRId64
#include <climits>    // PATH_MAX
#include <cmath>      // std::isfinite
#include <cstdio>     // std::rename, std::snprintf
#include <cstring>    // strnlen, strstr, strlen, memcpy
#include <cstdlib>
#include <filesystem> // create_directories
#include <sys/stat.h> // stat
#include "core/platform.h"

// ---------------------------------------------------------------------------
// Registry key for the World pointer
// ---------------------------------------------------------------------------

// A file-static variable whose address is used as a unique key in the Lua
// registry. Scripts never see this — it is never exposed to Lua.
// The value stored at this key is a light userdata pointing to the World.
static int s_worldRegistryKey = 0;

// Registry key for the ScriptEngine pointer (for timer bindings).
static int s_engineRegistryKey = 0;

// Registry key for the collision callback Lua function reference.
// Stores an integer (luaL_ref result) as light userdata.
static int s_collisionCallbackKey = 0;

// Registry key for the 3D collision callback Lua function reference.
// Same pattern as s_collisionCallbackKey but for 3D physics events.
static int s_collision3DCallbackKey = 0;

// Registry keys for networking Lua callback references.
static int s_netMessageCallbackKey       = 0;
static int s_netClientConnectedKey       = 0;
static int s_netClientDisconnectedKey    = 0;
static int s_netOnConnectedKey           = 0;
static int s_netOnDisconnectedKey        = 0;
static int s_netOnServerInputKey         = 0;
static int s_netOnLobbyUpdateKey         = 0;
static int s_netOnGameStartKey           = 0;
static int s_netOnHitConfirmKey          = 0;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Returns true if the path is safe to open:
//   - non-null and non-empty
//   - not an absolute path (no leading '/' or '\', no drive letter)
//   - contains no '..' traversal components
//   - not a UNC path (\\server\share or \\.\device)
//   - contains no ':' (blocks Windows Alternate Data Streams)
bool isPathSafe(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    // Absolute paths — also catches the first character of UNC paths (\\...)
    // Explicit UNC path guard: \\server\share and \\.\device paths begin with
    // '\\', which is caught by the path[0]=='\\' absolute-path check below.
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
    // Reject Windows Alternate Data Streams (e.g. "scripts/game.lua:stream").
    // Legitimate relative asset paths never contain ':'.
    // Drive-letter absolute paths ("C:\...") are already rejected above by the
    // path[1]==':' check, so any remaining ':' is always an ADS or device path.
    if (std::strchr(path, ':') != nullptr) { return false; }
    return true;
}

// Instruction count hook — kills scripts that exceed the budget.
// This is registered via lua_sethook and runs in C, so it can call luaL_error.
void instructionHook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "script exceeded instruction budget (possible infinite loop)");
}

// Registry key for the ScriptEngine pointer (for save/load bindings).
// We reuse s_engineRegistryKey defined below for timer/save bindings.

// ---------------------------------------------------------------------------
// Save/Load helpers
// ---------------------------------------------------------------------------

// Maximum save file size (1 MB).
static constexpr std::size_t MAX_SAVE_FILE_SIZE = 1024u * 1024u;

// Maximum JSON nesting depth during Lua table -> JSON serialization.
static constexpr int MAX_SAVE_NESTING_DEPTH = 32;

// Maximum number of .json files in the saves directory.
static constexpr int MAX_SAVE_FILE_COUNT = 128;

// Returns true if the filename consists only of [a-zA-Z0-9._-] characters,
// ends with ".json", and contains no path traversal.
bool isValidSaveFilename(const char* filename) {
    if (filename == nullptr || filename[0] == '\0') return false;

    const std::size_t len = std::strlen(filename);
    if (len > 255) return false;

    // Must end with .json
    if (len < 6) return false;  // minimum: "x.json" = 6 chars
    if (std::strcmp(filename + len - 5, ".json") != 0) return false;

    // Character allowlist and traversal check
    for (std::size_t i = 0; i < len; ++i) {
        const char c = filename[i];
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!allowed) return false;
    }

    // Reject ".." anywhere in the filename
    if (std::strstr(filename, "..") != nullptr) return false;

    // Reject slashes (already covered by allowlist, but explicit for safety)
    if (std::strchr(filename, '/') != nullptr) return false;
    if (std::strchr(filename, '\\') != nullptr) return false;

    return true;
}

// Count .json files in a directory. Returns -1 on error.
int countJsonFiles(const char* dirPath) {
    int count = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (ec) return -1;
        if (entry.is_regular_file(ec) && !ec) {
            const auto& p = entry.path();
            if (p.extension() == ".json") {
                ++count;
            }
        }
    }
    return count;
}

// Recursively convert a Lua table (at stack index tblIdx) to nlohmann::json.
// depth tracks recursion. Unsupported types are skipped with a log warning.
nlohmann::json luaTableToJson(lua_State* L, int tblIdx, int depth) {
    if (depth > MAX_SAVE_NESTING_DEPTH) {
        FFE_LOG_WARN("ScriptEngine", "saveData: max nesting depth (%d) exceeded — truncating", MAX_SAVE_NESTING_DEPTH);
        return nlohmann::json(nullptr);
    }

    // Make absolute index
    if (tblIdx < 0) tblIdx = lua_gettop(L) + tblIdx + 1;

    // Detect if the table is array-like: consecutive integer keys starting at 1.
    bool isArray = true;
    lua_Integer maxIdx = 0;
    lua_Integer count = 0;

    lua_pushnil(L);
    while (lua_next(L, tblIdx) != 0) {
        if (lua_type(L, -2) == LUA_TNUMBER) {
            const lua_Number key = lua_tonumber(L, -2);
            const auto intKey = static_cast<lua_Integer>(key);
            if (static_cast<lua_Number>(intKey) == key && intKey >= 1) {
                if (intKey > maxIdx) maxIdx = intKey;
                ++count;
            } else {
                isArray = false;
            }
        } else {
            isArray = false;
        }
        lua_pop(L, 1); // pop value, keep key
    }

    // If all keys are sequential integers 1..N, treat as array
    if (isArray && maxIdx == count && count > 0) {
        nlohmann::json arr = nlohmann::json::array();
        for (lua_Integer i = 1; i <= maxIdx; ++i) {
            lua_rawgeti(L, tblIdx, static_cast<int>(i));
            const int vtype = lua_type(L, -1);
            switch (vtype) {
                case LUA_TSTRING:
                    arr.push_back(lua_tostring(L, -1));
                    break;
                case LUA_TNUMBER:
                    arr.push_back(lua_tonumber(L, -1));
                    break;
                case LUA_TBOOLEAN:
                    arr.push_back(lua_toboolean(L, -1) != 0);
                    break;
                case LUA_TNIL:
                    arr.push_back(nullptr);
                    break;
                case LUA_TTABLE:
                    arr.push_back(luaTableToJson(L, -1, depth + 1));
                    break;
                default:
                    FFE_LOG_WARN("ScriptEngine", "saveData: skipping unsupported type %s in array",
                                 lua_typename(L, vtype));
                    arr.push_back(nullptr);
                    break;
            }
            lua_pop(L, 1);
        }
        return arr;
    }

    // Otherwise, object
    nlohmann::json obj = nlohmann::json::object();
    lua_pushnil(L);
    while (lua_next(L, tblIdx) != 0) {
        // Convert key to string
        const char* keyStr = nullptr;
        char keyBuf[64];
        if (lua_type(L, -2) == LUA_TSTRING) {
            keyStr = lua_tostring(L, -2);
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
            std::snprintf(keyBuf, sizeof(keyBuf), "%.17g", lua_tonumber(L, -2));
            keyStr = keyBuf;
        } else {
            FFE_LOG_WARN("ScriptEngine", "saveData: skipping non-string/number key");
            lua_pop(L, 1);
            continue;
        }

        const int vtype = lua_type(L, -1);
        switch (vtype) {
            case LUA_TSTRING:
                obj[keyStr] = lua_tostring(L, -1);
                break;
            case LUA_TNUMBER:
                obj[keyStr] = lua_tonumber(L, -1);
                break;
            case LUA_TBOOLEAN:
                obj[keyStr] = (lua_toboolean(L, -1) != 0);
                break;
            case LUA_TNIL:
                obj[keyStr] = nullptr;
                break;
            case LUA_TTABLE:
                obj[keyStr] = luaTableToJson(L, -1, depth + 1);
                break;
            default:
                FFE_LOG_WARN("ScriptEngine", "saveData: skipping unsupported type %s for key '%s'",
                             lua_typename(L, vtype), keyStr);
                break;
        }
        lua_pop(L, 1); // pop value, keep key
    }
    return obj;
}

// Push a nlohmann::json value onto the Lua stack as a Lua table/value.
void jsonToLuaTable(lua_State* L, const nlohmann::json& j) {
    if (j.is_null()) {
        lua_pushnil(L);
    } else if (j.is_boolean()) {
        lua_pushboolean(L, j.get<bool>() ? 1 : 0);
    } else if (j.is_number_integer()) {
        lua_pushinteger(L, static_cast<lua_Integer>(j.get<int64_t>()));
    } else if (j.is_number_float()) {
        lua_pushnumber(L, static_cast<lua_Number>(j.get<double>()));
    } else if (j.is_string()) {
        const auto& s = j.get_ref<const std::string&>();
        lua_pushlstring(L, s.c_str(), s.size());
    } else if (j.is_array()) {
        lua_createtable(L, static_cast<int>(j.size()), 0);
        int idx = 1;
        for (const auto& elem : j) {
            jsonToLuaTable(L, elem);
            lua_rawseti(L, -2, idx++);
        }
    } else if (j.is_object()) {
        lua_createtable(L, 0, static_cast<int>(j.size()));
        for (auto it = j.begin(); it != j.end(); ++it) {
            lua_pushstring(L, it.key().c_str());
            jsonToLuaTable(L, it.value());
            lua_settable(L, -3);
        }
    } else {
        lua_pushnil(L);
    }
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

    // Step 6: Store ScriptEngine pointer in Lua registry for timer bindings.
    lua_pushlightuserdata(L, &s_engineRegistryKey);
    lua_pushlightuserdata(L, this);
    lua_settable(L, LUA_REGISTRYINDEX);

    m_initialised = true;
    FFE_LOG_INFO("ScriptEngine", "Lua scripting initialised (budget: 1,000,000 instructions/call)");
    return true;
}

void ScriptEngine::shutdown() {
    if (m_luaState == nullptr) {
        return;
    }

    lua_State* L = static_cast<lua_State*>(m_luaState);

    // Call the Lua global shutdown() function if it exists.
    // This is best-effort: a Lua error must not prevent C++ shutdown from continuing.
    // The instruction budget hook is already active (set during init()), so a misbehaving
    // shutdown() function cannot loop forever.
    lua_getglobal(L, "shutdown");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            FFE_LOG_ERROR("ScriptEngine", "shutdown() Lua error: %s",
                          err != nullptr ? err : "(no message)");
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1); // pop the non-function value
    }

    // Release all timer Lua references before closing the state.
    for (u32 i = 0; i < m_timerCount; ++i) {
        if (m_timers[i].active && m_timers[i].luaRef != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, m_timers[i].luaRef);
        }
        m_timers[i] = {};
    }
    m_timerCount = 0;

    // Free vegetation GPU resources. Safe to call even if init() was never called.
    m_vegetationSystem.shutdown();

    // Free WaterManager GPU resources. Safe to call even if init() was never called.
    m_waterManager.shutdown();

    lua_close(L);
    m_luaState   = nullptr;
    m_initialised = false;
    FFE_LOG_INFO("ScriptEngine", "Lua scripting shut down");
}

i32 ScriptEngine::allocTimer() {
    // First try to reuse an inactive slot within the high-water mark.
    for (u32 i = 0; i < m_timerCount; ++i) {
        if (!m_timers[i].active) {
            return static_cast<i32>(i);
        }
    }
    // Expand if below capacity.
    if (m_timerCount < MAX_TIMERS) {
        return static_cast<i32>(m_timerCount++);
    }
    return -1;
}

void ScriptEngine::tickTimers(const float dt) {
    if (!m_initialised || m_luaState == nullptr) return;

    lua_State* L = static_cast<lua_State*>(m_luaState);

    for (u32 i = 0; i < m_timerCount; ++i) {
        Timer& t = m_timers[i];
        if (!t.active) continue;

        t.remaining -= dt;
        if (t.remaining > 0.0f) continue;

        // Timer fired — call the callback.
        lua_rawgeti(L, LUA_REGISTRYINDEX, t.luaRef);
        if (lua_pcall(L, 0, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            FFE_LOG_ERROR("ScriptEngine", "Timer callback error: %s",
                          err != nullptr ? err : "(no message)");
            lua_pop(L, 1);
            // Cancel the timer on error to prevent repeated error spam.
            luaL_unref(L, LUA_REGISTRYINDEX, t.luaRef);
            t = {};
            continue;
        }

        if (t.repeating) {
            // Reset for next interval. Accumulate overshoot so timing stays accurate.
            t.remaining += t.interval;
            // Guard against dt >> interval causing runaway catch-up.
            if (t.remaining < 0.0f) {
                t.remaining = t.interval;
            }
        } else {
            // One-shot: release the Lua reference and deactivate.
            luaL_unref(L, LUA_REGISTRYINDEX, t.luaRef);
            t = {};
        }
    }
}

bool ScriptEngine::setScriptRoot(const char* absolutePath) {
    if (absolutePath == nullptr || absolutePath[0] == '\0') {
        FFE_LOG_ERROR("ScriptEngine", "setScriptRoot: path is null or empty");
        return false;
    }
    // Write-once: reject if already set.
    if (m_assetRoot[0] != '\0') {
        FFE_LOG_WARN("ScriptEngine", "setScriptRoot: already set to '%s'", m_assetRoot);
        return false;
    }
    const std::size_t len = std::strlen(absolutePath);
    if (len >= ASSET_ROOT_BUF_SIZE) {
        FFE_LOG_ERROR("ScriptEngine", "setScriptRoot: path too long (%zu chars)", len);
        return false;
    }
    std::memcpy(m_assetRoot, absolutePath, len + 1);
    return true;
}

const char* ScriptEngine::scriptRoot() const {
    return m_assetRoot[0] != '\0' ? m_assetRoot : nullptr;
}

bool ScriptEngine::setSaveRoot(const char* absolutePath) {
    if (absolutePath == nullptr || absolutePath[0] == '\0') {
        FFE_LOG_ERROR("ScriptEngine", "setSaveRoot: path is null or empty");
        return false;
    }
    if (m_saveRoot[0] != '\0') {
        FFE_LOG_WARN("ScriptEngine", "setSaveRoot: already set to '%s'", m_saveRoot);
        return false;
    }

    // Resolve to canonical path
    char resolved[SAVE_ROOT_BUF_SIZE];
    if (!ffe::canonicalizePath(absolutePath, resolved, sizeof(resolved))) {
        FFE_LOG_ERROR("ScriptEngine", "setSaveRoot: canonicalizePath failed for '%s'", absolutePath);
        return false;
    }

    const std::size_t len = std::strlen(resolved);
    if (len >= SAVE_ROOT_BUF_SIZE) {
        FFE_LOG_ERROR("ScriptEngine", "setSaveRoot: resolved path too long (%zu chars)", len);
        return false;
    }
    std::memcpy(m_saveRoot, resolved, len + 1);
    return true;
}

const char* ScriptEngine::saveRoot() const {
    return m_saveRoot[0] != '\0' ? m_saveRoot : nullptr;
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

    // Canonicalize to resolve any symlinks or encoded traversal sequences that
    // survived the isPathSafe string check. Then verify the resolved path is
    // still under the asset root — matching the pattern used in loadTexture,
    // loadMesh, loadSound, etc.
    char canonPath[PATH_MAX + 1];
    if (!ffe::canonicalizePath(fullPath, canonPath, sizeof(canonPath))) {
        FFE_LOG_ERROR("ScriptEngine", "doFile() canonicalizePath failed for: %s", fullPath);
        return false;
    }

    // Prefix check: canonical path must begin with the asset root.
    if (std::strncmp(canonPath, assetRoot, rootLen) != 0) {
        FFE_LOG_ERROR("ScriptEngine",
                      "doFile() canonical path \"%s\" escapes asset root \"%s\"",
                      canonPath, assetRoot);
        return false;
    }
    // Separator check: ensures the match is a directory boundary, not a prefix
    // collision (e.g. root="/assets" matching "/assets_evil/foo.lua").
    if (canonPath[rootLen] != '/' && canonPath[rootLen] != '\0') {
        FFE_LOG_ERROR("ScriptEngine",
                      "doFile() canonical path \"%s\" escapes asset root \"%s\"",
                      canonPath, assetRoot);
        return false;
    }

    lua_State* L = static_cast<lua_State*>(m_luaState);

    const int loadResult = luaL_loadfile(L, canonPath);
    if (loadResult != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        FFE_LOG_ERROR("ScriptEngine", "Lua load error (%s): %s",
                      path, err != nullptr ? err : "(no message)");
        lua_pop(L, 1);
        return false;
    }

    // Reset instruction budget so each doFile gets a fresh 1M instructions.
    // Without this, chained loadScene calls could share/exhaust a single budget.
    lua_sethook(L, instructionHook, LUA_MASKCOUNT, 1'000'000);

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

void ScriptEngine::setCommandLineArgs(int argc, char** argv) {
    if (!m_initialised || m_luaState == nullptr) { return; }
    lua_State* L = static_cast<lua_State*>(m_luaState);
    lua_newtable(L);
    for (int i = 0; i < argc; ++i) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i);  // arg[0], arg[1], etc.
    }
    lua_setglobal(L, "arg");
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

void ScriptEngine::deliverCollisionEvents(World& world) {
    if (!m_initialised || m_luaState == nullptr) {
        return;
    }

    // Check if collision events exist in the ECS context.
    if (!world.registry().ctx().contains<ffe::CollisionEventList>()) {
        return;
    }
    const auto& eventList = world.registry().ctx().get<ffe::CollisionEventList>();
    if (eventList.count == 0 || eventList.events == nullptr) {
        return;
    }

    auto* L = static_cast<lua_State*>(m_luaState);

    // Retrieve the callback ref from the Lua registry.
    lua_pushlightuserdata(L, &s_collisionCallbackKey);
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return; // No callback registered.
    }
    const int ref = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);

    if (ref == -2) { // LUA_NOREF
        return;
    }

    // Call the callback for each event: callback(entityA, entityB)
    for (u32 i = 0; i < eventList.count; ++i) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            FFE_LOG_WARN("ScriptEngine", "Collision callback is not a function — unregistering");
            luaL_unref(L, LUA_REGISTRYINDEX, ref);

            // Clear the stored ref.
            lua_pushlightuserdata(L, &s_collisionCallbackKey);
            lua_pushinteger(L, -2); // LUA_NOREF
            lua_settable(L, LUA_REGISTRYINDEX);

            if (world.registry().ctx().contains<ffe::CollisionCallbackRef>()) {
                world.registry().ctx().get<ffe::CollisionCallbackRef>().luaRef = -2;
            }
            return;
        }

        lua_pushinteger(L, static_cast<lua_Integer>(eventList.events[i].entityA));
        lua_pushinteger(L, static_cast<lua_Integer>(eventList.events[i].entityB));

        if (lua_pcall(L, 2, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            FFE_LOG_ERROR("ScriptEngine", "Collision callback error: %s",
                          err != nullptr ? err : "(unknown)");
            lua_pop(L, 1);
            // Continue delivering remaining events despite the error.
        }
    }
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
    // ffe.getMouseDeltaX()       -> number (f64)
    // ffe.getMouseDeltaY()       -> number (f64)
    // ffe.setCursorCaptured(bool) -> void
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

    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushnumber(state, static_cast<lua_Number>(ffe::mouseDeltaX()));
        return 1;
    });
    lua_setfield(L, -2, "getMouseDeltaX");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushnumber(state, static_cast<lua_Number>(ffe::mouseDeltaY()));
        return 1;
    });
    lua_setfield(L, -2, "getMouseDeltaY");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const bool captured = lua_toboolean(state, 1) != 0;
        ffe::setCursorCaptured(captured);
        return 0;
    });
    lua_setfield(L, -2, "setCursorCaptured");

    // ----------------------------------------------------------------
    // Key code constants — match Key enum values in input.h (= GLFW_KEY_*).
    // Scripts use these to avoid magic numbers.
    // ----------------------------------------------------------------
    // Letters (full alphabet)
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::A)); lua_setfield(L, -2, "KEY_A");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::B)); lua_setfield(L, -2, "KEY_B");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::C)); lua_setfield(L, -2, "KEY_C");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::D)); lua_setfield(L, -2, "KEY_D");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::E)); lua_setfield(L, -2, "KEY_E");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::F)); lua_setfield(L, -2, "KEY_F");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::G)); lua_setfield(L, -2, "KEY_G");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::H)); lua_setfield(L, -2, "KEY_H");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::I)); lua_setfield(L, -2, "KEY_I");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::J)); lua_setfield(L, -2, "KEY_J");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::K)); lua_setfield(L, -2, "KEY_K");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::L)); lua_setfield(L, -2, "KEY_L");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::M)); lua_setfield(L, -2, "KEY_M");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::N)); lua_setfield(L, -2, "KEY_N");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::O)); lua_setfield(L, -2, "KEY_O");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::P)); lua_setfield(L, -2, "KEY_P");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::Q)); lua_setfield(L, -2, "KEY_Q");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::R)); lua_setfield(L, -2, "KEY_R");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::S)); lua_setfield(L, -2, "KEY_S");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::T)); lua_setfield(L, -2, "KEY_T");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::U)); lua_setfield(L, -2, "KEY_U");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::V)); lua_setfield(L, -2, "KEY_V");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::W)); lua_setfield(L, -2, "KEY_W");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::X)); lua_setfield(L, -2, "KEY_X");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::Y)); lua_setfield(L, -2, "KEY_Y");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::Z)); lua_setfield(L, -2, "KEY_Z");

    // Numbers
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_0)); lua_setfield(L, -2, "KEY_0");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_1)); lua_setfield(L, -2, "KEY_1");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_2)); lua_setfield(L, -2, "KEY_2");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_3)); lua_setfield(L, -2, "KEY_3");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_4)); lua_setfield(L, -2, "KEY_4");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_5)); lua_setfield(L, -2, "KEY_5");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_6)); lua_setfield(L, -2, "KEY_6");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_7)); lua_setfield(L, -2, "KEY_7");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_8)); lua_setfield(L, -2, "KEY_8");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::NUM_9)); lua_setfield(L, -2, "KEY_9");

    // Arrows
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::UP));    lua_setfield(L, -2, "KEY_UP");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::DOWN));  lua_setfield(L, -2, "KEY_DOWN");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::LEFT));  lua_setfield(L, -2, "KEY_LEFT");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::RIGHT)); lua_setfield(L, -2, "KEY_RIGHT");

    // Common gameplay keys
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::SPACE));      lua_setfield(L, -2, "KEY_SPACE");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::ESCAPE));     lua_setfield(L, -2, "KEY_ESCAPE");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::ENTER));      lua_setfield(L, -2, "KEY_ENTER");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::TAB));        lua_setfield(L, -2, "KEY_TAB");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::BACKSPACE));  lua_setfield(L, -2, "KEY_BACKSPACE");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::LEFT_SHIFT)); lua_setfield(L, -2, "KEY_LEFT_SHIFT");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::LEFT_CTRL));  lua_setfield(L, -2, "KEY_LEFT_CTRL");

    // Function keys
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::F1));  lua_setfield(L, -2, "KEY_F1");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::F2));  lua_setfield(L, -2, "KEY_F2");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::F3));  lua_setfield(L, -2, "KEY_F3");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::F4));  lua_setfield(L, -2, "KEY_F4");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::Key::F5));  lua_setfield(L, -2, "KEY_F5");

    // ----------------------------------------------------------------
    // Mouse button bindings — ffe.isMousePressed/Held/Released(button)
    // Button constants: ffe.MOUSE_LEFT, ffe.MOUSE_RIGHT, ffe.MOUSE_MIDDLE
    // ----------------------------------------------------------------
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto btn = static_cast<ffe::MouseButton>(luaL_checkinteger(state, 1));
        lua_pushboolean(state, ffe::isMouseButtonPressed(btn) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isMousePressed");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto btn = static_cast<ffe::MouseButton>(luaL_checkinteger(state, 1));
        lua_pushboolean(state, ffe::isMouseButtonHeld(btn) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isMouseHeld");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto btn = static_cast<ffe::MouseButton>(luaL_checkinteger(state, 1));
        lua_pushboolean(state, ffe::isMouseButtonReleased(btn) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isMouseReleased");

    // Mouse button constants
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::MouseButton::LEFT));   lua_setfield(L, -2, "MOUSE_LEFT");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::MouseButton::RIGHT));  lua_setfield(L, -2, "MOUSE_RIGHT");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::MouseButton::MIDDLE)); lua_setfield(L, -2, "MOUSE_MIDDLE");

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

    // ----------------------------------------------------------------
    // ffe.fillTransform(entityId, table) -> bool
    //   Zero-allocation alternative to ffe.getTransform.
    //   Writes x, y, rotation, scaleX, scaleY into a caller-provided table.
    //   Returns true on success, false if entity is invalid or has no Transform.
    //   The caller pre-allocates the table once and reuses it every frame,
    //   eliminating per-call GC pressure from table creation.
    // ----------------------------------------------------------------
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Retrieve World pointer from the registry.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // Validate arg 1: entity ID (integer).
        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0) {
            lua_pushboolean(state, 0);
            return 1;
        }

        // Validate arg 2: must be a table.
        luaL_checktype(state, 2, LUA_TTABLE);

        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }
        if (!world->hasComponent<ffe::Transform>(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }

        const ffe::Transform& t = world->getComponent<ffe::Transform>(entityId);

        // Fill the caller's table in-place — no new table allocation.
        lua_pushnumber(state, static_cast<lua_Number>(t.position.x)); lua_setfield(state, 2, "x");
        lua_pushnumber(state, static_cast<lua_Number>(t.position.y)); lua_setfield(state, 2, "y");
        lua_pushnumber(state, static_cast<lua_Number>(t.rotation));   lua_setfield(state, 2, "rotation");
        lua_pushnumber(state, static_cast<lua_Number>(t.scale.x));    lua_setfield(state, 2, "scaleX");
        lua_pushnumber(state, static_cast<lua_Number>(t.scale.y));    lua_setfield(state, 2, "scaleY");

        lua_pushboolean(state, 1);
        return 1;
    });
    lua_setfield(L, -2, "fillTransform");

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
    // ffe.cameraShake(intensity, duration) — trigger a camera shake effect.
    // intensity: max pixel offset (clamped to [0, 100])
    // duration: seconds (clamped to [0, 5])
    // If a shake is already active, the new one replaces it.
    // ----------------------------------------------------------------
    lua_pushcfunction(L, [](lua_State* state) -> int {
        if (lua_gettop(state) < 2) {
            luaL_error(state, "cameraShake requires 2 arguments: intensity, duration");
            return 0;
        }
        const auto intensity = static_cast<float>(luaL_checknumber(state, 1));
        const auto duration  = static_cast<float>(luaL_checknumber(state, 2));

        // Reject NaN/Inf
        if (std::isnan(intensity) || std::isinf(intensity) ||
            std::isnan(duration)  || std::isinf(duration)) {
            return 0;
        }

        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        if (world == nullptr) { return 0; }

        auto& shake = world->registry().ctx().get<ffe::CameraShake>();
        shake.intensity = std::max(0.0f, std::min(100.0f, intensity));
        shake.duration  = std::max(0.0f, std::min(5.0f, duration));
        shake.elapsed   = 0.0f;
        return 0;
    });
    lua_setfield(L, -2, "cameraShake");

    // ----------------------------------------------------------------
    // ffe.setBackgroundColor(r, g, b) — set the clear/background color.
    // Values are clamped to [0, 1]. NaN/Inf silently rejected.
    // ----------------------------------------------------------------
    lua_pushcfunction(L, [](lua_State* state) -> int {
        if (lua_gettop(state) < 3) {
            luaL_error(state, "setBackgroundColor requires 3 arguments: r, g, b");
            return 0;
        }
        const auto r = static_cast<float>(luaL_checknumber(state, 1));
        const auto g = static_cast<float>(luaL_checknumber(state, 2));
        const auto b = static_cast<float>(luaL_checknumber(state, 3));

        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) {
            return 0;
        }

        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        if (world == nullptr) { return 0; }

        auto& cc = world->registry().ctx().get<ffe::ClearColor>();
        cc.r = std::max(0.0f, std::min(1.0f, r));
        cc.g = std::max(0.0f, std::min(1.0f, g));
        cc.b = std::max(0.0f, std::min(1.0f, b));
        return 0;
    });
    lua_setfield(L, -2, "setBackgroundColor");

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

    // ffe.getEntityCount() -> integer
    // Returns the number of alive entities (total - free list).
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushinteger(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        if (world == nullptr) {
            lua_pushinteger(state, 0);
            return 1;
        }
        const auto& reg = world->registry();
        const auto* storage = reg.storage<entt::entity>();
        std::size_t count = 0;
        if (storage != nullptr) {
            count = storage->free_list();
        }
        lua_pushinteger(state, static_cast<lua_Integer>(count));
        return 1;
    });
    lua_setfield(L, -2, "getEntityCount");

    // ffe.drawText(text, x, y, scale, r, g, b, a) — queue HUD text for rendering.
    // Coordinates are screen pixels, origin top-left. scale=1 = 8px glyphs.
    // Color components [0,1]. Clamped. NaN/Inf rejected (no-op).
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0; // No world — silently ignore
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** trPtr = world->registry().ctx().find<ffe::renderer::TextRenderer*>();
        if (trPtr == nullptr || *trPtr == nullptr) return 0;

        const char* text = luaL_checkstring(state, 1);
        const lua_Number x     = luaL_checknumber(state, 2);
        const lua_Number y     = luaL_checknumber(state, 3);
        const lua_Number scale = luaL_optnumber(state, 4, 1.0);
        const lua_Number r     = luaL_optnumber(state, 5, 1.0);
        const lua_Number g     = luaL_optnumber(state, 6, 1.0);
        const lua_Number b     = luaL_optnumber(state, 7, 1.0);
        const lua_Number a     = luaL_optnumber(state, 8, 1.0);

        // Reject NaN/Inf
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(scale) ||
            !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
            return 0;
        }

        const auto clamp01 = [](lua_Number v) -> ffe::f32 {
            return static_cast<ffe::f32>(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
        };
        const ffe::f32 clampedScale = static_cast<ffe::f32>(scale < 0.1 ? 0.1 : (scale > 20.0 ? 20.0 : scale));

        ffe::renderer::drawText(**trPtr, text,
            static_cast<ffe::f32>(x), static_cast<ffe::f32>(y), clampedScale,
            clamp01(r), clamp01(g), clamp01(b), clamp01(a));
        return 0;
    });
    lua_setfield(L, -2, "drawText");

    // ffe.drawRect(x, y, width, height, r, g, b, a) — draw a filled rectangle.
    // Screen-space coordinates, origin top-left. Color clamped [0,1].
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** trPtr = world->registry().ctx().find<ffe::renderer::TextRenderer*>();
        if (trPtr == nullptr || *trPtr == nullptr) return 0;

        const lua_Number x = luaL_checknumber(state, 1);
        const lua_Number y = luaL_checknumber(state, 2);
        const lua_Number w = luaL_checknumber(state, 3);
        const lua_Number h = luaL_checknumber(state, 4);
        const lua_Number r = luaL_optnumber(state, 5, 1.0);
        const lua_Number g = luaL_optnumber(state, 6, 1.0);
        const lua_Number b = luaL_optnumber(state, 7, 1.0);
        const lua_Number a = luaL_optnumber(state, 8, 1.0);

        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) ||
            !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
            return 0;
        }

        const auto clamp01 = [](lua_Number v) -> ffe::f32 {
            return static_cast<ffe::f32>(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
        };

        ffe::renderer::drawRect(**trPtr,
            static_cast<ffe::f32>(x), static_cast<ffe::f32>(y),
            static_cast<ffe::f32>(w), static_cast<ffe::f32>(h),
            clamp01(r), clamp01(g), clamp01(b), clamp01(a));
        return 0;
    });
    lua_setfield(L, -2, "drawRect");

    // ffe.getScreenWidth() -> number
    // ffe.getScreenHeight() -> number
    // Returns the screen dimensions in pixels. Useful for centering text.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushnumber(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        auto** trPtr = world->registry().ctx().find<ffe::renderer::TextRenderer*>();
        if (trPtr == nullptr || *trPtr == nullptr) {
            lua_pushnumber(state, 0);
            return 1;
        }
        lua_pushnumber(state, static_cast<lua_Number>((*trPtr)->screenWidth));
        return 1;
    });
    lua_setfield(L, -2, "getScreenWidth");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushnumber(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        auto** trPtr = world->registry().ctx().find<ffe::renderer::TextRenderer*>();
        if (trPtr == nullptr || *trPtr == nullptr) {
            lua_pushnumber(state, 0);
            return 1;
        }
        lua_pushnumber(state, static_cast<lua_Number>((*trPtr)->screenHeight));
        return 1;
    });
    lua_setfield(L, -2, "getScreenHeight");

    // ffe.isSoftwareRenderer() -> boolean
    // Returns true if the GPU driver is a software renderer (e.g., llvmpipe,
    // swrast). Games can use this to skip advanced post-processing effects
    // that produce black or corrupted output on software renderers.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushboolean(state, ffe::rhi::isSoftwareRenderer() ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isSoftwareRenderer");

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

    // ffe.setSpriteColor(entityId, r, g, b, a) -> nothing
    // Modifies an existing Sprite component's color without log-spam overwrite.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::Sprite>(entityId)) { return 0; }

        const lua_Number r = luaL_checknumber(state, 2);
        const lua_Number g = luaL_checknumber(state, 3);
        const lua_Number b = luaL_checknumber(state, 4);
        const lua_Number a = luaL_checknumber(state, 5);

        if (!std::isfinite(r) || !std::isfinite(g) ||
            !std::isfinite(b) || !std::isfinite(a)) {
            FFE_LOG_ERROR("ScriptEngine", "setSpriteColor: non-finite value rejected");
            return 0;
        }

        ffe::Sprite& sp = world->getComponent<ffe::Sprite>(entityId);
        sp.color.r = static_cast<ffe::f32>(r);
        sp.color.g = static_cast<ffe::f32>(g);
        sp.color.b = static_cast<ffe::f32>(b);
        sp.color.a = static_cast<ffe::f32>(a);
        return 0;
    });
    lua_setfield(L, -2, "setSpriteColor");

    // ffe.setSpriteSize(entityId, width, height) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::Sprite>(entityId)) { return 0; }

        const lua_Number w = luaL_checknumber(state, 2);
        const lua_Number h = luaL_checknumber(state, 3);

        if (!std::isfinite(w) || !std::isfinite(h) || w <= 0.0 || h <= 0.0) {
            FFE_LOG_ERROR("ScriptEngine", "setSpriteSize: invalid size rejected");
            return 0;
        }

        ffe::Sprite& sp = world->getComponent<ffe::Sprite>(entityId);
        sp.size.x = static_cast<ffe::f32>(w);
        sp.size.y = static_cast<ffe::f32>(h);
        return 0;
    });
    lua_setfield(L, -2, "setSpriteSize");

    // ffe.setSpriteFlip(entityId, flipX, flipY) -> nothing
    // Sets horizontal/vertical flip flags on a Sprite component.
    // Flipping swaps UV coordinates at render time — works with animations.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::Sprite>(entityId)) { return 0; }

        const bool flipX = lua_toboolean(state, 2) != 0;
        const bool flipY = lua_toboolean(state, 3) != 0;

        ffe::Sprite& sp = world->getComponent<ffe::Sprite>(entityId);
        sp.flipX = flipX;
        sp.flipY = flipY;
        return 0;
    });
    lua_setfield(L, -2, "setSpriteFlip");

    // ffe.addTilemap(entityId, width, height, tileW, tileH, textureHandle, columns, tileCount, layer) -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushboolean(state, 0); return 1; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { lua_pushboolean(state, 0); return 1; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { lua_pushboolean(state, 0); return 1; }

        const auto w          = static_cast<ffe::u16>(luaL_checkinteger(state, 2));
        const auto h          = static_cast<ffe::u16>(luaL_checkinteger(state, 3));
        const auto tileW      = static_cast<ffe::f32>(luaL_checknumber(state, 4));
        const auto tileH      = static_cast<ffe::f32>(luaL_checknumber(state, 5));
        const auto texHandle  = static_cast<ffe::u32>(luaL_checkinteger(state, 6));
        const auto columns    = static_cast<ffe::u16>(luaL_checkinteger(state, 7));
        const auto tileCount  = static_cast<ffe::u16>(luaL_checkinteger(state, 8));
        const auto layer      = static_cast<ffe::i16>(luaL_optinteger(state, 9, 0));

        if (texHandle == 0 || w == 0 || h == 0 || columns == 0 || tileCount == 0) {
            lua_pushboolean(state, 0);
            return 1;
        }

        ffe::Tilemap tm;
        if (!ffe::initTilemap(tm, w, h)) {
            lua_pushboolean(state, 0);
            return 1;
        }
        tm.tileWidth  = tileW;
        tm.tileHeight = tileH;
        tm.texture    = ffe::rhi::TextureHandle{texHandle};
        tm.columns    = columns;
        tm.tileCount  = tileCount;
        tm.layer      = layer;

        // Destroy existing tilemap if present (prevents leak on overwrite).
        if (world->hasComponent<ffe::Tilemap>(entityId)) {
            ffe::destroyTilemap(world->getComponent<ffe::Tilemap>(entityId));
        }
        world->addComponent<ffe::Tilemap>(entityId) = tm;
        lua_pushboolean(state, 1);
        return 1;
    });
    lua_setfield(L, -2, "addTilemap");

    // ffe.setTile(entityId, x, y, tileIndex) -> nothing
    // x, y are 0-based grid coordinates. tileIndex 0 = empty.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0) return 0;
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) return 0;
        if (!world->hasComponent<ffe::Tilemap>(entityId)) return 0;

        const auto x     = static_cast<ffe::u16>(luaL_checkinteger(state, 2));
        const auto y     = static_cast<ffe::u16>(luaL_checkinteger(state, 3));
        const auto index = static_cast<ffe::u16>(luaL_checkinteger(state, 4));

        ffe::Tilemap& tm = world->getComponent<ffe::Tilemap>(entityId);
        if (x >= tm.width || y >= tm.height) return 0;

        tm.tiles[static_cast<ffe::u32>(y) * tm.width + x] = index;
        return 0;
    });
    lua_setfield(L, -2, "setTile");

    // ffe.getTile(entityId, x, y) -> tileIndex or nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushnil(state); return 1; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0) { lua_pushnil(state); return 1; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { lua_pushnil(state); return 1; }
        if (!world->hasComponent<ffe::Tilemap>(entityId)) { lua_pushnil(state); return 1; }

        const auto x = static_cast<ffe::u16>(luaL_checkinteger(state, 2));
        const auto y = static_cast<ffe::u16>(luaL_checkinteger(state, 3));

        const ffe::Tilemap& tm = world->getComponent<ffe::Tilemap>(entityId);
        if (x >= tm.width || y >= tm.height) { lua_pushnil(state); return 1; }

        lua_pushinteger(state, tm.tiles[static_cast<ffe::u32>(y) * tm.width + x]);
        return 1;
    });
    lua_setfield(L, -2, "getTile");

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
    // Sprite animation bindings — atlas-based animation control.
    //
    // ffe.addSpriteAnimation(entityId, frameCount, columns, frameTime, looping)
    //   → true on success, false on failure
    //   Validates: entity exists, frameCount > 0, columns > 0,
    //   columns <= frameCount, frameTime > 0.
    //   Sets playing = false initially (user calls playAnimation to start).
    //
    // ffe.playAnimation(entityId) → nothing
    //   Set playing = true, reset elapsed = 0.
    //
    // ffe.stopAnimation(entityId) → nothing
    //   Set playing = false.
    //
    // ffe.setAnimationFrame(entityId, frame) → nothing
    //   Clamp to [0, frameCount-1], update UVs immediately.
    //
    // ffe.isAnimationPlaying(entityId) → boolean
    //   Returns true if the animation is currently playing.
    // ----------------------------------------------------------------

    // ffe.addSpriteAnimation(entityId, frameCount, columns, frameTime, looping) -> bool
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
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }

        const lua_Integer frameCount = luaL_checkinteger(state, 2);
        const lua_Integer columns    = luaL_checkinteger(state, 3);
        const lua_Number  frameTime  = luaL_checknumber(state, 4);
        const bool        looping    = lua_toboolean(state, 5) != 0;

        // Validate parameters.
        if (frameCount <= 0 || frameCount > 65535) {
            FFE_LOG_ERROR("ScriptEngine", "addSpriteAnimation: frameCount must be in [1, 65535]");
            lua_pushboolean(state, 0);
            return 1;
        }
        if (columns <= 0 || columns > frameCount) {
            FFE_LOG_ERROR("ScriptEngine", "addSpriteAnimation: columns must be in [1, frameCount]");
            lua_pushboolean(state, 0);
            return 1;
        }
        if (!std::isfinite(frameTime) || frameTime <= 0.0) {
            FFE_LOG_ERROR("ScriptEngine", "addSpriteAnimation: frameTime must be > 0 and finite");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Overwrite guard (H-2): use emplace_or_replace.
        if (world->hasComponent<ffe::SpriteAnimation>(entityId)) {
            FFE_LOG_WARN("ScriptEngine", "addSpriteAnimation: entity already has SpriteAnimation — overwriting");
        }
        ffe::SpriteAnimation& anim = world->registry().emplace_or_replace<ffe::SpriteAnimation>(
            static_cast<entt::entity>(entityId));
        anim.frameCount   = static_cast<ffe::u16>(frameCount);
        anim.columns      = static_cast<ffe::u16>(columns);
        anim.currentFrame = 0;
        anim.frameTime    = static_cast<ffe::f32>(frameTime);
        anim.elapsed      = 0.0f;
        anim.looping      = looping;
        anim.playing      = false;

        lua_pushboolean(state, 1);
        return 1;
    });
    lua_setfield(L, -2, "addSpriteAnimation");

    // ffe.playAnimation(entityId) -> nothing
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
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::SpriteAnimation>(entityId)) { return 0; }

        ffe::SpriteAnimation& anim = world->getComponent<ffe::SpriteAnimation>(entityId);
        anim.playing = true;
        anim.elapsed = 0.0f;
        return 0;
    });
    lua_setfield(L, -2, "playAnimation");

    // ffe.stopAnimation(entityId) -> nothing
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
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::SpriteAnimation>(entityId)) { return 0; }

        world->getComponent<ffe::SpriteAnimation>(entityId).playing = false;
        return 0;
    });
    lua_setfield(L, -2, "stopAnimation");

    // ffe.setAnimationFrame(entityId, frame) -> nothing
    // Clamps frame to [0, frameCount-1] and updates UVs immediately.
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
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::SpriteAnimation>(entityId)) { return 0; }

        ffe::SpriteAnimation& anim = world->getComponent<ffe::SpriteAnimation>(entityId);

        const lua_Integer rawFrame = luaL_checkinteger(state, 2);
        // Clamp to [0, frameCount - 1].
        ffe::u16 frame = 0;
        if (rawFrame > 0) {
            frame = static_cast<ffe::u16>(
                rawFrame >= static_cast<lua_Integer>(anim.frameCount)
                    ? anim.frameCount - 1
                    : rawFrame);
        }
        anim.currentFrame = frame;

        // Update UVs immediately if the entity also has a Sprite component.
        if (world->hasComponent<ffe::Sprite>(entityId)) {
            ffe::Sprite& sprite = world->getComponent<ffe::Sprite>(entityId);
            const ffe::u16 col  = frame % anim.columns;
            const ffe::u16 row  = frame / anim.columns;
            const ffe::u16 rows = (anim.frameCount + anim.columns - 1) / anim.columns;

            const ffe::f32 uWidth  = 1.0f / static_cast<ffe::f32>(anim.columns);
            const ffe::f32 vHeight = 1.0f / static_cast<ffe::f32>(rows);

            sprite.uvMin.x = static_cast<ffe::f32>(col) * uWidth;
            sprite.uvMin.y = static_cast<ffe::f32>(row) * vHeight;
            sprite.uvMax.x = sprite.uvMin.x + uWidth;
            sprite.uvMax.y = sprite.uvMin.y + vHeight;
        }
        return 0;
    });
    lua_setfield(L, -2, "setAnimationFrame");

    // ffe.isAnimationPlaying(entityId) -> boolean
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
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }
        if (!world->hasComponent<ffe::SpriteAnimation>(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }

        const ffe::SpriteAnimation& anim = world->getComponent<ffe::SpriteAnimation>(entityId);
        lua_pushboolean(state, anim.playing ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isAnimationPlaying");

    // ----------------------------------------------------------------
    // Particle emitter bindings
    //
    // ffe.addEmitter(entityId [, config]) -> bool
    //   Adds a ParticleEmitter component to the entity. The entity must
    //   already have a Transform. Optional config table sets emitter
    //   properties (emitRate, lifetimeMin, lifetimeMax, speedMin, speedMax,
    //   angleMin, angleMax, sizeStart, sizeEnd, gravityY, texture,
    //   colorStartR/G/B/A, colorEndR/G/B/A, layer, sortOrder, offsetX, offsetY).
    //
    // ffe.setEmitterConfig(entityId, config) -> nothing
    //   Updates emitter properties from a config table.
    //
    // ffe.startEmitter(entityId) -> nothing
    //   Start continuous particle emission.
    //
    // ffe.stopEmitter(entityId) -> nothing
    //   Stop continuous emission. Existing particles continue to live.
    //
    // ffe.emitBurst(entityId, count) -> nothing
    //   Emit a fixed number of particles immediately.
    //
    // ffe.removeEmitter(entityId) -> nothing
    //   Remove the ParticleEmitter component.
    // ----------------------------------------------------------------

    // Helper: apply config table fields to a ParticleEmitter.
    // Expects the config table at stack index configIdx.
    // This lambda captures nothing — it reads from the Lua stack only.
    // NOTE: defined as a local function pointer for use in both addEmitter and setEmitterConfig.

    // ffe.addEmitter(entityId [, config]) -> bool
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
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }
        if (!world->hasComponent<ffe::Transform>(entityId)) {
            FFE_LOG_ERROR("ScriptEngine", "addEmitter: entity has no Transform");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Add emitter component (or reset if already present).
        // Avoid emplace_or_replace — GCC-13 ICE on large structs with that template.
        if (world->hasComponent<ffe::ParticleEmitter>(entityId)) {
            world->getComponent<ffe::ParticleEmitter>(entityId) = ffe::ParticleEmitter{};
        } else {
            world->addComponent<ffe::ParticleEmitter>(entityId);
        }
        ffe::ParticleEmitter& em = world->getComponent<ffe::ParticleEmitter>(entityId);

        // Apply optional config table (arg 2)
        if (lua_gettop(state) >= 2 && lua_istable(state, 2)) {
            const int tbl = 2;

            auto readF32 = [&](const char* key, ffe::f32& out) {
                lua_getfield(state, tbl, key);
                if (lua_isnumber(state, -1)) {
                    out = static_cast<ffe::f32>(lua_tonumber(state, -1));
                }
                lua_pop(state, 1);
            };

            readF32("emitRate",    em.emitRate);
            readF32("lifetimeMin", em.lifetimeMin);
            readF32("lifetimeMax", em.lifetimeMax);
            readF32("speedMin",    em.speedMin);
            readF32("speedMax",    em.speedMax);
            readF32("angleMin",    em.angleMin);
            readF32("angleMax",    em.angleMax);
            readF32("sizeStart",   em.sizeStart);
            readF32("sizeEnd",     em.sizeEnd);
            readF32("gravityY",    em.gravityY);
            readF32("offsetX",     em.offset.x);
            readF32("offsetY",     em.offset.y);

            readF32("colorStartR", em.colorStart.r);
            readF32("colorStartG", em.colorStart.g);
            readF32("colorStartB", em.colorStart.b);
            readF32("colorStartA", em.colorStart.a);
            readF32("colorEndR",   em.colorEnd.r);
            readF32("colorEndG",   em.colorEnd.g);
            readF32("colorEndB",   em.colorEnd.b);
            readF32("colorEndA",   em.colorEnd.a);

            lua_getfield(state, tbl, "texture");
            if (lua_isnumber(state, -1)) {
                const lua_Integer rawTex = lua_tointeger(state, -1);
                if (rawTex > 0 && rawTex <= static_cast<lua_Integer>(UINT32_MAX)) {
                    em.texture.id = static_cast<ffe::u32>(rawTex);
                }
            }
            lua_pop(state, 1);

            lua_getfield(state, tbl, "layer");
            if (lua_isnumber(state, -1)) {
                em.layer = static_cast<ffe::i16>(lua_tointeger(state, -1));
            }
            lua_pop(state, 1);

            lua_getfield(state, tbl, "sortOrder");
            if (lua_isnumber(state, -1)) {
                em.sortOrder = static_cast<ffe::i16>(lua_tointeger(state, -1));
            }
            lua_pop(state, 1);

            // Guard against NaN/Inf in float config values.
            auto guardFinite = [](ffe::f32& v, const ffe::f32 fallback) {
                if (!std::isfinite(v)) { v = fallback; }
            };
            guardFinite(em.emitRate,    0.0f);
            guardFinite(em.lifetimeMin, 0.5f);
            guardFinite(em.lifetimeMax, 1.5f);
            guardFinite(em.speedMin,    0.0f);
            guardFinite(em.speedMax,    80.0f);
            guardFinite(em.angleMin,    0.0f);
            guardFinite(em.angleMax,    6.28318f);
            guardFinite(em.sizeStart,   4.0f);
            guardFinite(em.sizeEnd,     0.0f);
            guardFinite(em.gravityY,    0.0f);
            guardFinite(em.offset.x,    0.0f);
            guardFinite(em.offset.y,    0.0f);

            // Clamp lifetime to minimum 0.001f to prevent division by zero.
            if (em.lifetimeMin < 0.001f) { em.lifetimeMin = 0.001f; }
            if (em.lifetimeMax < 0.001f) { em.lifetimeMax = 0.001f; }
        }

        lua_pushboolean(state, 1);
        return 1;
    });
    lua_setfield(L, -2, "addEmitter");

    // ffe.setEmitterConfig(entityId, config) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::ParticleEmitter>(entityId)) { return 0; }

        ffe::ParticleEmitter& em = world->getComponent<ffe::ParticleEmitter>(entityId);

        if (!lua_istable(state, 2)) {
            FFE_LOG_ERROR("ScriptEngine", "setEmitterConfig: arg 2 must be a table");
            return 0;
        }
        const int tbl = 2;

        auto readF32 = [&](const char* key, ffe::f32& out) {
            lua_getfield(state, tbl, key);
            if (lua_isnumber(state, -1)) {
                out = static_cast<ffe::f32>(lua_tonumber(state, -1));
            }
            lua_pop(state, 1);
        };

        readF32("emitRate",    em.emitRate);
        readF32("lifetimeMin", em.lifetimeMin);
        readF32("lifetimeMax", em.lifetimeMax);
        readF32("speedMin",    em.speedMin);
        readF32("speedMax",    em.speedMax);
        readF32("angleMin",    em.angleMin);
        readF32("angleMax",    em.angleMax);
        readF32("sizeStart",   em.sizeStart);
        readF32("sizeEnd",     em.sizeEnd);
        readF32("gravityY",    em.gravityY);
        readF32("offsetX",     em.offset.x);
        readF32("offsetY",     em.offset.y);

        readF32("colorStartR", em.colorStart.r);
        readF32("colorStartG", em.colorStart.g);
        readF32("colorStartB", em.colorStart.b);
        readF32("colorStartA", em.colorStart.a);
        readF32("colorEndR",   em.colorEnd.r);
        readF32("colorEndG",   em.colorEnd.g);
        readF32("colorEndB",   em.colorEnd.b);
        readF32("colorEndA",   em.colorEnd.a);

        lua_getfield(state, tbl, "texture");
        if (lua_isnumber(state, -1)) {
            const lua_Integer rawTex = lua_tointeger(state, -1);
            if (rawTex > 0 && rawTex <= static_cast<lua_Integer>(UINT32_MAX)) {
                em.texture.id = static_cast<ffe::u32>(rawTex);
            }
        }
        lua_pop(state, 1);

        lua_getfield(state, tbl, "layer");
        if (lua_isnumber(state, -1)) {
            em.layer = static_cast<ffe::i16>(lua_tointeger(state, -1));
        }
        lua_pop(state, 1);

        lua_getfield(state, tbl, "sortOrder");
        if (lua_isnumber(state, -1)) {
            em.sortOrder = static_cast<ffe::i16>(lua_tointeger(state, -1));
        }
        lua_pop(state, 1);

        // Guard against NaN/Inf in float config values.
        auto guardFinite = [](ffe::f32& v, const ffe::f32 fallback) {
            if (!std::isfinite(v)) { v = fallback; }
        };
        guardFinite(em.emitRate,    0.0f);
        guardFinite(em.lifetimeMin, 0.5f);
        guardFinite(em.lifetimeMax, 1.5f);
        guardFinite(em.speedMin,    0.0f);
        guardFinite(em.speedMax,    80.0f);
        guardFinite(em.angleMin,    0.0f);
        guardFinite(em.angleMax,    6.28318f);
        guardFinite(em.sizeStart,   4.0f);
        guardFinite(em.sizeEnd,     0.0f);
        guardFinite(em.gravityY,    0.0f);
        guardFinite(em.offset.x,    0.0f);
        guardFinite(em.offset.y,    0.0f);

        // Clamp lifetime to minimum 0.001f to prevent division by zero.
        if (em.lifetimeMin < 0.001f) { em.lifetimeMin = 0.001f; }
        if (em.lifetimeMax < 0.001f) { em.lifetimeMax = 0.001f; }

        return 0;
    });
    lua_setfield(L, -2, "setEmitterConfig");

    // ffe.startEmitter(entityId) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::ParticleEmitter>(entityId)) { return 0; }

        world->getComponent<ffe::ParticleEmitter>(entityId).emitting = true;
        return 0;
    });
    lua_setfield(L, -2, "startEmitter");

    // ffe.stopEmitter(entityId) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::ParticleEmitter>(entityId)) { return 0; }

        world->getComponent<ffe::ParticleEmitter>(entityId).emitting = false;
        return 0;
    });
    lua_setfield(L, -2, "stopEmitter");

    // ffe.emitBurst(entityId, count) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::ParticleEmitter>(entityId)) { return 0; }

        const lua_Integer rawCount = luaL_checkinteger(state, 2);
        if (rawCount <= 0) { return 0; }

        ffe::ParticleEmitter& em = world->getComponent<ffe::ParticleEmitter>(entityId);
        em.burstCount = static_cast<ffe::u32>(
            rawCount > static_cast<lua_Integer>(ffe::MAX_PARTICLES)
                ? ffe::MAX_PARTICLES
                : rawCount);
        return 0;
    });
    lua_setfield(L, -2, "emitBurst");

    // ffe.removeEmitter(entityId) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::ParticleEmitter>(entityId)) { return 0; }

        world->removeComponent<ffe::ParticleEmitter>(entityId);
        return 0;
    });
    lua_setfield(L, -2, "removeEmitter");

    // ----------------------------------------------------------------
    // Collision bindings — add/remove colliders and set collision callback.
    //
    // ffe.addCollider(entityId, shape, halfW, halfH, layer, mask, isTrigger)
    //   shape: "aabb" or "circle"
    //   halfW, halfH: positive numbers
    //   layer, mask: integers (default 0xFFFF if omitted)
    //   isTrigger: boolean (default false)
    //   Returns true on success, false on failure.
    //
    // ffe.removeCollider(entityId)
    //   Remove Collider2D component. No-op if entity has no collider.
    //
    // ffe.setCollisionCallback(func)
    //   Register a Lua function to be called for each collision event.
    //   func receives (entityA, entityB) per overlapping pair per frame.
    //   Pass nil to unregister.
    // ----------------------------------------------------------------

    // ffe.addCollider(entityId, shape, halfW, halfH [, layer, mask, isTrigger]) -> bool
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

        // Arg 1: entity ID
        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushboolean(state, 0);
            return 1;
        }

        // Arg 2: shape string ("aabb" or "circle")
        if (lua_type(state, 2) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "addCollider: shape must be a string (\"aabb\" or \"circle\")");
            lua_pushboolean(state, 0);
            return 1;
        }
        const char* shapeStr = lua_tostring(state, 2);
        ffe::ColliderShape shape = ffe::ColliderShape::AABB;
        if (shapeStr != nullptr && shapeStr[0] == 'c') {
            shape = ffe::ColliderShape::CIRCLE;
        } else if (shapeStr == nullptr || shapeStr[0] != 'a') {
            FFE_LOG_ERROR("ScriptEngine", "addCollider: unknown shape \"%s\" — use \"aabb\" or \"circle\"",
                          shapeStr != nullptr ? shapeStr : "(null)");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Arg 3: halfWidth (positive, finite)
        const lua_Number halfW = luaL_checknumber(state, 3);
        if (!std::isfinite(halfW) || halfW <= 0.0) {
            FFE_LOG_ERROR("ScriptEngine", "addCollider: halfWidth must be finite and > 0");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Arg 4: halfHeight (positive, finite; ignored for circle but must be valid)
        const lua_Number halfH = luaL_checknumber(state, 4);
        if (!std::isfinite(halfH) || halfH < 0.0) {
            FFE_LOG_ERROR("ScriptEngine", "addCollider: halfHeight must be finite and >= 0");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Arg 5 (optional): layer (default 0xFFFF)
        ffe::u16 layer = 0xFFFF;
        if (lua_gettop(state) >= 5 && !lua_isnil(state, 5)) {
            const lua_Integer rawLayer = luaL_checkinteger(state, 5);
            if (rawLayer < 0 || rawLayer > 0xFFFF) {
                FFE_LOG_WARN("ScriptEngine", "addCollider: layer out of [0, 65535] — clamping");
            }
            layer = static_cast<ffe::u16>(rawLayer < 0 ? 0 : (rawLayer > 0xFFFF ? 0xFFFF : rawLayer));
        }

        // Arg 6 (optional): mask (default 0xFFFF)
        ffe::u16 mask = 0xFFFF;
        if (lua_gettop(state) >= 6 && !lua_isnil(state, 6)) {
            const lua_Integer rawMask = luaL_checkinteger(state, 6);
            if (rawMask < 0 || rawMask > 0xFFFF) {
                FFE_LOG_WARN("ScriptEngine", "addCollider: mask out of [0, 65535] — clamping");
            }
            mask = static_cast<ffe::u16>(rawMask < 0 ? 0 : (rawMask > 0xFFFF ? 0xFFFF : rawMask));
        }

        // Arg 7 (optional): isTrigger (default false)
        bool isTrigger = false;
        if (lua_gettop(state) >= 7 && !lua_isnil(state, 7)) {
            isTrigger = lua_toboolean(state, 7) != 0;
        }

        // Overwrite guard (H-2): use emplace_or_replace.
        if (world->hasComponent<ffe::Collider2D>(entityId)) {
            FFE_LOG_WARN("ScriptEngine", "addCollider: entity already has Collider2D — overwriting");
        }
        ffe::Collider2D& col = world->registry().emplace_or_replace<ffe::Collider2D>(
            static_cast<entt::entity>(entityId));
        col.shape      = shape;
        col.isTrigger  = isTrigger;
        col.layer      = layer;
        col.mask       = mask;
        col.halfWidth  = static_cast<ffe::f32>(halfW);
        col.halfHeight = static_cast<ffe::f32>(halfH);

        lua_pushboolean(state, 1);
        return 1;
    });
    lua_setfield(L, -2, "addCollider");

    // ffe.removeCollider(entityId) -> nothing
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
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::Collider2D>(entityId)) { return 0; }

        world->removeComponent<ffe::Collider2D>(entityId);
        return 0;
    });
    lua_setfield(L, -2, "removeCollider");

    // ffe.setCollisionCallback(func) -> nothing
    // Stores the function as a Lua registry reference. The collision system
    // writes events to CollisionEventList in ECS context; ScriptEngine reads
    // them and calls this callback after all systems have run.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Allow nil to unregister the callback.
        if (lua_isnil(state, 1) || lua_gettop(state) == 0) {
            // Unregister: release old ref if any.
            lua_pushlightuserdata(state, &s_collisionCallbackKey);
            lua_gettable(state, LUA_REGISTRYINDEX);
            if (!lua_isnil(state, -1)) {
                const int oldRef = static_cast<int>(lua_tointeger(state, -1));
                if (oldRef != -2) { // LUA_NOREF
                    luaL_unref(state, LUA_REGISTRYINDEX, oldRef);
                }
            }
            lua_pop(state, 1);

            // Store LUA_NOREF sentinel.
            lua_pushlightuserdata(state, &s_collisionCallbackKey);
            lua_pushinteger(state, -2); // LUA_NOREF
            lua_settable(state, LUA_REGISTRYINDEX);

            // Also update ECS context if World is available.
            lua_pushlightuserdata(state, &s_worldRegistryKey);
            lua_gettable(state, LUA_REGISTRYINDEX);
            if (!lua_isnil(state, -1)) {
                auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
                if (world != nullptr && world->registry().ctx().contains<ffe::CollisionCallbackRef>()) {
                    world->registry().ctx().get<ffe::CollisionCallbackRef>().luaRef = -2;
                }
            }
            lua_pop(state, 1);
            return 0;
        }

        // Must be a function.
        luaL_checktype(state, 1, LUA_TFUNCTION);

        // Release old ref if any.
        lua_pushlightuserdata(state, &s_collisionCallbackKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { // LUA_NOREF
                luaL_unref(state, LUA_REGISTRYINDEX, oldRef);
            }
        }
        lua_pop(state, 1);

        // Create a new reference to the callback function.
        lua_pushvalue(state, 1); // push copy of the function
        const int ref = luaL_ref(state, LUA_REGISTRYINDEX);

        // Store the ref integer in the registry under our key.
        lua_pushlightuserdata(state, &s_collisionCallbackKey);
        lua_pushinteger(state, static_cast<lua_Integer>(ref));
        lua_settable(state, LUA_REGISTRYINDEX);

        // Also update ECS context if World is available.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
            if (world != nullptr && world->registry().ctx().contains<ffe::CollisionCallbackRef>()) {
                world->registry().ctx().get<ffe::CollisionCallbackRef>().luaRef = ref;
            }
        }
        lua_pop(state, 1);

        return 0;
    });
    lua_setfield(L, -2, "setCollisionCallback");

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

    // ffe.getAtlasUtilization() -> number [0.0, 1.0]
    // Returns the fraction of atlas space used. Debugging/profiling only.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushnumber(state, static_cast<lua_Number>(ffe::renderer::getAtlasUtilization()));
        return 1;
    });
    lua_setfield(L, -2, "getAtlasUtilization");

    // ----------------------------------------------------------------
    // Music playback bindings — wrap ffe::audio music API for Lua scripts.
    //
    // ffe.playMusic(soundHandle, loop)
    //   soundHandle: integer returned by ffe.loadSound (not yet a Lua API —
    //   currently music tracks must be loaded from C++ and the handle integer
    //   passed in via a global variable or ffe.loadSound when that is added).
    //   loop: boolean (default true if omitted)
    //   No-op in headless mode.
    //
    // ffe.stopMusic()
    //   Stop the currently playing music track.
    //
    // ffe.setMusicVolume(volume)
    //   Set music volume [0.0, 1.0]. NaN/Inf -> 0.0.
    //
    // ffe.getMusicVolume() -> number
    //   Query current music volume.
    //
    // ffe.isMusicPlaying() -> boolean
    //   Returns true if a music track is currently active.
    // ----------------------------------------------------------------

    // ffe.playMusic(soundHandle, loop)
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const lua_Integer rawHandle = luaL_checkinteger(state, 1);
        if (rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(UINT32_MAX)) {
            FFE_LOG_ERROR("ScriptEngine",
                          "playMusic: invalid handle %" PRId64 " — no-op",
                          static_cast<long long>(rawHandle));
            return 0;
        }
        const ffe::audio::SoundHandle handle{static_cast<ffe::u32>(rawHandle)};
        // Default loop = true if argument is omitted or nil.
        const bool loop = (lua_gettop(state) < 2 || lua_isnil(state, 2))
                        ? true
                        : (lua_toboolean(state, 2) != 0);
        ffe::audio::playMusic(handle, loop);
        return 0;
    });
    lua_setfield(L, -2, "playMusic");

    // ffe.stopMusic()
    lua_pushcfunction(L, [](lua_State* /*state*/) -> int {
        ffe::audio::stopMusic();
        return 0;
    });
    lua_setfield(L, -2, "stopMusic");

    // ffe.setMusicVolume(volume)
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const lua_Number vol = luaL_checknumber(state, 1);
        ffe::audio::setMusicVolume(static_cast<float>(vol));
        return 0;
    });
    lua_setfield(L, -2, "setMusicVolume");

    // ffe.getMusicVolume() -> number
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushnumber(state, static_cast<lua_Number>(ffe::audio::getMusicVolume()));
        return 1;
    });
    lua_setfield(L, -2, "getMusicVolume");

    // ffe.isMusicPlaying() -> boolean
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushboolean(state, ffe::audio::isMusicPlaying() ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isMusicPlaying");

    // ----------------------------------------------------------------
    // Sound effect bindings — load, unload, play sounds and control
    // master volume. Follows the exact same security pattern as the
    // texture lifecycle bindings (MEDIUM-1, LOW-2, LOW-5).
    //
    // ffe.loadSound(path)
    //   path must be a relative string (no traversal, no absolute paths).
    //   Uses the global asset root set by renderer::setAssetRoot() in C++.
    //   Returns an integer handle id on success, nil on failure.
    //   Return nil if the argument is not a string (MEDIUM-1 type guard).
    //   Scripts must NOT be able to specify an asset root (LOW-5).
    //
    // ffe.unloadSound(handle)
    //   handle must be a positive integer in (0, UINT32_MAX] (LOW-2).
    //   Frees the decoded PCM buffer. After this call handle must not be used.
    //   Returns nothing.
    //
    // ffe.playSound(handle [, volume])
    //   Play a one-shot sound effect. Optional volume (default 1.0).
    //   Returns nothing.
    //
    // ffe.setMasterVolume(volume)
    //   Set master volume [0.0, 1.0]. Returns nothing.
    // ----------------------------------------------------------------

    // ffe.loadSound(path) -> integer or nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // MEDIUM-1: explicit type guard — do NOT use lua_tostring without checking
        // type first. lua_tostring silently coerces numbers and booleans to strings
        // (e.g. ffe.loadSound(0) would forward "0" to C++ without this guard).
        if (lua_type(state, 1) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "loadSound: argument must be a string");
            lua_pushnil(state);
            return 1;
        }
        const char* path = lua_tostring(state, 1);

        // LOW-5: scripts must not control the asset root. We pass the engine's
        // internal asset root from the renderer (shared between textures and audio).
        const char* assetRoot = ffe::renderer::getAssetRoot();
        const ffe::audio::SoundHandle handle = ffe::audio::loadSound(path, assetRoot);

        if (handle.id == 0u) {
            lua_pushnil(state);
            return 1;
        }
        lua_pushinteger(state, static_cast<lua_Integer>(handle.id));
        return 1;
    });
    lua_setfield(L, -2, "loadSound");

    // ffe.loadMusic(path) -> integer or nil
    // Lightweight loader for music files — validates path and stores it for
    // streaming via playMusic(). Does NOT decode the file to PCM, so large
    // music tracks are accepted (no AUDIO_MAX_DECODED_BYTES limit).
    lua_pushcfunction(L, [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "loadMusic: argument must be a string");
            lua_pushnil(state);
            return 1;
        }
        const char* path = lua_tostring(state, 1);
        const char* assetRoot = ffe::renderer::getAssetRoot();
        const ffe::audio::SoundHandle handle = ffe::audio::loadMusic(path, assetRoot);

        if (handle.id == 0u) {
            lua_pushnil(state);
            return 1;
        }
        lua_pushinteger(state, static_cast<lua_Integer>(handle.id));
        return 1;
    });
    lua_setfield(L, -2, "loadMusic");

    // ffe.unloadSound(handle) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const lua_Integer rawHandle = luaL_checkinteger(state, 1);
        // LOW-2: validate handle range before calling C++.
        // 0 is the null sentinel (invalid handle); values above UINT32_MAX overflow u32.
        if (rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(UINT32_MAX)) {
            FFE_LOG_ERROR("ScriptEngine",
                          "unloadSound: handle %" PRId64 " is out of range — no-op",
                          static_cast<long long>(rawHandle));
            return 0;
        }
        const ffe::audio::SoundHandle handle{static_cast<ffe::u32>(rawHandle)};
        ffe::audio::unloadSound(handle);
        return 0;
    });
    lua_setfield(L, -2, "unloadSound");

    // ffe.playSound(handle [, volume]) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const lua_Integer rawHandle = luaL_checkinteger(state, 1);
        if (rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(UINT32_MAX)) {
            FFE_LOG_ERROR("ScriptEngine",
                          "playSound: invalid handle %" PRId64 " — no-op",
                          static_cast<long long>(rawHandle));
            return 0;
        }
        const ffe::audio::SoundHandle handle{static_cast<ffe::u32>(rawHandle)};
        // Optional second argument: volume (default 1.0f).
        const float volume = (lua_gettop(state) >= 2 && !lua_isnil(state, 2))
                           ? static_cast<float>(luaL_checknumber(state, 2))
                           : 1.0f;
        ffe::audio::playSound(handle, volume);
        return 0;
    });
    lua_setfield(L, -2, "playSound");

    // ffe.setMasterVolume(volume) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const lua_Number vol = luaL_checknumber(state, 1);
        ffe::audio::setMasterVolume(static_cast<float>(vol));
        return 0;
    });
    lua_setfield(L, -2, "setMasterVolume");

    // ----------------------------------------------------------------
    // 3D Positional Audio bindings
    //
    // ffe.playSound3D(path, x, y, z [, volume])
    //   Play a positioned sound at (x,y,z). Optional volume (default 1.0).
    //   path is a string (same as ffe.playSound).
    //   NaN/Inf coordinates are replaced with 0.0 on the C++ side.
    //
    // ffe.setListenerPosition(x, y, z, fx, fy, fz, ux, uy, uz)
    //   Manual listener override. 9 floats: position, forward, up.
    //
    // ffe.setSound3DMinDistance(dist) / ffe.setSound3DMaxDistance(dist)
    //   Configure global attenuation range.
    // ----------------------------------------------------------------

    // ffe.playSound3D(path, x, y, z [, volume]) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Argument 1: path (string)
        if (lua_type(state, 1) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "playSound3D: first argument must be a string");
            return 0;
        }
        const char* path = lua_tostring(state, 1);

        // Arguments 2-4: x, y, z
        const float x = static_cast<float>(luaL_checknumber(state, 2));
        const float y = static_cast<float>(luaL_checknumber(state, 3));
        const float z = static_cast<float>(luaL_checknumber(state, 4));

        // NaN/Inf guard on coordinates
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            FFE_LOG_WARN("ScriptEngine", "playSound3D: NaN/Inf coordinate rejected");
            return 0;
        }

        // Optional argument 5: volume (default 1.0)
        const float volume = (lua_gettop(state) >= 5 && !lua_isnil(state, 5))
                           ? static_cast<float>(luaL_checknumber(state, 5))
                           : 1.0f;

        if (!std::isfinite(volume)) {
            FFE_LOG_WARN("ScriptEngine", "playSound3D: NaN/Inf volume rejected");
            return 0;
        }

        // Load the sound using the same path-to-handle caching pattern as playSound.
        // For now, load + play (fire-and-forget). In a real game, sounds should be
        // pre-loaded. This matches the existing ffe.playSound(path, vol) Lua pattern
        // where the scripting layer auto-loads by path.
        //
        // Retrieve asset root from the renderer (same as ffe.loadSound binding).
        const char* assetRoot = ffe::renderer::getAssetRoot();
        if (!assetRoot || assetRoot[0] == '\0') {
            FFE_LOG_ERROR("ScriptEngine", "playSound3D: no asset root set");
            return 0;
        }

        ffe::audio::SoundHandle handle = ffe::audio::loadSound(path, assetRoot);
        if (!ffe::audio::isValid(handle)) {
            // loadSound already logged the specific error
            return 0;
        }

        ffe::audio::playSound3D(handle, x, y, z, volume);
        return 0;
    });
    lua_setfield(L, -2, "playSound3D");

    // ffe.setListenerPosition(x, y, z, fx, fy, fz, ux, uy, uz) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const float x  = static_cast<float>(luaL_checknumber(state, 1));
        const float y  = static_cast<float>(luaL_checknumber(state, 2));
        const float z  = static_cast<float>(luaL_checknumber(state, 3));
        const float fx = static_cast<float>(luaL_checknumber(state, 4));
        const float fy = static_cast<float>(luaL_checknumber(state, 5));
        const float fz = static_cast<float>(luaL_checknumber(state, 6));
        const float ux = static_cast<float>(luaL_checknumber(state, 7));
        const float uy = static_cast<float>(luaL_checknumber(state, 8));
        const float uz = static_cast<float>(luaL_checknumber(state, 9));

        // NaN/Inf guard — setListenerPosition sanitizes internally, but log a warning
        if (!std::isfinite(x)  || !std::isfinite(y)  || !std::isfinite(z)  ||
            !std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz) ||
            !std::isfinite(ux) || !std::isfinite(uy) || !std::isfinite(uz)) {
            FFE_LOG_WARN("ScriptEngine", "setListenerPosition: NaN/Inf parameter detected — sanitized");
        }

        ffe::audio::setListenerPosition(x, y, z, fx, fy, fz, ux, uy, uz);
        return 0;
    });
    lua_setfield(L, -2, "setListenerPosition");

    // ffe.setSound3DMinDistance(dist) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const float dist = static_cast<float>(luaL_checknumber(state, 1));
        if (!std::isfinite(dist)) {
            FFE_LOG_WARN("ScriptEngine", "setSound3DMinDistance: NaN/Inf rejected");
            return 0;
        }
        ffe::audio::setSound3DMinDistance(dist);
        return 0;
    });
    lua_setfield(L, -2, "setSound3DMinDistance");

    // ffe.setSound3DMaxDistance(dist) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const float dist = static_cast<float>(luaL_checknumber(state, 1));
        if (!std::isfinite(dist)) {
            FFE_LOG_WARN("ScriptEngine", "setSound3DMaxDistance: NaN/Inf rejected");
            return 0;
        }
        ffe::audio::setSound3DMaxDistance(dist);
        return 0;
    });
    lua_setfield(L, -2, "setSound3DMaxDistance");

    // ffe.setHudText(text) -> nothing
    // Sets the on-screen HUD text via HudTextBuffer in ECS context.
    // Pass "" or nil to clear the HUD.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Retrieve World* from Lua registry
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        if (world == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "setHudText: World not set");
            return 0;
        }

        auto* hudBuf = world->registry().ctx().find<ffe::HudTextBuffer>();
        if (hudBuf == nullptr) {
            return 0;
        }

        if (lua_isnil(state, 1) || lua_type(state, 1) != LUA_TSTRING) {
            hudBuf->text[0] = '\0';
            return 0;
        }

        const char* text = lua_tostring(state, 1);
        if (text != nullptr) {
            std::strncpy(hudBuf->text, text, ffe::HUD_TEXT_BUFFER_SIZE - 1);
            hudBuf->text[ffe::HUD_TEXT_BUFFER_SIZE - 1] = '\0';
        } else {
            hudBuf->text[0] = '\0';
        }

        return 0;
    });
    lua_setfield(L, -2, "setHudText");

    // ----------------------------------------------------------------
    // Timer API
    // ----------------------------------------------------------------

    // ffe.after(seconds, callback) -> timerId or nil
    // Schedules a one-shot timer. The callback fires once after 'seconds'.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Number seconds = luaL_checknumber(state, 1);
        luaL_checktype(state, 2, LUA_TFUNCTION);

        if (!std::isfinite(seconds) || seconds < 0.0) {
            lua_pushnil(state);
            return 1;
        }

        const i32 slot = engine->allocTimer();
        if (slot < 0) {
            FFE_LOG_WARN("ScriptEngine", "ffe.after: max timers reached (%u)", ScriptEngine::MAX_TIMERS);
            lua_pushnil(state);
            return 1;
        }

        // Store the callback function in the Lua registry.
        lua_pushvalue(state, 2);
        const int ref = luaL_ref(state, LUA_REGISTRYINDEX);

        auto& t = engine->m_timers[slot];
        t.remaining = static_cast<f32>(seconds);
        t.interval  = static_cast<f32>(seconds);
        t.luaRef    = ref;
        t.active    = true;
        t.repeating = false;

        lua_pushinteger(state, slot);
        return 1;
    });
    lua_setfield(L, -2, "after");

    // ffe.every(seconds, callback) -> timerId or nil
    // Schedules a repeating timer. The callback fires every 'seconds'.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Number seconds = luaL_checknumber(state, 1);
        luaL_checktype(state, 2, LUA_TFUNCTION);

        if (!std::isfinite(seconds) || seconds <= 0.0) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.every: interval must be > 0");
            lua_pushnil(state);
            return 1;
        }

        const i32 slot = engine->allocTimer();
        if (slot < 0) {
            FFE_LOG_WARN("ScriptEngine", "ffe.every: max timers reached (%u)", ScriptEngine::MAX_TIMERS);
            lua_pushnil(state);
            return 1;
        }

        lua_pushvalue(state, 2);
        const int ref = luaL_ref(state, LUA_REGISTRYINDEX);

        auto& t = engine->m_timers[slot];
        t.remaining = static_cast<f32>(seconds);
        t.interval  = static_cast<f32>(seconds);
        t.luaRef    = ref;
        t.active    = true;
        t.repeating = true;

        lua_pushinteger(state, slot);
        return 1;
    });
    lua_setfield(L, -2, "every");

    // ffe.cancelTimer(timerId) -> nothing
    // Cancels an active timer. No-op for invalid or already-cancelled IDs.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 0 || rawId >= static_cast<lua_Integer>(ScriptEngine::MAX_TIMERS)) {
            return 0;
        }

        const u32 slot = static_cast<u32>(rawId);
        auto& t = engine->m_timers[slot];
        if (t.active && t.luaRef != LUA_NOREF) {
            luaL_unref(state, LUA_REGISTRYINDEX, t.luaRef);
        }
        t = {};
        return 0;
    });
    lua_setfield(L, -2, "cancelTimer");

    // ffe.destroyAllEntities() -> nothing
    // Nuclear reset for scene transitions. Cleans up heap-owning components,
    // clears collision callback, cancels all timers, then destroys all entities.
    // NOTE: Update this function when new heap-owning components are added.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // 1. Free Tilemap heap allocations before clearing the registry.
        auto tmView = world->view<ffe::Tilemap>();
        for (auto entity : tmView) {
            ffe::destroyTilemap(tmView.get<ffe::Tilemap>(entity));
        }

        // 2. Clear the collision callback Lua ref to prevent stale callbacks.
        lua_pushlightuserdata(state, &s_collisionCallbackKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { // -2 = LUA_NOREF sentinel
                luaL_unref(state, LUA_REGISTRYINDEX, oldRef);
            }
        }
        lua_pop(state, 1);
        lua_pushlightuserdata(state, &s_collisionCallbackKey);
        lua_pushinteger(state, -2); // LUA_NOREF
        lua_settable(state, LUA_REGISTRYINDEX);

        if (world->registry().ctx().contains<ffe::CollisionCallbackRef>()) {
            world->registry().ctx().get<ffe::CollisionCallbackRef>().luaRef = -2;
        }

        // 2b. Clear the 3D collision callback Lua ref.
        lua_pushlightuserdata(state, &s_collision3DCallbackKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int old3DRef = static_cast<int>(lua_tointeger(state, -1));
            if (old3DRef != -2) { // LUA_NOREF
                luaL_unref(state, LUA_REGISTRYINDEX, old3DRef);
            }
        }
        lua_pop(state, 1);
        lua_pushlightuserdata(state, &s_collision3DCallbackKey);
        lua_pushinteger(state, -2); // LUA_NOREF
        lua_settable(state, LUA_REGISTRYINDEX);

        // 3. Cancel all active timers so callbacks don't fire on dead entities.
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
            for (ffe::u32 i = 0; i < engine->m_timerCount; ++i) {
                auto& t = engine->m_timers[i];
                if (t.active && t.luaRef != LUA_NOREF) {
                    luaL_unref(state, LUA_REGISTRYINDEX, t.luaRef);
                }
                t = {};
            }
            engine->m_timerCount = 0;
        }
        lua_pop(state, 1);

        // 4. Clear all entities and components from the ECS registry.
        world->clearAllEntities();

        return 0;
    });
    lua_setfield(L, -2, "destroyAllEntities");

    // ffe.cancelAllTimers() -> nothing
    // Cancels every active timer. Useful for scene transitions.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        for (ffe::u32 i = 0; i < engine->m_timerCount; ++i) {
            auto& t = engine->m_timers[i];
            if (t.active && t.luaRef != LUA_NOREF) {
                luaL_unref(state, LUA_REGISTRYINDEX, t.luaRef);
            }
            t = {};
        }
        engine->m_timerCount = 0;
        return 0;
    });
    lua_setfield(L, -2, "cancelAllTimers");

    // ffe.loadScene(scriptPath) -> nothing
    // Loads and executes a Lua script file for scene transitions.
    // scriptPath is validated with the same safety checks as doFile.
    // Re-entrancy is guarded: max depth of 4 nested loadScene calls.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        if (!lua_isstring(state, 1)) {
            FFE_LOG_ERROR("ScriptEngine", "loadScene: expected string argument");
            return 0;
        }
        const char* scriptPath = lua_tostring(state, 1);

        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        if (engine->scriptRoot() == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "loadScene: script root not set");
            return 0;
        }

        if (engine->m_loadSceneDepth >= ScriptEngine::MAX_LOAD_SCENE_DEPTH) {
            FFE_LOG_ERROR("ScriptEngine", "loadScene: max re-entrancy depth (%u) exceeded",
                          ScriptEngine::MAX_LOAD_SCENE_DEPTH);
            return 0;
        }

        ++engine->m_loadSceneDepth;
        engine->doFile(scriptPath, engine->m_assetRoot);
        --engine->m_loadSceneDepth;

        return 0;
    });
    lua_setfield(L, -2, "loadScene");

    // ffe.loadSceneJSON(jsonPath) -> bool
    // Loads a JSON scene file exported by the editor. Clears all existing
    // entities first, then deserialises the JSON scene into the world.
    // The path is relative and validated with the same safety checks as other
    // file-loading bindings. Returns true on success, false on failure.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        if (!lua_isstring(state, 1)) {
            FFE_LOG_ERROR("ScriptEngine", "loadSceneJSON: expected string argument");
            lua_pushboolean(state, 0);
            return 1;
        }
        const char* jsonPath = lua_tostring(state, 1);

        if (!isPathSafe(jsonPath)) {
            FFE_LOG_ERROR("ScriptEngine", "loadSceneJSON: path rejected by safety check: %s",
                          jsonPath != nullptr ? jsonPath : "(null)");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Retrieve the World pointer from the Lua registry.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            FFE_LOG_ERROR("ScriptEngine", "loadSceneJSON: no World registered");
            lua_pushboolean(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // Retrieve the ScriptEngine pointer for the asset root.
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            FFE_LOG_ERROR("ScriptEngine", "loadSceneJSON: no ScriptEngine registered");
            lua_pushboolean(state, 0);
            return 1;
        }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        if (engine->scriptRoot() == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "loadSceneJSON: script root not set");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Build the full path: <scriptRoot>/<jsonPath>
        char fullPath[1024];
        std::snprintf(fullPath, sizeof(fullPath), "%s/%s",
                      engine->scriptRoot(), jsonPath);

        // Clear existing entities before loading the new scene.
        world->clearAllEntities();

        const bool ok = ffe::scene::loadScene(*world, std::string(fullPath));
        lua_pushboolean(state, ok ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "loadSceneJSON");

    // ----------------------------------------------------------------
    // Save/Load bindings — persist Lua tables as JSON files.
    //
    // ffe.saveData(filename, table) -> true | (nil, error)
    // ffe.loadData(filename)        -> table | (nil, error)
    //
    // Security: filename is validated against an allowlist, path traversal
    // is prevented, file size is capped at 1 MB, and the save directory is
    // sandboxed under the configured save root.
    // ----------------------------------------------------------------

    // ffe.saveData(filename, table) -> true | (nil, error)
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Retrieve ScriptEngine pointer from registry.
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        if (engine == nullptr) {
            lua_pushnil(state);
            lua_pushstring(state, "internal error");
            return 2;
        }

        // Arg 1: filename (must be a string, not coerced)
        if (lua_type(state, 1) != LUA_TSTRING) {
            lua_pushnil(state);
            lua_pushstring(state, "filename must be a string");
            return 2;
        }
        const char* filename = lua_tostring(state, 1);

        // Arg 2: data (must be a table)
        if (lua_type(state, 2) != LUA_TTABLE) {
            lua_pushnil(state);
            lua_pushstring(state, "data must be a table");
            return 2;
        }

        // Check save root is configured.
        const char* root = engine->saveRoot();
        if (root == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "saveData: save root not configured");
            lua_pushnil(state);
            lua_pushstring(state, "save root not configured");
            return 2;
        }

        // Validate filename (S1).
        if (!isValidSaveFilename(filename)) {
            FFE_LOG_WARN("ScriptEngine", "saveData: invalid filename '%s'", filename);
            lua_pushnil(state);
            lua_pushstring(state, "invalid filename");
            return 2;
        }

        // Build saves directory path: <saveRoot>/saves/
        char savesDir[PATH_MAX];
        const int sdLen = std::snprintf(savesDir, sizeof(savesDir), "%s/saves", root);
        if (sdLen < 0 || static_cast<std::size_t>(sdLen) >= sizeof(savesDir)) {
            FFE_LOG_ERROR("ScriptEngine", "saveData: saves directory path too long");
            lua_pushnil(state);
            lua_pushstring(state, "write failed");
            return 2;
        }

        // Create saves/ directory if it does not exist.
        {
            std::error_code ec;
            std::filesystem::create_directories(savesDir, ec);
            if (ec) {
                FFE_LOG_ERROR("ScriptEngine", "saveData: failed to create saves directory: %s", ec.message().c_str());
                lua_pushnil(state);
                lua_pushstring(state, "write failed");
                return 2;
            }
        }

        // Check file count limit before writing a new file.
        // Only check if the target file does not already exist (overwrite is ok).
        char fullPath[PATH_MAX];
        const int fpLen = std::snprintf(fullPath, sizeof(fullPath), "%s/%s", savesDir, filename);
        if (fpLen < 0 || static_cast<std::size_t>(fpLen) >= sizeof(fullPath)) {
            FFE_LOG_ERROR("ScriptEngine", "saveData: file path too long");
            lua_pushnil(state);
            lua_pushstring(state, "write failed");
            return 2;
        }

        // S2 — resolved path validation.
        // Validate saves dir is under save root. Use canonicalizePath on the directory
        // (which exists now after create_directories).
        {
            char resolvedDir[PATH_MAX];
            if (!ffe::canonicalizePath(savesDir, resolvedDir, sizeof(resolvedDir))) {
                FFE_LOG_ERROR("ScriptEngine", "saveData: canonicalizePath failed for saves dir");
                lua_pushnil(state);
                lua_pushstring(state, "write failed");
                return 2;
            }

            char resolvedRoot[PATH_MAX];
            if (!ffe::canonicalizePath(root, resolvedRoot, sizeof(resolvedRoot))) {
                FFE_LOG_ERROR("ScriptEngine", "saveData: canonicalizePath failed for save root");
                lua_pushnil(state);
                lua_pushstring(state, "write failed");
                return 2;
            }

            const std::size_t rootLen = std::strlen(resolvedRoot);
            if (std::strncmp(resolvedDir, resolvedRoot, rootLen) != 0 ||
                (resolvedDir[rootLen] != '/' && resolvedDir[rootLen] != '\0')) {
                FFE_LOG_ERROR("ScriptEngine", "saveData: saves dir escapes save root");
                lua_pushnil(state);
                lua_pushstring(state, "invalid filename");
                return 2;
            }
        }

        // Check if this is a new file. If so, enforce file count limit.
        {
            struct stat st;
            if (stat(fullPath, &st) != 0) {
                // File does not exist — check count.
                const int fileCount = countJsonFiles(savesDir);
                if (fileCount >= MAX_SAVE_FILE_COUNT) {
                    FFE_LOG_WARN("ScriptEngine", "saveData: save file limit (%d) reached", MAX_SAVE_FILE_COUNT);
                    lua_pushnil(state);
                    lua_pushstring(state, "too many save files");
                    return 2;
                }
            }
        }

        // Serialize the Lua table to JSON.
        nlohmann::json jsonData = luaTableToJson(state, 2, 0);
        const std::string jsonStr = jsonData.dump(2); // pretty-print with indent=2

        // S3 — check serialized size.
        if (jsonStr.size() > MAX_SAVE_FILE_SIZE) {
            FFE_LOG_WARN("ScriptEngine", "saveData: serialized data too large (%zu bytes, max %zu)",
                         jsonStr.size(), MAX_SAVE_FILE_SIZE);
            lua_pushnil(state);
            lua_pushstring(state, "save data too large");
            return 2;
        }

        // Atomic write: write to .tmp, then rename.
        char tmpPath[PATH_MAX];
        const int tpLen = std::snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", fullPath);
        if (tpLen < 0 || static_cast<std::size_t>(tpLen) >= sizeof(tmpPath)) {
            lua_pushnil(state);
            lua_pushstring(state, "write failed");
            return 2;
        }

        {
            FILE* f = std::fopen(tmpPath, "wb");
            if (f == nullptr) {
                FFE_LOG_ERROR("ScriptEngine", "saveData: failed to open tmp file for writing");
                lua_pushnil(state);
                lua_pushstring(state, "write failed");
                return 2;
            }

            const std::size_t written = std::fwrite(jsonStr.data(), 1, jsonStr.size(), f);
            std::fclose(f);

            if (written != jsonStr.size()) {
                FFE_LOG_ERROR("ScriptEngine", "saveData: incomplete write (%zu / %zu bytes)", written, jsonStr.size());
                std::remove(tmpPath);
                lua_pushnil(state);
                lua_pushstring(state, "write failed");
                return 2;
            }
        }

        // Rename .tmp -> target (atomic on POSIX).
        if (std::rename(tmpPath, fullPath) != 0) {
            FFE_LOG_ERROR("ScriptEngine", "saveData: rename failed");
            std::remove(tmpPath);
            lua_pushnil(state);
            lua_pushstring(state, "write failed");
            return 2;
        }

        FFE_LOG_INFO("ScriptEngine", "saveData: saved '%s' (%zu bytes)", filename, jsonStr.size());
        lua_pushboolean(state, 1);
        return 1;
    });
    lua_setfield(L, -2, "saveData");

    // ffe.loadData(filename) -> table | (nil, error)
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Retrieve ScriptEngine pointer from registry.
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        if (engine == nullptr) {
            lua_pushnil(state);
            lua_pushstring(state, "internal error");
            return 2;
        }

        // Arg 1: filename (must be a string, not coerced)
        if (lua_type(state, 1) != LUA_TSTRING) {
            lua_pushnil(state);
            lua_pushstring(state, "filename must be a string");
            return 2;
        }
        const char* filename = lua_tostring(state, 1);

        // Check save root is configured.
        const char* root = engine->saveRoot();
        if (root == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "loadData: save root not configured");
            lua_pushnil(state);
            lua_pushstring(state, "save root not configured");
            return 2;
        }

        // Validate filename (S1).
        if (!isValidSaveFilename(filename)) {
            FFE_LOG_WARN("ScriptEngine", "loadData: invalid filename '%s'", filename);
            lua_pushnil(state);
            lua_pushstring(state, "invalid filename");
            return 2;
        }

        // Build full path: <saveRoot>/saves/<filename>
        char fullPath[PATH_MAX];
        const int fpLen = std::snprintf(fullPath, sizeof(fullPath), "%s/saves/%s", root, filename);
        if (fpLen < 0 || static_cast<std::size_t>(fpLen) >= sizeof(fullPath)) {
            FFE_LOG_ERROR("ScriptEngine", "loadData: file path too long");
            lua_pushnil(state);
            lua_pushstring(state, "file not found");
            return 2;
        }

        // S3 — stat the file for size check before reading.
        struct stat st;
        if (stat(fullPath, &st) != 0) {
            lua_pushnil(state);
            lua_pushstring(state, "file not found");
            return 2;
        }

        if (static_cast<std::size_t>(st.st_size) > MAX_SAVE_FILE_SIZE) {
            FFE_LOG_WARN("ScriptEngine", "loadData: file too large (%lld bytes, max %zu)",
                         static_cast<long long>(st.st_size), MAX_SAVE_FILE_SIZE);
            lua_pushnil(state);
            lua_pushstring(state, "save file too large");
            return 2;
        }

        // S2 — resolved path validation.
        {
            char resolvedPath[PATH_MAX];
            if (!ffe::canonicalizePath(fullPath, resolvedPath, sizeof(resolvedPath))) {
                FFE_LOG_ERROR("ScriptEngine", "loadData: canonicalizePath failed");
                lua_pushnil(state);
                lua_pushstring(state, "file not found");
                return 2;
            }

            char resolvedRoot[PATH_MAX];
            if (!ffe::canonicalizePath(root, resolvedRoot, sizeof(resolvedRoot))) {
                FFE_LOG_ERROR("ScriptEngine", "loadData: canonicalizePath failed for save root");
                lua_pushnil(state);
                lua_pushstring(state, "file not found");
                return 2;
            }

            const std::size_t rootLen = std::strlen(resolvedRoot);
            if (std::strncmp(resolvedPath, resolvedRoot, rootLen) != 0 ||
                (resolvedPath[rootLen] != '/' && resolvedPath[rootLen] != '\0')) {
                FFE_LOG_ERROR("ScriptEngine", "loadData: resolved path escapes save root");
                lua_pushnil(state);
                lua_pushstring(state, "invalid filename");
                return 2;
            }
        }

        // Read the file.
        FILE* f = std::fopen(fullPath, "rb");
        if (f == nullptr) {
            lua_pushnil(state);
            lua_pushstring(state, "file not found");
            return 2;
        }

        const auto fileSize = static_cast<std::size_t>(st.st_size);
        std::string contents;
        contents.resize(fileSize);
        const std::size_t bytesRead = std::fread(contents.data(), 1, fileSize, f);
        std::fclose(f);

        if (bytesRead != fileSize) {
            FFE_LOG_ERROR("ScriptEngine", "loadData: incomplete read (%zu / %zu bytes)", bytesRead, fileSize);
            lua_pushnil(state);
            lua_pushstring(state, "corrupted save file");
            return 2;
        }

        // Parse JSON (no-exception mode).
        nlohmann::json parsed = nlohmann::json::parse(contents, nullptr, false);
        if (parsed.is_discarded()) {
            FFE_LOG_ERROR("ScriptEngine", "loadData: JSON parse failed for '%s'", filename);
            lua_pushnil(state);
            lua_pushstring(state, "corrupted save file");
            return 2;
        }

        // Convert JSON to Lua table and push onto stack.
        jsonToLuaTable(state, parsed);

        FFE_LOG_INFO("ScriptEngine", "loadData: loaded '%s' (%zu bytes)", filename, fileSize);
        return 1;
    });
    lua_setfield(L, -2, "loadData");

    // ----------------------------------------------------------------
    // TTF Font bindings — load, unload, draw, and measure TrueType fonts.
    //
    // ffe.loadFont(path, size)                        -> fontId or nil
    // ffe.unloadFont(fontId)                          -> nothing
    // ffe.drawFontText(fontId, text, x, y [, ...])    -> nothing
    // ffe.measureText(fontId, text [, scale])          -> width, height
    // ----------------------------------------------------------------

    // ffe.loadFont(path, size) -> integer (1-8) or nil + error
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Retrieve World pointer for TextRenderer access.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushnil(state);
            lua_pushstring(state, "no world");
            return 2;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** trPtr = world->registry().ctx().find<ffe::renderer::TextRenderer*>();
        if (trPtr == nullptr || *trPtr == nullptr) {
            lua_pushnil(state);
            lua_pushstring(state, "no text renderer");
            return 2;
        }

        // Arg 1: relative path (string, not coerced).
        if (lua_type(state, 1) != LUA_TSTRING) {
            lua_pushnil(state);
            lua_pushstring(state, "path must be a string");
            return 2;
        }
        const char* relPath = lua_tostring(state, 1);

        // Arg 2: pixel size (number).
        const lua_Number size = luaL_checknumber(state, 2);
        if (!std::isfinite(size) || size < 4.0 || size > 256.0) {
            lua_pushnil(state);
            lua_pushstring(state, "size must be between 4 and 256");
            return 2;
        }

        // Validate path safety.
        if (!isPathSafe(relPath)) {
            FFE_LOG_ERROR("ScriptEngine", "loadFont: path traversal rejected: '%s'", relPath);
            lua_pushnil(state);
            lua_pushstring(state, "invalid path");
            return 2;
        }

        // Resolve against asset root.
        const char* assetRoot = ffe::renderer::getAssetRoot();
        if (assetRoot == nullptr || assetRoot[0] == '\0') {
            lua_pushnil(state);
            lua_pushstring(state, "asset root not configured");
            return 2;
        }

        char absPath[PATH_MAX];
        const int pathLen = std::snprintf(absPath, sizeof(absPath), "%s/%s", assetRoot, relPath);
        if (pathLen < 0 || static_cast<std::size_t>(pathLen) >= sizeof(absPath)) {
            lua_pushnil(state);
            lua_pushstring(state, "path too long");
            return 2;
        }

        // Resolve and verify the path is under asset root.
        char resolvedPath[PATH_MAX];
        if (!ffe::canonicalizePath(absPath, resolvedPath, sizeof(resolvedPath))) {
            lua_pushnil(state);
            lua_pushstring(state, "file not found");
            return 2;
        }

        char resolvedRoot[PATH_MAX];
        if (!ffe::canonicalizePath(assetRoot, resolvedRoot, sizeof(resolvedRoot))) {
            lua_pushnil(state);
            lua_pushstring(state, "asset root invalid");
            return 2;
        }

        const std::size_t rootLen = std::strlen(resolvedRoot);
        if (std::strncmp(resolvedPath, resolvedRoot, rootLen) != 0 ||
            (resolvedPath[rootLen] != '/' && resolvedPath[rootLen] != '\0')) {
            FFE_LOG_ERROR("ScriptEngine", "loadFont: path escapes asset root");
            lua_pushnil(state);
            lua_pushstring(state, "invalid path");
            return 2;
        }

        const ffe::u32 fontId = ffe::renderer::loadFont(**trPtr, resolvedPath,
                                                         static_cast<ffe::f32>(size));
        if (fontId == 0) {
            lua_pushnil(state);
            lua_pushstring(state, "failed to load font");
            return 2;
        }

        lua_pushinteger(state, static_cast<lua_Integer>(fontId));
        return 1;
    });
    lua_setfield(L, -2, "loadFont");

    // ffe.unloadFont(fontId) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** trPtr = world->registry().ctx().find<ffe::renderer::TextRenderer*>();
        if (trPtr == nullptr || *trPtr == nullptr) return 0;

        const lua_Integer rawId = luaL_checkinteger(state, 1);
        if (rawId < 1 || rawId > static_cast<lua_Integer>(ffe::renderer::MAX_FONTS)) {
            return 0; // Out of range — no-op.
        }

        ffe::renderer::unloadFont(**trPtr, static_cast<ffe::u32>(rawId));
        return 0;
    });
    lua_setfield(L, -2, "unloadFont");

    // ffe.drawFontText(fontId, text, x, y [, scale, r, g, b, a]) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** trPtr = world->registry().ctx().find<ffe::renderer::TextRenderer*>();
        if (trPtr == nullptr || *trPtr == nullptr) return 0;

        const lua_Integer fontId = luaL_checkinteger(state, 1);
        const char* text         = luaL_checkstring(state, 2);
        const lua_Number x       = luaL_checknumber(state, 3);
        const lua_Number y       = luaL_checknumber(state, 4);
        const lua_Number scale   = luaL_optnumber(state, 5, 1.0);
        const lua_Number r       = luaL_optnumber(state, 6, 1.0);
        const lua_Number g       = luaL_optnumber(state, 7, 1.0);
        const lua_Number b       = luaL_optnumber(state, 8, 1.0);
        const lua_Number a       = luaL_optnumber(state, 9, 1.0);

        // Reject NaN/Inf
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(scale) ||
            !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
            return 0;
        }

        if (fontId < 1 || fontId > static_cast<lua_Integer>(ffe::renderer::MAX_FONTS)) {
            return 0; // Invalid font ID — silently ignore.
        }

        const auto clamp01 = [](lua_Number v) -> ffe::f32 {
            return static_cast<ffe::f32>(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
        };
        const ffe::f32 clampedScale = static_cast<ffe::f32>(
            scale < 0.1 ? 0.1 : (scale > 20.0 ? 20.0 : scale));

        ffe::renderer::drawFontText(**trPtr, static_cast<ffe::u32>(fontId), text,
            static_cast<ffe::f32>(x), static_cast<ffe::f32>(y), clampedScale,
            clamp01(r), clamp01(g), clamp01(b), clamp01(a));
        return 0;
    });
    lua_setfield(L, -2, "drawFontText");

    // ffe.measureText(fontId, text [, scale]) -> width, height
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            return 2;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** trPtr = world->registry().ctx().find<ffe::renderer::TextRenderer*>();
        if (trPtr == nullptr || *trPtr == nullptr) {
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            return 2;
        }

        const lua_Integer fontId = luaL_checkinteger(state, 1);
        const char* text         = luaL_checkstring(state, 2);
        const lua_Number scale   = luaL_optnumber(state, 3, 1.0);

        if (!std::isfinite(scale) || fontId < 1 ||
            fontId > static_cast<lua_Integer>(ffe::renderer::MAX_FONTS)) {
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            return 2;
        }

        const ffe::f32 clampedScale = static_cast<ffe::f32>(
            scale < 0.1 ? 0.1 : (scale > 20.0 ? 20.0 : scale));

        ffe::f32 w = 0.0f;
        ffe::f32 h = 0.0f;
        ffe::renderer::measureText(**trPtr, static_cast<ffe::u32>(fontId), text,
                                   clampedScale, &w, &h);

        lua_pushnumber(state, static_cast<lua_Number>(w));
        lua_pushnumber(state, static_cast<lua_Number>(h));
        return 2;
    });
    lua_setfield(L, -2, "measureText");

    // ----------------------------------------------------------------
    // Gamepad bindings — query gamepad state from Lua.
    //
    // ffe.isGamepadConnected(id)               -> bool
    // ffe.isGamepadButtonPressed(id, button)   -> bool
    // ffe.isGamepadButtonHeld(id, button)      -> bool
    // ffe.isGamepadButtonReleased(id, button)  -> bool
    // ffe.getGamepadAxis(id, axis)             -> number
    // ffe.getGamepadName(id)                   -> string
    // ffe.setGamepadDeadzone(value)            -> nothing
    // ----------------------------------------------------------------

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto id = static_cast<ffe::i32>(luaL_checkinteger(state, 1));
        lua_pushboolean(state, ffe::isGamepadConnected(id) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isGamepadConnected");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto id  = static_cast<ffe::i32>(luaL_checkinteger(state, 1));
        const auto btn = static_cast<ffe::GamepadButton>(luaL_checkinteger(state, 2));
        lua_pushboolean(state, ffe::isGamepadButtonPressed(id, btn) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isGamepadButtonPressed");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto id  = static_cast<ffe::i32>(luaL_checkinteger(state, 1));
        const auto btn = static_cast<ffe::GamepadButton>(luaL_checkinteger(state, 2));
        lua_pushboolean(state, ffe::isGamepadButtonHeld(id, btn) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isGamepadButtonHeld");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto id  = static_cast<ffe::i32>(luaL_checkinteger(state, 1));
        const auto btn = static_cast<ffe::GamepadButton>(luaL_checkinteger(state, 2));
        lua_pushboolean(state, ffe::isGamepadButtonReleased(id, btn) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isGamepadButtonReleased");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto id   = static_cast<ffe::i32>(luaL_checkinteger(state, 1));
        const auto axis = static_cast<ffe::GamepadAxis>(luaL_checkinteger(state, 2));
        lua_pushnumber(state, static_cast<lua_Number>(ffe::getGamepadAxis(id, axis)));
        return 1;
    });
    lua_setfield(L, -2, "getGamepadAxis");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto id = static_cast<ffe::i32>(luaL_checkinteger(state, 1));
        const char* name = ffe::getGamepadName(id);
        lua_pushstring(state, name);
        return 1;
    });
    lua_setfield(L, -2, "getGamepadName");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto dz = static_cast<ffe::f32>(luaL_checknumber(state, 1));
        ffe::setGamepadDeadzone(dz);
        return 0;
    });
    lua_setfield(L, -2, "setGamepadDeadzone");

    lua_pushcfunction(L, [](lua_State* state) -> int {
        const ffe::f32 dz = ffe::getGamepadDeadzone();
        lua_pushnumber(state, static_cast<lua_Number>(dz));
        return 1;
    });
    lua_setfield(L, -2, "getGamepadDeadzone");

    // Gamepad button constants
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::A));             lua_setfield(L, -2, "GAMEPAD_A");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::B));             lua_setfield(L, -2, "GAMEPAD_B");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::X));             lua_setfield(L, -2, "GAMEPAD_X");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::Y));             lua_setfield(L, -2, "GAMEPAD_Y");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::LEFT_BUMPER));   lua_setfield(L, -2, "GAMEPAD_LEFT_BUMPER");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::RIGHT_BUMPER));  lua_setfield(L, -2, "GAMEPAD_RIGHT_BUMPER");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::BACK));          lua_setfield(L, -2, "GAMEPAD_BACK");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::START));         lua_setfield(L, -2, "GAMEPAD_START");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::DPAD_UP));       lua_setfield(L, -2, "GAMEPAD_DPAD_UP");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::DPAD_DOWN));     lua_setfield(L, -2, "GAMEPAD_DPAD_DOWN");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::DPAD_LEFT));     lua_setfield(L, -2, "GAMEPAD_DPAD_LEFT");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::DPAD_RIGHT));    lua_setfield(L, -2, "GAMEPAD_DPAD_RIGHT");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::GUIDE));         lua_setfield(L, -2, "GAMEPAD_GUIDE");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::LEFT_STICK));    lua_setfield(L, -2, "GAMEPAD_LEFT_STICK");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadButton::RIGHT_STICK));   lua_setfield(L, -2, "GAMEPAD_RIGHT_STICK");

    // Gamepad axis constants
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadAxis::LEFT_X));          lua_setfield(L, -2, "GAMEPAD_AXIS_LEFT_X");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadAxis::LEFT_Y));          lua_setfield(L, -2, "GAMEPAD_AXIS_LEFT_Y");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadAxis::RIGHT_X));         lua_setfield(L, -2, "GAMEPAD_AXIS_RIGHT_X");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadAxis::RIGHT_Y));         lua_setfield(L, -2, "GAMEPAD_AXIS_RIGHT_Y");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadAxis::LEFT_TRIGGER));    lua_setfield(L, -2, "GAMEPAD_AXIS_LEFT_TRIGGER");
    lua_pushinteger(L, static_cast<lua_Integer>(ffe::GamepadAxis::RIGHT_TRIGGER));   lua_setfield(L, -2, "GAMEPAD_AXIS_RIGHT_TRIGGER");

    // ----------------------------------------------------------------
    // 3D mesh bindings (ADR-007 Section 11)
    // ----------------------------------------------------------------

    // ffe.loadMesh(path: string) -> integer
    // Load a .glb mesh from a path relative to the asset root.
    // Returns integer mesh handle > 0 on success, 0 on failure.
    // Cold path only — do NOT call per frame.
    auto ffe_loadMesh = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadMesh: argument 1 must be a string");
            lua_pushinteger(state, 0);
            return 1;
        }
        const char* path = lua_tostring(state, 1);
        if (path == nullptr || path[0] == '\0') {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadMesh: path is null or empty");
            lua_pushinteger(state, 0);
            return 1;
        }
        const ffe::renderer::MeshHandle handle = ffe::renderer::loadMesh(path);
        lua_pushinteger(state, static_cast<lua_Integer>(handle.id));
        return 1;
    };
    lua_pushcfunction(L, ffe_loadMesh);
    lua_setfield(L, -2, "loadMesh");

    // ffe.unloadMesh(meshHandle: integer) -> nothing
    // Destroy a loaded mesh and free GPU resources.
    auto ffe_unloadMesh = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.unloadMesh: argument 1 must be a number");
            return 0;
        }
        const lua_Integer rawHandle = lua_tointeger(state, 1);
        if (rawHandle <= 0) {
            return 0; // invalid handle — no-op
        }
        if (static_cast<ffe::u32>(rawHandle) > ffe::renderer::MAX_MESH_ASSETS) { return 0; }
        ffe::renderer::unloadMesh(ffe::renderer::MeshHandle{static_cast<ffe::u32>(rawHandle)});
        return 0;
    };
    lua_pushcfunction(L, ffe_unloadMesh);
    lua_setfield(L, -2, "unloadMesh");

    // ffe.getInstanceCount(meshHandle: integer) -> integer
    // Returns how many entities in the world have this mesh handle.
    // Useful for debugging instancing behaviour.
    auto ffe_getInstanceCount = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.getInstanceCount: argument 1 must be a number");
            lua_pushinteger(state, 0);
            return 1;
        }
        const auto rawHandle = lua_tointeger(state, 1);
        if (rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(ffe::renderer::MAX_MESH_ASSETS)) {
            lua_pushinteger(state, 0);
            return 1;
        }
        // Retrieve World pointer from the Lua registry.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushinteger(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        if (world == nullptr) {
            lua_pushinteger(state, 0);
            return 1;
        }
        const ffe::u32 count = ffe::renderer::getInstanceCount(
            *world, static_cast<ffe::u32>(rawHandle));
        lua_pushinteger(state, static_cast<lua_Integer>(count));
        return 1;
    };
    lua_pushcfunction(L, ffe_getInstanceCount);
    lua_setfield(L, -2, "getInstanceCount");

    // ffe.createEntity3D(meshHandle: integer, x: number, y: number, z: number) -> integer
    // Create an entity with Transform3D and Mesh components at the given position.
    // Returns entity ID on success, 0 on failure.
    auto ffe_createEntity3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.createEntity3D: argument 1 (meshHandle) must be a number");
            lua_pushinteger(state, 0);
            return 1;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushinteger(state, 0);
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawHandle = lua_tointeger(state, 1);
        if (rawHandle <= 0) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.createEntity3D: invalid mesh handle %" PRId64,
                          static_cast<long long>(rawHandle));
            lua_pushinteger(state, 0);
            return 1;
        }
        const ffe::renderer::MeshHandle meshHandle{static_cast<ffe::u32>(rawHandle)};
        if (!ffe::renderer::isValid(meshHandle)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.createEntity3D: mesh handle %u is invalid",
                          meshHandle.id);
            lua_pushinteger(state, 0);
            return 1;
        }

        const ffe::f32 x = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
        const ffe::f32 y = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
        const ffe::f32 z = static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0));

        const ffe::EntityId entityId = world->createEntity();

        ffe::Transform3D t3d;
        t3d.position = {x, y, z};
        world->registry().emplace<ffe::Transform3D>(static_cast<entt::entity>(entityId), t3d);

        ffe::Mesh meshComp;
        meshComp.meshHandle = meshHandle;
        world->registry().emplace<ffe::Mesh>(static_cast<entt::entity>(entityId), meshComp);

        lua_pushinteger(state, static_cast<lua_Integer>(entityId));
        return 1;
    };
    lua_pushcfunction(L, ffe_createEntity3D);
    lua_setfield(L, -2, "createEntity3D");

    // ffe.setTransform3D(entityId, x, y, z, rx, ry, rz, sx, sy, sz) -> nothing
    // Set the full 3D transform of an entity.
    // rx, ry, rz are Euler angles in DEGREES (converted to quaternion internally).
    // GLM's quat(vec3) applies rotations in intrinsic XYZ order (extrinsic ZYX).
    // sx, sy, sz are scale factors.
    auto ffe_setTransform3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setTransform3D: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setTransform3D: invalid entity ID %" PRId64,
                          static_cast<long long>(rawId));
            return 0;
        }

        const ffe::f32 px = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
        const ffe::f32 py = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
        const ffe::f32 pz = static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0));
        const ffe::f32 rx = static_cast<ffe::f32>(luaL_optnumber(state, 5, 0.0));
        const ffe::f32 ry = static_cast<ffe::f32>(luaL_optnumber(state, 6, 0.0));
        const ffe::f32 rz = static_cast<ffe::f32>(luaL_optnumber(state, 7, 0.0));
        const ffe::f32 sx = static_cast<ffe::f32>(luaL_optnumber(state, 8, 1.0));
        const ffe::f32 sy = static_cast<ffe::f32>(luaL_optnumber(state, 9, 1.0));
        const ffe::f32 sz = static_cast<ffe::f32>(luaL_optnumber(state, 10, 1.0));

        // Convert Euler degrees to quaternion (GLM intrinsic XYZ / extrinsic ZYX).
        const glm::quat rotation = glm::quat(glm::radians(glm::vec3{rx, ry, rz}));

        ffe::Transform3D& t3d = world->registry().get_or_emplace<ffe::Transform3D>(
            static_cast<entt::entity>(entityId));
        t3d.position = {px, py, pz};
        t3d.rotation = rotation;
        t3d.scale    = {sx, sy, sz};

        // Sync to Jolt physics body if entity has a RigidBody3D component.
        // Without this, physics3dSyncSystem overwrites Transform3D each frame,
        // causing desync (e.g. player falls through ground when Lua sets rotation).
        const auto* rb = world->registry().try_get<ffe::RigidBody3D>(
            static_cast<entt::entity>(entityId));
        if (rb != nullptr && rb->initialized && ffe::physics::isValid(rb->handle)) {
            ffe::physics::setBodyPosition(rb->handle, {px, py, pz});
            ffe::physics::setBodyRotation(rb->handle, rotation);
        }

        return 0;
    };
    lua_pushcfunction(L, ffe_setTransform3D);
    lua_setfield(L, -2, "setTransform3D");

    // ----------------------------------------------------------------
    // ffe.fillTransform3D(entityId, buf) -> nothing
    //   Zero-allocation alternative for reading a 3D transform.
    //   Writes x, y, z (position), rx, ry, rz (rotation in degrees,
    //   GLM intrinsic XYZ order), and sx, sy, sz (scale) into the caller-provided table.
    //   Returns early safely if entity is invalid or has no Transform3D.
    //   The caller pre-allocates the table once and reuses it every frame,
    //   eliminating per-call GC pressure from table creation.
    // ----------------------------------------------------------------
    auto ffe_fillTransform3D = [](lua_State* state) -> int {
        // Retrieve World pointer from the registry.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return 0;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // Validate arg 1: entity ID (integer).
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.fillTransform3D: argument 1 (entityId) must be a number");
            return 0;
        }
        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            return 0;
        }

        // Validate arg 2: must be a table.
        luaL_checktype(state, 2, LUA_TTABLE);

        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            return 0;
        }

        // Transform3D is managed via the raw entt registry (not the World
        // component wrapper), so use registry().try_get for safe access.
        const ffe::Transform3D* t3d = world->registry().try_get<ffe::Transform3D>(
            static_cast<entt::entity>(entityId));
        if (t3d == nullptr) {
            return 0;
        }

        // If the entity has an active RigidBody3D, read position and rotation
        // directly from the Jolt physics body instead of the ECS Transform3D.
        // This guarantees the caller always sees the physics-driven values,
        // even if physics3dSyncSystem hasn't run yet this tick (e.g., Lua
        // systems at lower priority than Physics3DSync, or future refactors
        // that reorder system priorities). Without this, fillTransform3D
        // could return stale ECS values that, when written back via
        // setTransform3D, fight the physics solver and cause hovering.
        glm::vec3 position = t3d->position;
        glm::quat rotation = t3d->rotation;

        const auto* rb = world->registry().try_get<ffe::RigidBody3D>(
            static_cast<entt::entity>(entityId));
        if (rb != nullptr && rb->initialized && ffe::physics::isValid(rb->handle)) {
            position = ffe::physics::getBodyPosition(rb->handle);
            rotation = ffe::physics::getBodyRotation(rb->handle);
        }

        // Convert quaternion back to Euler angles in degrees.
        // glm::eulerAngles inverts glm::quat(glm::radians(vec3{rx, ry, rz})),
        // returning radians in the same component order (intrinsic XYZ).
        const glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(rotation));

        // Fill the caller's table in-place — no new table allocation.
        lua_pushnumber(state, static_cast<lua_Number>(position.x)); lua_setfield(state, 2, "x");
        lua_pushnumber(state, static_cast<lua_Number>(position.y)); lua_setfield(state, 2, "y");
        lua_pushnumber(state, static_cast<lua_Number>(position.z)); lua_setfield(state, 2, "z");
        lua_pushnumber(state, static_cast<lua_Number>(eulerDeg.x));      lua_setfield(state, 2, "rx");
        lua_pushnumber(state, static_cast<lua_Number>(eulerDeg.y));      lua_setfield(state, 2, "ry");
        lua_pushnumber(state, static_cast<lua_Number>(eulerDeg.z));      lua_setfield(state, 2, "rz");
        lua_pushnumber(state, static_cast<lua_Number>(t3d->scale.x));    lua_setfield(state, 2, "sx");
        lua_pushnumber(state, static_cast<lua_Number>(t3d->scale.y));    lua_setfield(state, 2, "sy");
        lua_pushnumber(state, static_cast<lua_Number>(t3d->scale.z));    lua_setfield(state, 2, "sz");

        return 0;
    };
    lua_pushcfunction(L, ffe_fillTransform3D);
    lua_setfield(L, -2, "fillTransform3D");

    // ffe.set3DCamera(x, y, z, targetX, targetY, targetZ) -> nothing
    // Set the 3D perspective camera position and look-at target.
    auto ffe_set3DCamera = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* cam = world->registry().ctx().find<ffe::renderer::Camera*>();
        if (cam == nullptr || *cam == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.set3DCamera: 3D camera not in ECS context");
            return 0;
        }

        const ffe::f32 x       = static_cast<ffe::f32>(luaL_optnumber(state, 1, 0.0));
        const ffe::f32 y       = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
        const ffe::f32 z       = static_cast<ffe::f32>(luaL_optnumber(state, 3, 5.0));
        const ffe::f32 targetX = static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0));
        const ffe::f32 targetY = static_cast<ffe::f32>(luaL_optnumber(state, 5, 0.0));
        const ffe::f32 targetZ = static_cast<ffe::f32>(luaL_optnumber(state, 6, 0.0));

        (*cam)->position = {x, y, z};
        (*cam)->target   = {targetX, targetY, targetZ};
        // up vector remains {0,1,0} — not settable from this binding

        return 0;
    };
    lua_pushcfunction(L, ffe_set3DCamera);
    lua_setfield(L, -2, "set3DCamera");

    // ffe.set3DCameraFPS(x, y, z, yaw_deg, pitch_deg) -> nothing
    // First-person-shooter style camera: position + yaw/pitch angles.
    // Yaw: 0 looks down +Z (right-hand Y-up). Pitch clamped to [-89, 89] degrees.
    auto ffe_set3DCameraFPS = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* cam = world->registry().ctx().find<ffe::renderer::Camera*>();
        if (cam == nullptr || *cam == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.set3DCameraFPS: 3D camera not in ECS context");
            return 0;
        }

        const ffe::f32 x         = static_cast<ffe::f32>(luaL_optnumber(state, 1, 0.0));
        const ffe::f32 y         = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
        const ffe::f32 z         = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
        const ffe::f32 yaw_deg   = static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0));
        const ffe::f32 pitch_deg = static_cast<ffe::f32>(luaL_optnumber(state, 5, 0.0));

        static constexpr ffe::f32 PI        = 3.14159265358979323846f;
        static constexpr ffe::f32 DEG_TO_RAD = PI / 180.0f;

        const ffe::f32 pitch_clamped = std::max(-89.0f, std::min(89.0f, pitch_deg));
        const ffe::f32 yaw_rad       = yaw_deg       * DEG_TO_RAD;
        const ffe::f32 pitch_rad     = pitch_clamped  * DEG_TO_RAD;

        const ffe::f32 fx = std::cos(pitch_rad) * std::sin(yaw_rad);
        const ffe::f32 fy = std::sin(pitch_rad);
        const ffe::f32 fz = std::cos(pitch_rad) * std::cos(yaw_rad);

        (*cam)->position = {x, y, z};
        (*cam)->target   = {x + fx, y + fy, z + fz};
        // up vector remains {0,1,0} — not modified

        return 0;
    };
    lua_pushcfunction(L, ffe_set3DCameraFPS);
    lua_setfield(L, -2, "set3DCameraFPS");

    // ffe.set3DCameraOrbit(target_x, target_y, target_z, radius, yaw_deg, pitch_deg) -> nothing
    // Orbit camera: camera circles a target point at the given radius.
    // Pitch clamped to [-85, 85] degrees to prevent up-vector ambiguity at poles.
    // No-op if radius <= 0 or any input is non-finite.
    auto ffe_set3DCameraOrbit = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* cam = world->registry().ctx().find<ffe::renderer::Camera*>();
        if (cam == nullptr || *cam == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.set3DCameraOrbit: 3D camera not in ECS context");
            return 0;
        }

        const ffe::f32 target_x  = static_cast<ffe::f32>(luaL_optnumber(state, 1, 0.0));
        const ffe::f32 target_y  = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
        const ffe::f32 target_z  = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
        const ffe::f32 radius    = static_cast<ffe::f32>(luaL_optnumber(state, 4, 5.0));
        const ffe::f32 yaw_deg   = static_cast<ffe::f32>(luaL_optnumber(state, 5, 0.0));
        const ffe::f32 pitch_deg = static_cast<ffe::f32>(luaL_optnumber(state, 6, 0.0));

        // Guard: NaN/Inf inputs
        if (!std::isfinite(target_x) || !std::isfinite(target_y) || !std::isfinite(target_z) ||
            !std::isfinite(radius)   || !std::isfinite(yaw_deg)   || !std::isfinite(pitch_deg)) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.set3DCameraOrbit: non-finite input detected — call ignored");
            return 0;
        }

        // Guard: radius must be positive
        if (radius <= 0.0f) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.set3DCameraOrbit: radius must be > 0 — call ignored");
            return 0;
        }

        static constexpr ffe::f32 PI        = 3.14159265358979323846f;
        static constexpr ffe::f32 DEG_TO_RAD = PI / 180.0f;

        const ffe::f32 pitch_clamped = std::max(-85.0f, std::min(85.0f, pitch_deg));
        const ffe::f32 yaw_rad       = yaw_deg       * DEG_TO_RAD;
        const ffe::f32 pitch_rad     = pitch_clamped  * DEG_TO_RAD;

        const ffe::f32 cx = target_x + radius * std::cos(pitch_rad) * std::sin(yaw_rad);
        const ffe::f32 cy = target_y + radius * std::sin(pitch_rad);
        const ffe::f32 cz = target_z + radius * std::cos(pitch_rad) * std::cos(yaw_rad);

        (*cam)->position = {cx, cy, cz};
        (*cam)->target   = {target_x, target_y, target_z};
        // up vector remains {0,1,0} — not modified

        return 0;
    };
    lua_pushcfunction(L, ffe_set3DCameraOrbit);
    lua_setfield(L, -2, "set3DCameraOrbit");

    // ffe.setMeshColor(entityId: integer, r, g, b, a: number) -> nothing
    // Set the diffuse color tint on a 3D entity's Material3D component.
    // Creates Material3D if not present on the entity.
    auto ffe_setMeshColor = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setMeshColor: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setMeshColor: invalid entity ID %" PRId64,
                          static_cast<long long>(rawId));
            return 0;
        }

        const ffe::f32 r = static_cast<ffe::f32>(luaL_optnumber(state, 2, 1.0));
        const ffe::f32 g = static_cast<ffe::f32>(luaL_optnumber(state, 3, 1.0));
        const ffe::f32 b = static_cast<ffe::f32>(luaL_optnumber(state, 4, 1.0));
        const ffe::f32 a = static_cast<ffe::f32>(luaL_optnumber(state, 5, 1.0));

        ffe::Material3D& mat = world->registry().get_or_emplace<ffe::Material3D>(
            static_cast<entt::entity>(entityId));
        mat.diffuseColor = {r, g, b, a};

        return 0;
    };
    lua_pushcfunction(L, ffe_setMeshColor);
    lua_setfield(L, -2, "setMeshColor");

    // ffe.setMeshTexture(entityId: integer, textureHandle: integer) -> nothing
    // Set the diffuse texture on a 3D entity's Material3D component.
    // textureHandle is the integer returned by ffe.loadTexture (0 = clear / white fallback).
    // Creates Material3D if not already present. Guards: entity must be valid; handle >= 0.
    auto ffe_setMeshTexture = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.setMeshTexture: argument 1 (entityId) must be a number");
            return 0;
        }

        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.setMeshTexture: invalid entity ID %" PRId64,
                          static_cast<long long>(rawId));
            return 0;
        }

        // textureHandle: 0 = clear (white fallback), >0 = use this texture
        const lua_Integer rawTex = luaL_optinteger(state, 2, 0);
        if (rawTex < 0) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.setMeshTexture: negative textureHandle ignored");
            return 0;
        }

        ffe::Material3D& mat = world->registry().get_or_emplace<ffe::Material3D>(
            static_cast<entt::entity>(entityId));
        mat.diffuseTexture = ffe::rhi::TextureHandle{static_cast<ffe::u32>(rawTex)};

        return 0;
    };
    lua_pushcfunction(L, ffe_setMeshTexture);
    lua_setfield(L, -2, "setMeshTexture");

    // ffe.setMeshSpecular(entityId: integer, r, g, b, shininess: number) -> nothing
    // Set specular color and shininess on a 3D entity's Material3D component.
    // Creates Material3D if not present on the entity.
    auto ffe_setMeshSpecular = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setMeshSpecular: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setMeshSpecular: invalid entity ID %" PRId64,
                          static_cast<long long>(rawId));
            return 0;
        }

        const ffe::f32 r  = static_cast<ffe::f32>(luaL_optnumber(state, 2, 1.0));
        const ffe::f32 g  = static_cast<ffe::f32>(luaL_optnumber(state, 3, 1.0));
        const ffe::f32 b  = static_cast<ffe::f32>(luaL_optnumber(state, 4, 1.0));
        const ffe::f32 sh = static_cast<ffe::f32>(luaL_optnumber(state, 5, 32.0));

        ffe::Material3D& mat = world->registry().get_or_emplace<ffe::Material3D>(
            static_cast<entt::entity>(entityId));
        mat.specularColor = {r, g, b};
        mat.shininess     = std::max(sh, 1.0f);  // clamp to at least 1 to avoid pow(x,0) issues

        return 0;
    };
    lua_pushcfunction(L, ffe_setMeshSpecular);
    lua_setfield(L, -2, "setMeshSpecular");

    // ffe.setMeshNormalMap(entityId: integer, textureId: integer) -> nothing
    // Bind a normal map texture to a 3D entity's Material3D component.
    // textureId is the integer returned by ffe.loadTexture (0 = disable normal mapping).
    // Creates Material3D if not already present.
    auto ffe_setMeshNormalMap = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setMeshNormalMap: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setMeshNormalMap: invalid entity ID %" PRId64,
                          static_cast<long long>(rawId));
            return 0;
        }

        const lua_Integer rawTex = luaL_optinteger(state, 2, 0);
        if (rawTex < 0) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setMeshNormalMap: negative textureId ignored");
            return 0;
        }

        ffe::Material3D& mat = world->registry().get_or_emplace<ffe::Material3D>(
            static_cast<entt::entity>(entityId));
        mat.normalMapTexture = ffe::rhi::TextureHandle{static_cast<ffe::u32>(rawTex)};

        return 0;
    };
    lua_pushcfunction(L, ffe_setMeshNormalMap);
    lua_setfield(L, -2, "setMeshNormalMap");

    // ffe.setMeshSpecularMap(entityId: integer, textureId: integer) -> nothing
    // Bind a specular map texture to a 3D entity's Material3D component.
    // textureId is the integer returned by ffe.loadTexture (0 = disable specular mapping).
    // Creates Material3D if not already present.
    auto ffe_setMeshSpecularMap = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setMeshSpecularMap: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setMeshSpecularMap: invalid entity ID %" PRId64,
                          static_cast<long long>(rawId));
            return 0;
        }

        const lua_Integer rawTex = luaL_optinteger(state, 2, 0);
        if (rawTex < 0) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setMeshSpecularMap: negative textureId ignored");
            return 0;
        }

        ffe::Material3D& mat = world->registry().get_or_emplace<ffe::Material3D>(
            static_cast<entt::entity>(entityId));
        mat.specularMapTexture = ffe::rhi::TextureHandle{static_cast<ffe::u32>(rawTex)};

        return 0;
    };
    lua_pushcfunction(L, ffe_setMeshSpecularMap);
    lua_setfield(L, -2, "setMeshSpecularMap");

    // =========================================================================
    // PBR Material bindings
    // =========================================================================

    // ffe.setPBRMaterial(entityId: integer, params: table) -> nothing
    // Create or update a PBRMaterial component on the entity.
    // params is a table with optional fields:
    //   albedo = {r, g, b, a}     (default 1,1,1,1)
    //   metallic = number         (default 0.0, clamped 0-1)
    //   roughness = number        (default 0.5, clamped 0.04-1.0)
    //   normalScale = number      (default 1.0)
    //   ao = number               (default 1.0, clamped 0-1)
    //   emissive = {r, g, b}      (default 0,0,0)
    auto ffe_setPBRMaterial = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setPBRMaterial: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setPBRMaterial: invalid entity ID %" PRId64,
                          static_cast<long long>(rawId));
            return 0;
        }

        ffe::renderer::PBRMaterial& mat = world->registry().get_or_emplace<ffe::renderer::PBRMaterial>(
            static_cast<entt::entity>(entityId));

        // If arg 2 is a table, read optional fields from it
        if (lua_istable(state, 2)) {
            // albedo = {r, g, b, a}
            lua_getfield(state, 2, "albedo");
            if (lua_istable(state, -1)) {
                lua_rawgeti(state, -1, 1);
                const ffe::f32 r = static_cast<ffe::f32>(luaL_optnumber(state, -1, 1.0));
                lua_pop(state, 1);
                lua_rawgeti(state, -1, 2);
                const ffe::f32 g = static_cast<ffe::f32>(luaL_optnumber(state, -1, 1.0));
                lua_pop(state, 1);
                lua_rawgeti(state, -1, 3);
                const ffe::f32 b = static_cast<ffe::f32>(luaL_optnumber(state, -1, 1.0));
                lua_pop(state, 1);
                lua_rawgeti(state, -1, 4);
                const ffe::f32 a = static_cast<ffe::f32>(luaL_optnumber(state, -1, 1.0));
                lua_pop(state, 1);
                mat.albedo = {r, g, b, a};
            }
            lua_pop(state, 1);

            // metallic (clamped 0-1)
            lua_getfield(state, 2, "metallic");
            if (lua_isnumber(state, -1)) {
                mat.metallic = std::clamp(static_cast<ffe::f32>(lua_tonumber(state, -1)), 0.0f, 1.0f);
            }
            lua_pop(state, 1);

            // roughness (clamped 0.04-1.0)
            lua_getfield(state, 2, "roughness");
            if (lua_isnumber(state, -1)) {
                mat.roughness = std::clamp(static_cast<ffe::f32>(lua_tonumber(state, -1)), 0.04f, 1.0f);
            }
            lua_pop(state, 1);

            // normalScale
            lua_getfield(state, 2, "normalScale");
            if (lua_isnumber(state, -1)) {
                mat.normalScale = static_cast<ffe::f32>(lua_tonumber(state, -1));
            }
            lua_pop(state, 1);

            // ao (clamped 0-1)
            lua_getfield(state, 2, "ao");
            if (lua_isnumber(state, -1)) {
                mat.ao = std::clamp(static_cast<ffe::f32>(lua_tonumber(state, -1)), 0.0f, 1.0f);
            }
            lua_pop(state, 1);

            // emissive = {r, g, b}
            lua_getfield(state, 2, "emissive");
            if (lua_istable(state, -1)) {
                lua_rawgeti(state, -1, 1);
                const ffe::f32 er = static_cast<ffe::f32>(luaL_optnumber(state, -1, 0.0));
                lua_pop(state, 1);
                lua_rawgeti(state, -1, 2);
                const ffe::f32 eg = static_cast<ffe::f32>(luaL_optnumber(state, -1, 0.0));
                lua_pop(state, 1);
                lua_rawgeti(state, -1, 3);
                const ffe::f32 eb = static_cast<ffe::f32>(luaL_optnumber(state, -1, 0.0));
                lua_pop(state, 1);
                mat.emissiveFactor = {er, eg, eb};
            }
            lua_pop(state, 1);
        }

        return 0;
    };
    lua_pushcfunction(L, ffe_setPBRMaterial);
    lua_setfield(L, -2, "setPBRMaterial");

    // ffe.setPBRTexture(entityId: integer, slot: string, textureHandle: integer) -> nothing
    // Set a texture map on the entity's PBRMaterial.
    // slot: "albedo", "metallicRoughness", "normal", "ao", "emissive"
    // textureHandle: integer from ffe.loadTexture (0 = clear).
    auto ffe_setPBRTexture = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setPBRTexture: argument 1 (entityId) must be a number");
            return 0;
        }
        if (lua_type(state, 2) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setPBRTexture: argument 2 (slot) must be a string");
            return 0;
        }

        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setPBRTexture: invalid entity ID %" PRId64,
                          static_cast<long long>(rawId));
            return 0;
        }

        const char* slot = lua_tostring(state, 2);
        const lua_Integer rawTex = luaL_optinteger(state, 3, 0);
        if (rawTex < 0) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setPBRTexture: negative textureHandle ignored");
            return 0;
        }
        const ffe::rhi::TextureHandle texHandle{static_cast<ffe::u32>(rawTex)};

        ffe::renderer::PBRMaterial& mat = world->registry().get_or_emplace<ffe::renderer::PBRMaterial>(
            static_cast<entt::entity>(entityId));

        if (std::strcmp(slot, "albedo") == 0) {
            mat.albedoMap = texHandle;
        } else if (std::strcmp(slot, "metallicRoughness") == 0) {
            mat.metallicRoughnessMap = texHandle;
        } else if (std::strcmp(slot, "normal") == 0) {
            mat.normalMap = texHandle;
        } else if (std::strcmp(slot, "ao") == 0) {
            mat.aoMap = texHandle;
        } else if (std::strcmp(slot, "emissive") == 0) {
            mat.emissiveMap = texHandle;
        } else {
            FFE_LOG_WARN("ScriptEngine", "ffe.setPBRTexture: unknown slot '%s'", slot);
        }

        return 0;
    };
    lua_pushcfunction(L, ffe_setPBRTexture);
    lua_setfield(L, -2, "setPBRTexture");

    // ffe.removePBRMaterial(entityId: integer) -> nothing
    // Remove PBRMaterial component; entity falls back to Material3D / Blinn-Phong.
    auto ffe_removePBRMaterial = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.removePBRMaterial: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }
        if (!world->hasComponent<ffe::renderer::PBRMaterial>(entityId)) { return 0; }

        world->removeComponent<ffe::renderer::PBRMaterial>(entityId);
        return 0;
    };
    lua_pushcfunction(L, ffe_removePBRMaterial);
    lua_setfield(L, -2, "removePBRMaterial");

    // ffe.setLightDirection(x, y, z: number) -> nothing
    // Set the scene directional light direction (world space).
    // M-4: zero-length vector guard — rejects if length < 1e-4.
    auto ffe_setLightDirection = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* lighting = world->registry().ctx().find<ffe::renderer::SceneLighting3D>();
        if (lighting == nullptr) { return 0; }

        const ffe::f32 x = static_cast<ffe::f32>(luaL_optnumber(state, 1, 0.5));
        const ffe::f32 y = static_cast<ffe::f32>(luaL_optnumber(state, 2, -1.0));
        const ffe::f32 z = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.3));

        const glm::vec3 inputVec{x, y, z};
        const float len = glm::length(inputVec);
        if (len < 0.0001f) {
            // M-4: zero-length vector guard — NaN in GLSL normalize(0,0,0)
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.setLightDirection: zero-length vector rejected (would produce NaN in shader). "
                         "Previous value unchanged.");
            return 0;
        }
        lighting->lightDir = glm::normalize(inputVec);

        return 0;
    };
    lua_pushcfunction(L, ffe_setLightDirection);
    lua_setfield(L, -2, "setLightDirection");

    // ffe.setLightColor(r, g, b: number) -> nothing
    // Set the directional light color (typically [0, 1]).
    auto ffe_setLightColor = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* lighting = world->registry().ctx().find<ffe::renderer::SceneLighting3D>();
        if (lighting == nullptr) { return 0; }

        lighting->lightColor = {
            static_cast<ffe::f32>(luaL_optnumber(state, 1, 1.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 2, 1.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 3, 1.0))
        };
        return 0;
    };
    lua_pushcfunction(L, ffe_setLightColor);
    lua_setfield(L, -2, "setLightColor");

    // ffe.setAmbientColor(r, g, b: number) -> nothing
    // Set the ambient light color (typically [0, 0.15]).
    auto ffe_setAmbientColor = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* lighting = world->registry().ctx().find<ffe::renderer::SceneLighting3D>();
        if (lighting == nullptr) { return 0; }

        lighting->ambientColor = {
            static_cast<ffe::f32>(luaL_optnumber(state, 1, 0.15)),
            static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.15)),
            static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.15))
        };
        return 0;
    };
    lua_pushcfunction(L, ffe_setAmbientColor);
    lua_setfield(L, -2, "setAmbientColor");

    // ----------------------------------------------------------------
    // Point light bindings
    // ----------------------------------------------------------------

    // ffe.addPointLight(index, x, y, z, r, g, b, radius) -> nothing
    // Set a point light at slot index (0-7). Enables the light.
    auto ffe_addPointLight = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* lighting = world->registry().ctx().find<ffe::renderer::SceneLighting3D>();
        if (lighting == nullptr) { return 0; }

        const int idx = static_cast<int>(luaL_checkinteger(state, 1));
        if (idx < 0 || idx >= static_cast<int>(ffe::renderer::MAX_POINT_LIGHTS)) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.addPointLight: index %d out of range [0, %u)",
                         idx, static_cast<unsigned int>(ffe::renderer::MAX_POINT_LIGHTS));
            return 0;
        }

        auto& pl = lighting->pointLights[static_cast<ffe::u32>(idx)];
        pl.position = {
            static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0))
        };
        pl.color = {
            static_cast<ffe::f32>(luaL_optnumber(state, 5, 1.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 6, 1.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 7, 1.0))
        };
        pl.radius = std::max(static_cast<ffe::f32>(luaL_optnumber(state, 8, 10.0)), 0.001f);
        pl.active = true;

        return 0;
    };
    lua_pushcfunction(L, ffe_addPointLight);
    lua_setfield(L, -2, "addPointLight");

    // ffe.removePointLight(index) -> nothing
    // Disable the point light at slot index.
    auto ffe_removePointLight = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* lighting = world->registry().ctx().find<ffe::renderer::SceneLighting3D>();
        if (lighting == nullptr) { return 0; }

        const int idx = static_cast<int>(luaL_checkinteger(state, 1));
        if (idx < 0 || idx >= static_cast<int>(ffe::renderer::MAX_POINT_LIGHTS)) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.removePointLight: index %d out of range [0, %u)",
                         idx, static_cast<unsigned int>(ffe::renderer::MAX_POINT_LIGHTS));
            return 0;
        }

        lighting->pointLights[static_cast<ffe::u32>(idx)].active = false;
        return 0;
    };
    lua_pushcfunction(L, ffe_removePointLight);
    lua_setfield(L, -2, "removePointLight");

    // ffe.setPointLightPosition(index, x, y, z) -> nothing
    auto ffe_setPointLightPosition = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* lighting = world->registry().ctx().find<ffe::renderer::SceneLighting3D>();
        if (lighting == nullptr) { return 0; }

        const int idx = static_cast<int>(luaL_checkinteger(state, 1));
        if (idx < 0 || idx >= static_cast<int>(ffe::renderer::MAX_POINT_LIGHTS)) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.setPointLightPosition: index %d out of range [0, %u)",
                         idx, static_cast<unsigned int>(ffe::renderer::MAX_POINT_LIGHTS));
            return 0;
        }

        lighting->pointLights[static_cast<ffe::u32>(idx)].position = {
            static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0))
        };
        return 0;
    };
    lua_pushcfunction(L, ffe_setPointLightPosition);
    lua_setfield(L, -2, "setPointLightPosition");

    // ffe.setPointLightColor(index, r, g, b) -> nothing
    auto ffe_setPointLightColor = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* lighting = world->registry().ctx().find<ffe::renderer::SceneLighting3D>();
        if (lighting == nullptr) { return 0; }

        const int idx = static_cast<int>(luaL_checkinteger(state, 1));
        if (idx < 0 || idx >= static_cast<int>(ffe::renderer::MAX_POINT_LIGHTS)) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.setPointLightColor: index %d out of range [0, %u)",
                         idx, static_cast<unsigned int>(ffe::renderer::MAX_POINT_LIGHTS));
            return 0;
        }

        lighting->pointLights[static_cast<ffe::u32>(idx)].color = {
            static_cast<ffe::f32>(luaL_optnumber(state, 2, 1.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 3, 1.0)),
            static_cast<ffe::f32>(luaL_optnumber(state, 4, 1.0))
        };
        return 0;
    };
    lua_pushcfunction(L, ffe_setPointLightColor);
    lua_setfield(L, -2, "setPointLightColor");

    // ffe.setPointLightRadius(index, radius) -> nothing
    auto ffe_setPointLightRadius = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* lighting = world->registry().ctx().find<ffe::renderer::SceneLighting3D>();
        if (lighting == nullptr) { return 0; }

        const int idx = static_cast<int>(luaL_checkinteger(state, 1));
        if (idx < 0 || idx >= static_cast<int>(ffe::renderer::MAX_POINT_LIGHTS)) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.setPointLightRadius: index %d out of range [0, %u)",
                         idx, static_cast<unsigned int>(ffe::renderer::MAX_POINT_LIGHTS));
            return 0;
        }

        const ffe::f32 radius = static_cast<ffe::f32>(luaL_optnumber(state, 2, 10.0));
        lighting->pointLights[static_cast<ffe::u32>(idx)].radius = std::max(radius, 0.001f);
        return 0;
    };
    lua_pushcfunction(L, ffe_setPointLightRadius);
    lua_setfield(L, -2, "setPointLightRadius");

    // ----------------------------------------------------------------
    // Shadow mapping bindings
    // ----------------------------------------------------------------

    // ffe.enableShadows(size: int) -> boolean
    // Enable shadow mapping with a shadow map of the given resolution.
    // size must be a power of 2 in [256, 4096]. Default: 1024.
    // Returns true on success, false on failure.
    auto ffe_enableShadows = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushboolean(state, 0); return 1; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** cfgPtr = world->registry().ctx().find<ffe::ShadowConfig*>();
        auto** mapPtr = world->registry().ctx().find<ffe::ShadowMap*>();
        if (cfgPtr == nullptr || mapPtr == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.enableShadows: shadow context not in ECS");
            lua_pushboolean(state, 0);
            return 1;
        }
        ffe::ShadowConfig* cfg = *cfgPtr;
        ffe::ShadowMap* sm     = *mapPtr;

        const int size = static_cast<int>(luaL_optinteger(state, 1, 1024));

        // Validate power of 2 in [256, 4096]
        const bool isPow2 = (size > 0) && ((size & (size - 1)) == 0);
        if (!isPow2 || size < 256 || size > 4096) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.enableShadows: size must be power of 2 in [256, 4096] (got %d)", size);
            lua_pushboolean(state, 0);
            return 1;
        }

        // Already enabled with same resolution — no-op success.
        if (cfg->enabled && sm->resolution == size && sm->fbo != 0) {
            lua_pushboolean(state, 1);
            return 1;
        }

        // Destroy existing shadow map if resolution changed.
        if (sm->fbo != 0) {
            ffe::destroyShadowMap(*sm);
        }

        *sm = ffe::createShadowMap(size);
        if (sm->fbo == 0) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.enableShadows: createShadowMap(%d) failed", size);
            cfg->enabled = false;
            lua_pushboolean(state, 0);
            return 1;
        }

        cfg->enabled    = true;
        cfg->resolution = size;
        lua_pushboolean(state, 1);
        return 1;
    };
    lua_pushcfunction(L, ffe_enableShadows);
    lua_setfield(L, -2, "enableShadows");

    // ffe.disableShadows() -> nothing
    // Disable shadow mapping and free GPU resources.
    auto ffe_disableShadows = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** cfgPtr = world->registry().ctx().find<ffe::ShadowConfig*>();
        auto** mapPtr = world->registry().ctx().find<ffe::ShadowMap*>();
        if (cfgPtr == nullptr || mapPtr == nullptr) { return 0; }
        ffe::ShadowConfig* cfg = *cfgPtr;
        ffe::ShadowMap* sm     = *mapPtr;

        if (!cfg->enabled) { return 0; }

        ffe::destroyShadowMap(*sm);
        cfg->enabled = false;
        return 0;
    };
    lua_pushcfunction(L, ffe_disableShadows);
    lua_setfield(L, -2, "disableShadows");

    // ffe.setShadowBias(bias: number) -> nothing
    // Set the shadow depth bias. Clamped to [0, 0.1].
    auto ffe_setShadowBias = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** cfgPtr = world->registry().ctx().find<ffe::ShadowConfig*>();
        if (cfgPtr == nullptr) { return 0; }
        ffe::ShadowConfig* cfg = *cfgPtr;

        const float bias = static_cast<float>(luaL_optnumber(state, 1, 0.005));
        if (!std::isfinite(bias)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setShadowBias: non-finite value rejected");
            return 0;
        }
        cfg->bias = std::clamp(bias, 0.0f, 0.1f);
        return 0;
    };
    lua_pushcfunction(L, ffe_setShadowBias);
    lua_setfield(L, -2, "setShadowBias");

    // ffe.setShadowArea(width, height, near, far: number) -> nothing
    // Configure the orthographic frustum for the shadow light camera.
    // All values must be positive, finite, and near < far.
    auto ffe_setShadowArea = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** cfgPtr = world->registry().ctx().find<ffe::ShadowConfig*>();
        if (cfgPtr == nullptr) { return 0; }
        ffe::ShadowConfig* cfg = *cfgPtr;

        const float width  = static_cast<float>(luaL_optnumber(state, 1, 20.0));
        const float height = static_cast<float>(luaL_optnumber(state, 2, 20.0));
        const float nearP  = static_cast<float>(luaL_optnumber(state, 3, 0.1));
        const float farP   = static_cast<float>(luaL_optnumber(state, 4, 50.0));

        if (!std::isfinite(width) || !std::isfinite(height) ||
            !std::isfinite(nearP) || !std::isfinite(farP)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setShadowArea: non-finite value rejected");
            return 0;
        }
        if (width <= 0.0f || height <= 0.0f || nearP <= 0.0f || farP <= 0.0f) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setShadowArea: all values must be > 0");
            return 0;
        }
        if (nearP >= farP) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setShadowArea: near (%.3f) must be < far (%.3f)",
                         static_cast<double>(nearP), static_cast<double>(farP));
            return 0;
        }

        cfg->areaWidth  = width;
        cfg->areaHeight = height;
        cfg->nearPlane  = nearP;
        cfg->farPlane   = farP;
        return 0;
    };
    lua_pushcfunction(L, ffe_setShadowArea);
    lua_setfield(L, -2, "setShadowArea");

    // ----------------------------------------------------------------
    // Skybox bindings
    // ----------------------------------------------------------------

    // ffe.loadSkybox(right, left, top, bottom, front, back) -> boolean
    // Load 6 face images into a cubemap texture and enable skybox rendering.
    // Face paths are relative to the asset root (same rules as loadTexture).
    // Returns true on success, false on failure.
    auto ffe_loadSkybox = [](lua_State* state) -> int {
        // All 6 arguments must be strings.
        for (int arg = 1; arg <= 6; ++arg) {
            if (lua_type(state, arg) != LUA_TSTRING) {
                FFE_LOG_ERROR("ScriptEngine",
                              "ffe.loadSkybox: argument %d must be a string (face path)", arg);
                lua_pushboolean(state, 0);
                return 1;
            }
        }

        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushboolean(state, 0); return 1; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** skyPtr = world->registry().ctx().find<ffe::renderer::SkyboxConfig*>();
        if (skyPtr == nullptr) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadSkybox: skybox context not in ECS");
            lua_pushboolean(state, 0);
            return 1;
        }
        ffe::renderer::SkyboxConfig* cfg = *skyPtr;

        // Unload existing cubemap if any.
        if (cfg->cubemapTexture != 0) {
            ffe::renderer::unloadCubemap(cfg->cubemapTexture);
            cfg->cubemapTexture = 0;
            cfg->enabled = false;
        }

        // Collect 6 face paths.
        std::array<std::string, 6> facePaths;
        for (int arg = 1; arg <= 6; ++arg) {
            facePaths[static_cast<std::size_t>(arg - 1)] = lua_tostring(state, arg);
        }

        const ffe::u32 texId = ffe::renderer::loadCubemap(facePaths);
        if (texId == 0) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadSkybox: loadCubemap failed");
            lua_pushboolean(state, 0);
            return 1;
        }

        cfg->cubemapTexture = texId;
        cfg->enabled = true;
        lua_pushboolean(state, 1);
        return 1;
    };
    lua_pushcfunction(L, ffe_loadSkybox);
    lua_setfield(L, -2, "loadSkybox");

    // ffe.unloadSkybox() -> nothing
    // Delete the cubemap texture and disable skybox rendering.
    auto ffe_unloadSkybox = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** skyPtr = world->registry().ctx().find<ffe::renderer::SkyboxConfig*>();
        if (skyPtr == nullptr) { return 0; }
        ffe::renderer::SkyboxConfig* cfg = *skyPtr;

        if (cfg->cubemapTexture != 0) {
            ffe::renderer::unloadCubemap(cfg->cubemapTexture);
            cfg->cubemapTexture = 0;
        }
        cfg->enabled = false;
        return 0;
    };
    lua_pushcfunction(L, ffe_unloadSkybox);
    lua_setfield(L, -2, "unloadSkybox");

    // ffe.setSkyboxEnabled(enabled: boolean) -> nothing
    // Toggle skybox rendering without unloading the cubemap texture.
    auto ffe_setSkyboxEnabled = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** skyPtr = world->registry().ctx().find<ffe::renderer::SkyboxConfig*>();
        if (skyPtr == nullptr) { return 0; }
        ffe::renderer::SkyboxConfig* cfg = *skyPtr;

        const bool enabled = lua_toboolean(state, 1) != 0;
        cfg->enabled = enabled;
        return 0;
    };
    lua_pushcfunction(L, ffe_setSkyboxEnabled);
    lua_setfield(L, -2, "setSkyboxEnabled");

    // ----------------------------------------------------------------
    // Fog bindings
    // ----------------------------------------------------------------

    // ffe.setFog(r, g, b, near, far) -> nothing
    // Enable linear fog with the given color and distance range.
    // Color components are clamped to [0, 1]. near must be < far, both > 0.
    auto ffe_setFog = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** fogPtr = world->registry().ctx().find<ffe::renderer::FogParams*>();
        if (fogPtr == nullptr) { return 0; }
        ffe::renderer::FogParams* fog = *fogPtr;

        const float r    = static_cast<float>(luaL_optnumber(state, 1, 0.7));
        const float g    = static_cast<float>(luaL_optnumber(state, 2, 0.7));
        const float b    = static_cast<float>(luaL_optnumber(state, 3, 0.8));
        const float near = static_cast<float>(luaL_optnumber(state, 4, 10.0));
        const float far  = static_cast<float>(luaL_optnumber(state, 5, 100.0));

        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) ||
            !std::isfinite(near) || !std::isfinite(far)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setFog: non-finite value rejected");
            return 0;
        }
        if (near <= 0.0f || far <= 0.0f) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setFog: near and far must be > 0");
            return 0;
        }
        if (near >= far) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.setFog: near (%.3f) must be < far (%.3f)",
                         static_cast<double>(near), static_cast<double>(far));
            return 0;
        }

        fog->r        = std::clamp(r, 0.0f, 1.0f);
        fog->g        = std::clamp(g, 0.0f, 1.0f);
        fog->b        = std::clamp(b, 0.0f, 1.0f);
        fog->nearDist = near;
        fog->farDist  = far;
        fog->enabled  = true;
        return 0;
    };
    lua_pushcfunction(L, ffe_setFog);
    lua_setfield(L, -2, "setFog");

    // ffe.disableFog() -> nothing
    // Disable fog rendering.
    auto ffe_disableFog = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto** fogPtr = world->registry().ctx().find<ffe::renderer::FogParams*>();
        if (fogPtr == nullptr) { return 0; }
        (*fogPtr)->enabled = false;
        return 0;
    };
    lua_pushcfunction(L, ffe_disableFog);
    lua_setfield(L, -2, "disableFog");

    // ----------------------------------------------------------------
    // Post-processing bindings
    // ----------------------------------------------------------------

    // ffe.enableBloom(threshold: number, intensity: number) -> nothing
    // Enable bloom with the given threshold and intensity.
    // threshold is the luminance cutoff for bright-pass extraction.
    // intensity is the additive blend strength. Both must be finite and > 0.
    auto ffe_enableBloom = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const float threshold = static_cast<float>(luaL_optnumber(state, 1, 1.0));
        const float intensity = static_cast<float>(luaL_optnumber(state, 2, 0.5));

        if (!std::isfinite(threshold) || !std::isfinite(intensity)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.enableBloom: non-finite value rejected");
            return 0;
        }

        auto* cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        if (cfg == nullptr) {
            world->registry().ctx().emplace<ffe::renderer::PostProcessConfig>();
            cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        }

        cfg->bloomEnabled   = true;
        cfg->bloomThreshold = std::max(threshold, 0.0f);
        cfg->bloomIntensity = std::max(intensity, 0.0f);
        return 0;
    };
    lua_pushcfunction(L, ffe_enableBloom);
    lua_setfield(L, -2, "enableBloom");

    // ffe.disableBloom() -> nothing
    // Set bloomEnabled to false on the PostProcessConfig. No-op if no config exists.
    auto ffe_disableBloom = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        if (cfg == nullptr) { return 0; }
        cfg->bloomEnabled = false;
        return 0;
    };
    lua_pushcfunction(L, ffe_disableBloom);
    lua_setfield(L, -2, "disableBloom");

    // ffe.setToneMapping(mode: int) -> nothing
    // Set the tone mapping mode: 0 = none, 1 = Reinhard, 2 = ACES.
    // When mode > 0, gammaEnabled is also set to true (tone mapping without
    // gamma correction produces washed-out results).
    auto ffe_setToneMapping = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const int mode = static_cast<int>(luaL_optinteger(state, 1, 0));
        if (mode < 0 || mode > 2) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.setToneMapping: mode must be 0, 1, or 2 (got %d)", mode);
            return 0;
        }

        auto* cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        if (cfg == nullptr) {
            world->registry().ctx().emplace<ffe::renderer::PostProcessConfig>();
            cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        }

        cfg->toneMapper = mode;
        if (mode > 0) {
            cfg->gammaEnabled = true;
        }
        return 0;
    };
    lua_pushcfunction(L, ffe_setToneMapping);
    lua_setfield(L, -2, "setToneMapping");

    // ffe.setGammaCorrection(enabled: boolean) -> nothing
    // Toggle gamma correction on or off. No-op if no PostProcessConfig exists.
    auto ffe_setGammaCorrection = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        if (cfg == nullptr) { return 0; }

        const bool enabled = lua_toboolean(state, 1) != 0;
        cfg->gammaEnabled = enabled;
        return 0;
    };
    lua_pushcfunction(L, ffe_setGammaCorrection);
    lua_setfield(L, -2, "setGammaCorrection");

    // ffe.enablePostProcessing() -> nothing
    // Ensure a PostProcessConfig exists in the ECS context. If one already
    // exists, this is a no-op. Creates with default values if not present.
    auto ffe_enablePostProcessing = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        if (cfg == nullptr) {
            world->registry().ctx().emplace<ffe::renderer::PostProcessConfig>();
        }
        return 0;
    };
    lua_pushcfunction(L, ffe_enablePostProcessing);
    lua_setfield(L, -2, "enablePostProcessing");

    // ffe.disablePostProcessing() -> nothing
    // Remove PostProcessConfig from ECS context entirely. The renderer will
    // skip the post-processing pass when no config exists.
    auto ffe_disablePostProcessing = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        if (cfg != nullptr) {
            world->registry().ctx().erase<ffe::renderer::PostProcessConfig>();
        }
        return 0;
    };
    lua_pushcfunction(L, ffe_disablePostProcessing);
    lua_setfield(L, -2, "disablePostProcessing");

    // ffe.setAntiAliasing(mode: int) -> nothing
    // Set anti-aliasing mode: 0 = none, 1 = MSAA, 2 = FXAA.
    // Creates a PostProcessConfig if one does not exist.
    // When switching to MSAA (mode 1), the MSAA FBO is created/updated
    // via updateAntiAliasingConfig. MSAA sample count defaults to 2 if
    // not previously set via ffe.setMSAASamples.
    auto ffe_setAntiAliasing = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const int mode = static_cast<int>(luaL_optinteger(state, 1, 0));
        if (mode < 0 || mode > 2) {
            FFE_LOG_WARN("ScriptEngine",
                         "ffe.setAntiAliasing: mode must be 0, 1, or 2 (got %d)", mode);
            return 0;
        }

        auto* cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        if (cfg == nullptr) {
            world->registry().ctx().emplace<ffe::renderer::PostProcessConfig>();
            cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        }

        cfg->aaMode = mode;
        ffe::renderer::updateAntiAliasingConfig(*cfg);
        return 0;
    };
    lua_pushcfunction(L, ffe_setAntiAliasing);
    lua_setfield(L, -2, "setAntiAliasing");

    // ffe.setMSAASamples(count: int) -> nothing
    // Set MSAA sample count: 2, 4, or 8. Clamped to nearest valid.
    // Creates a PostProcessConfig if one does not exist.
    // Takes effect immediately if aaMode is currently 1 (MSAA).
    auto ffe_setMSAASamples = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const int count = static_cast<int>(luaL_optinteger(state, 1, 2));

        auto* cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        if (cfg == nullptr) {
            world->registry().ctx().emplace<ffe::renderer::PostProcessConfig>();
            cfg = world->registry().ctx().find<ffe::renderer::PostProcessConfig>();
        }

        cfg->msaaSamples = ffe::renderer::clampMsaaSamples(count);
        if (cfg->aaMode == 1) {
            ffe::renderer::updateAntiAliasingConfig(*cfg);
        }
        return 0;
    };
    lua_pushcfunction(L, ffe_setMSAASamples);
    lua_setfield(L, -2, "setMSAASamples");

    // ----------------------------------------------------------------
    // SSAO bindings
    // ----------------------------------------------------------------

    // ffe.enableSSAO(radius: number, bias: number, samples: int) -> nothing
    // Enable screen-space ambient occlusion.
    // radius: hemisphere sample radius in view space (default 0.5).
    // bias: depth bias to prevent self-occlusion (default 0.025).
    // samples: sample count — 16, 32, or 64 (default 32, clamped).
    auto ffe_enableSSAO = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const float radius  = static_cast<float>(luaL_optnumber(state, 1, 0.5));
        const float bias    = static_cast<float>(luaL_optnumber(state, 2, 0.025));
        const int   samples = static_cast<int>(luaL_optinteger(state, 3, 32));

        if (!std::isfinite(radius) || !std::isfinite(bias)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.enableSSAO: non-finite value rejected");
            return 0;
        }

        auto* cfg = world->registry().ctx().find<ffe::renderer::SSAOConfig>();
        if (cfg == nullptr) {
            world->registry().ctx().emplace<ffe::renderer::SSAOConfig>();
            cfg = world->registry().ctx().find<ffe::renderer::SSAOConfig>();
        }

        cfg->enabled     = true;
        cfg->radius      = ffe::renderer::clampSSAORadius(radius);
        cfg->bias        = ffe::renderer::clampSSAOBias(bias);
        cfg->sampleCount = ffe::renderer::clampSSAOSamples(samples);
        return 0;
    };
    lua_pushcfunction(L, ffe_enableSSAO);
    lua_setfield(L, -2, "enableSSAO");

    // ffe.disableSSAO() -> nothing
    // Disable screen-space ambient occlusion.
    auto ffe_disableSSAO = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        auto* cfg = world->registry().ctx().find<ffe::renderer::SSAOConfig>();
        if (cfg != nullptr) {
            cfg->enabled = false;
        }
        return 0;
    };
    lua_pushcfunction(L, ffe_disableSSAO);
    lua_setfield(L, -2, "disableSSAO");

    // ffe.setSSAOIntensity(intensity: number) -> nothing
    // Set the SSAO darkening multiplier (default 1.0).
    // Higher values produce stronger ambient occlusion.
    auto ffe_setSSAOIntensity = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const float intensity = static_cast<float>(luaL_optnumber(state, 1, 1.0));
        if (!std::isfinite(intensity)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setSSAOIntensity: non-finite value rejected");
            return 0;
        }

        auto* cfg = world->registry().ctx().find<ffe::renderer::SSAOConfig>();
        if (cfg == nullptr) { return 0; }

        cfg->intensity = ffe::renderer::clampSSAOIntensity(intensity);
        return 0;
    };
    lua_pushcfunction(L, ffe_setSSAOIntensity);
    lua_setfield(L, -2, "setSSAOIntensity");

    // ----------------------------------------------------------------
    // Screenshot binding
    // ----------------------------------------------------------------

    // ffe.screenshot(filename: string) -> boolean
    // Capture the current framebuffer to a PNG file.
    //
    // filename: destination filename, e.g. "shot.png". Saved to screenshots/
    //           under the current working directory. The screenshots/ directory
    //           is created if it does not exist.
    //
    // Returns true on success, false on failure (headless mode, bad path,
    // write error). Call this after all draw calls in the same frame, before
    // the frame ends. Never call this per-frame in production — it allocates
    // heap memory and calls glReadPixels (cold path only).
    //
    // Uses named-variable lambda to avoid lua_pushcfunction macro expansion
    // issues (Session 38 pattern — inline lambdas break with some compilers).
    auto ffe_screenshot = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.screenshot: argument 1 must be a string (filename)");
            lua_pushboolean(state, 0);
            return 1;
        }

        const char* filename = lua_tostring(state, 1);
        if (filename == nullptr || filename[0] == '\0') {
            FFE_LOG_ERROR("ScriptEngine", "ffe.screenshot: filename is null or empty");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Path safety: filename must pass the same isPathSafe check as other
        // file-writing operations (no traversal, no absolute paths, no ADS).
        if (!isPathSafe(filename)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.screenshot: unsafe filename rejected: \"%s\"", filename);
            lua_pushboolean(state, 0);
            return 1;
        }

        // Build the screenshots/ output directory and full output path.
        // Use std::filesystem to create the directory; then snprintf the full path.
        {
            std::error_code ec;
            std::filesystem::create_directories("screenshots", ec);
            if (ec) {
                FFE_LOG_ERROR("ScriptEngine",
                              "ffe.screenshot: failed to create screenshots/ directory: %s",
                              ec.message().c_str());
                lua_pushboolean(state, 0);
                return 1;
            }
        }

        char fullPath[PATH_MAX];
        const int pathLen = std::snprintf(fullPath, sizeof(fullPath), "screenshots/%s", filename);
        if (pathLen < 0 || static_cast<std::size_t>(pathLen) >= sizeof(fullPath)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.screenshot: output path too long");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Get current framebuffer dimensions from the RHI.
        const int w = ffe::rhi::getViewportWidth();
        const int h = ffe::rhi::getViewportHeight();

        if (w <= 0 || h <= 0) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.screenshot: invalid viewport dimensions %dx%d — "
                          "RHI not initialised?", w, h);
            lua_pushboolean(state, 0);
            return 1;
        }

        const bool ok = ffe::renderer::captureFramebuffer(fullPath, w, h);
        lua_pushboolean(state, ok ? 1 : 0);
        return 1;
    };
    lua_pushcfunction(L, ffe_screenshot);
    lua_setfield(L, -2, "screenshot");

    // ----------------------------------------------------------------
    // 3D Skeletal Animation bindings
    // ----------------------------------------------------------------

    // ffe.playAnimation3D(entityId: integer, animIndex: integer, loop: boolean) -> nothing
    // Start playing animation animIndex (0-based) on the entity.
    // loop defaults to true. Adds AnimationState component if not present.
    auto ffe_playAnimation3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.playAnimation3D: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.playAnimation3D: invalid entity ID");
            return 0;
        }

        // Check entity has a Mesh component with skeleton data
        const auto* meshComp = world->registry().try_get<ffe::Mesh>(static_cast<entt::entity>(entityId));
        if (meshComp == nullptr) {
            FFE_LOG_WARN("ScriptEngine", "ffe.playAnimation3D: entity has no Mesh component");
            return 0;
        }
        const ffe::renderer::MeshGpuRecord* rec = ffe::renderer::getMeshGpuRecord(meshComp->meshHandle);
        if (rec == nullptr || !rec->hasSkeleton) {
            FFE_LOG_WARN("ScriptEngine", "ffe.playAnimation3D: mesh has no skeleton data");
            return 0;
        }

        const lua_Integer rawAnim = luaL_optinteger(state, 2, 0);
        const ffe::u32 animIndex = (rawAnim >= 0) ? static_cast<ffe::u32>(rawAnim) : 0;
        const bool loop = lua_isboolean(state, 3) ? (lua_toboolean(state, 3) != 0) : true;

        // Validate animation index
        const ffe::renderer::MeshAnimations* anims = ffe::renderer::getMeshAnimations(meshComp->meshHandle);
        if (anims == nullptr || animIndex >= anims->clipCount) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.playAnimation3D: animIndex %u out of range (max %u)",
                          animIndex, anims ? anims->clipCount : 0u);
            return 0;
        }

        // Ensure Skeleton component exists
        if (!world->registry().all_of<ffe::Skeleton>(static_cast<entt::entity>(entityId))) {
            auto& skel = world->registry().emplace<ffe::Skeleton>(static_cast<entt::entity>(entityId));
            const ffe::renderer::SkeletonData* skelData = ffe::renderer::getMeshSkeletonData(meshComp->meshHandle);
            if (skelData != nullptr) {
                skel.boneCount = skelData->boneCount;
                // Initialize bone matrices to identity
                for (ffe::u32 b = 0; b < skel.boneCount; ++b) {
                    skel.boneMatrices[b] = glm::mat4(1.0f);
                }
            }
        }

        // Set or create AnimationState
        auto& animState = world->registry().get_or_emplace<ffe::AnimationState>(
            static_cast<entt::entity>(entityId));
        animState.clipIndex = animIndex;
        animState.time      = 0.0f;
        animState.looping   = loop;
        animState.playing   = true;

        return 0;
    };
    lua_pushcfunction(L, ffe_playAnimation3D);
    lua_setfield(L, -2, "playAnimation3D");

    // ffe.stopAnimation3D(entityId: integer) -> nothing
    // Stop animation playback. Entity freezes at current pose.
    auto ffe_stopAnimation3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) { return 0; }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }

        auto* animState = world->registry().try_get<ffe::AnimationState>(
            static_cast<entt::entity>(entityId));
        if (animState != nullptr) {
            animState->playing = false;
        }
        return 0;
    };
    lua_pushcfunction(L, ffe_stopAnimation3D);
    lua_setfield(L, -2, "stopAnimation3D");

    // ffe.setAnimationSpeed3D(entityId: integer, speed: number) -> nothing
    // Set playback speed multiplier. Negative values clamped to 0.
    auto ffe_setAnimationSpeed = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) { return 0; }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }

        auto* animState = world->registry().try_get<ffe::AnimationState>(
            static_cast<entt::entity>(entityId));
        if (animState != nullptr) {
            ffe::f32 speed = static_cast<ffe::f32>(luaL_optnumber(state, 2, 1.0));
            if (speed < 0.0f) speed = 0.0f;
            animState->speed = speed;
        }
        return 0;
    };
    lua_pushcfunction(L, ffe_setAnimationSpeed);
    lua_setfield(L, -2, "setAnimationSpeed3D");

    // ffe.getAnimationProgress3D(entityId: integer) -> number
    // Returns time / duration as a float in [0.0, 1.0]. Returns 0.0 if no animation.
    auto ffe_getAnimationProgress = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            lua_pushnumber(state, 0.0);
            return 1;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushnumber(state, 0.0); return 1; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { lua_pushnumber(state, 0.0); return 1; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { lua_pushnumber(state, 0.0); return 1; }

        const auto* animState = world->registry().try_get<ffe::AnimationState>(
            static_cast<entt::entity>(entityId));
        if (animState == nullptr) { lua_pushnumber(state, 0.0); return 1; }

        const auto* meshComp = world->registry().try_get<ffe::Mesh>(static_cast<entt::entity>(entityId));
        if (meshComp == nullptr) { lua_pushnumber(state, 0.0); return 1; }

        const ffe::renderer::MeshAnimations* anims = ffe::renderer::getMeshAnimations(meshComp->meshHandle);
        if (anims == nullptr || animState->clipIndex >= anims->clipCount) {
            lua_pushnumber(state, 0.0);
            return 1;
        }

        const ffe::f32 duration = anims->clips[animState->clipIndex].duration;
        if (duration <= 0.0f) {
            lua_pushnumber(state, 0.0);
            return 1;
        }

        const double progress = static_cast<double>(animState->time / duration);
        lua_pushnumber(state, std::clamp(progress, 0.0, 1.0));
        return 1;
    };
    lua_pushcfunction(L, ffe_getAnimationProgress);
    lua_setfield(L, -2, "getAnimationProgress3D");

    // ffe.isAnimation3DPlaying(entityId: integer) -> boolean
    // Returns true if AnimationState::playing == true.
    auto ffe_isAnimation3DPlaying = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            lua_pushboolean(state, 0);
            return 1;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushboolean(state, 0); return 1; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { lua_pushboolean(state, 0); return 1; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { lua_pushboolean(state, 0); return 1; }

        const auto* animState = world->registry().try_get<ffe::AnimationState>(
            static_cast<entt::entity>(entityId));
        lua_pushboolean(state, (animState != nullptr && animState->playing) ? 1 : 0);
        return 1;
    };
    lua_pushcfunction(L, ffe_isAnimation3DPlaying);
    lua_setfield(L, -2, "isAnimation3DPlaying");

    // ffe.getAnimationCount3D(entityId: integer) -> integer
    // Returns number of animation clips for this entity's mesh.
    auto ffe_getAnimationCount = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            lua_pushinteger(state, 0);
            return 1;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushinteger(state, 0); return 1; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { lua_pushinteger(state, 0); return 1; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { lua_pushinteger(state, 0); return 1; }

        const auto* meshComp = world->registry().try_get<ffe::Mesh>(static_cast<entt::entity>(entityId));
        if (meshComp == nullptr) { lua_pushinteger(state, 0); return 1; }

        const ffe::renderer::MeshAnimations* anims = ffe::renderer::getMeshAnimations(meshComp->meshHandle);
        lua_pushinteger(state, (anims != nullptr) ? static_cast<lua_Integer>(anims->clipCount) : 0);
        return 1;
    };
    lua_pushcfunction(L, ffe_getAnimationCount);
    lua_setfield(L, -2, "getAnimationCount3D");

    // ffe.crossfadeAnimation3D(entityId: integer, clipIndex: integer, duration: number) -> nothing
    // Initiates a crossfade blend from the current animation to clipIndex over duration seconds.
    // Validates entity has AnimationState, clipIndex is valid, duration > 0.
    auto ffe_crossfadeAnimation3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.crossfadeAnimation3D: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.crossfadeAnimation3D: invalid entity ID");
            return 0;
        }

        // Entity must have AnimationState
        auto* animState = world->registry().try_get<ffe::AnimationState>(static_cast<entt::entity>(entityId));
        if (animState == nullptr) {
            FFE_LOG_WARN("ScriptEngine", "ffe.crossfadeAnimation3D: entity has no AnimationState");
            return 0;
        }

        // clipIndex argument
        if (lua_type(state, 2) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.crossfadeAnimation3D: argument 2 (clipIndex) must be a number");
            return 0;
        }
        const lua_Integer rawClip = lua_tointeger(state, 2);
        if (rawClip < 0) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.crossfadeAnimation3D: clipIndex must be >= 0");
            return 0;
        }
        const ffe::u32 clipIndex = static_cast<ffe::u32>(rawClip);

        // Validate clipIndex against mesh animation count
        const auto* meshComp = world->registry().try_get<ffe::Mesh>(static_cast<entt::entity>(entityId));
        if (meshComp != nullptr) {
            const ffe::renderer::MeshAnimations* anims = ffe::renderer::getMeshAnimations(meshComp->meshHandle);
            if (anims != nullptr && clipIndex >= anims->clipCount) {
                FFE_LOG_ERROR("ScriptEngine", "ffe.crossfadeAnimation3D: clipIndex %u out of range (max %u)",
                              clipIndex, anims->clipCount);
                return 0;
            }
        }

        // duration argument
        if (lua_type(state, 3) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.crossfadeAnimation3D: argument 3 (duration) must be a number");
            return 0;
        }
        const double duration = lua_tonumber(state, 3);
        if (duration <= 0.0) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.crossfadeAnimation3D: duration must be > 0");
            return 0;
        }

        // Set up crossfade blend
        animState->blending      = true;
        animState->blendFromClip = static_cast<ffe::i32>(animState->clipIndex);
        animState->blendFromTime = animState->time;
        animState->blendDuration = static_cast<ffe::f32>(duration);
        animState->blendElapsed  = 0.0f;
        animState->clipIndex     = clipIndex;
        animState->time          = 0.0f;

        return 0;
    };
    lua_pushcfunction(L, ffe_crossfadeAnimation3D);
    lua_setfield(L, -2, "crossfadeAnimation3D");

    // ffe.setRootMotion3D(entityId: integer, enabled: boolean) -> nothing
    // If enabled=true, emplaces a RootMotionDelta component with enabled=true.
    // If false, sets enabled=false or removes the component.
    auto ffe_setRootMotion3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.setRootMotion3D: argument 1 (entityId) must be a number");
            return 0;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setRootMotion3D: invalid entity ID");
            return 0;
        }

        const bool enabled = lua_toboolean(state, 2) != 0;
        if (enabled) {
            auto& rm = world->registry().get_or_emplace<ffe::RootMotionDelta>(
                static_cast<entt::entity>(entityId));
            rm.enabled = true;
        } else {
            auto* rm = world->registry().try_get<ffe::RootMotionDelta>(
                static_cast<entt::entity>(entityId));
            if (rm != nullptr) {
                rm->enabled = false;
            }
        }

        return 0;
    };
    lua_pushcfunction(L, ffe_setRootMotion3D);
    lua_setfield(L, -2, "setRootMotion3D");

    // ffe.getRootMotionDelta3D(entityId: integer) -> dx, dy, dz
    // Returns 3 numbers from the RootMotionDelta component.
    // Returns 0,0,0 if component doesn't exist or isn't enabled.
    auto ffe_getRootMotionDelta3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            return 3;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            return 3;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            return 3;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            return 3;
        }

        const auto* rm = world->registry().try_get<ffe::RootMotionDelta>(
            static_cast<entt::entity>(entityId));
        if (rm == nullptr || !rm->enabled) {
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            lua_pushnumber(state, 0.0);
            return 3;
        }

        lua_pushnumber(state, static_cast<double>(rm->translationDelta.x));
        lua_pushnumber(state, static_cast<double>(rm->translationDelta.y));
        lua_pushnumber(state, static_cast<double>(rm->translationDelta.z));
        return 3;
    };
    lua_pushcfunction(L, ffe_getRootMotionDelta3D);
    lua_setfield(L, -2, "getRootMotionDelta3D");

    // ----------------------------------------------------------------
    // 3D physics bindings (Session 48)
    // ----------------------------------------------------------------

    // ffe.createPhysicsBody(entityId: integer, defTable: table) -> boolean
    // Parse a Lua table describing the body, create a Jolt body, attach RigidBody3D.
    // Returns true on success.
    auto ffe_createPhysicsBody = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.createPhysicsBody: argument 1 (entityId) must be a number");
            lua_pushboolean(state, 0);
            return 1;
        }
        if (lua_type(state, 2) != LUA_TTABLE) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.createPhysicsBody: argument 2 must be a table");
            lua_pushboolean(state, 0);
            return 1;
        }

        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushboolean(state, 0); return 1; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { lua_pushboolean(state, 0); return 1; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.createPhysicsBody: invalid entity ID %" PRId64,
                          static_cast<long long>(rawId));
            lua_pushboolean(state, 0);
            return 1;
        }

        // Parse the definition table
        ffe::physics::BodyDef3D def;

        // shape = "box" | "sphere" | "capsule"
        lua_getfield(state, 2, "shape");
        if (lua_type(state, -1) == LUA_TSTRING) {
            const char* shapeStr = lua_tostring(state, -1);
            if (shapeStr[0] == 's' || shapeStr[0] == 'S') {
                def.shapeType = ffe::physics::ShapeType3D::SPHERE;
            } else if (shapeStr[0] == 'c' || shapeStr[0] == 'C') {
                def.shapeType = ffe::physics::ShapeType3D::CAPSULE;
            } else {
                def.shapeType = ffe::physics::ShapeType3D::BOX;
            }
        }
        lua_pop(state, 1);

        // motion = "static" | "kinematic" | "dynamic"
        lua_getfield(state, 2, "motion");
        if (lua_type(state, -1) == LUA_TSTRING) {
            const char* motionStr = lua_tostring(state, -1);
            if (motionStr[0] == 's' || motionStr[0] == 'S') {
                def.motionType = ffe::physics::MotionType3D::STATIC;
            } else if (motionStr[0] == 'k' || motionStr[0] == 'K') {
                def.motionType = ffe::physics::MotionType3D::KINEMATIC;
            } else {
                def.motionType = ffe::physics::MotionType3D::DYNAMIC;
            }
        }
        lua_pop(state, 1);

        // halfExtents = {x, y, z}
        lua_getfield(state, 2, "halfExtents");
        if (lua_type(state, -1) == LUA_TTABLE) {
            lua_rawgeti(state, -1, 1); def.halfExtents.x = static_cast<ffe::f32>(luaL_optnumber(state, -1, 0.5)); lua_pop(state, 1);
            lua_rawgeti(state, -1, 2); def.halfExtents.y = static_cast<ffe::f32>(luaL_optnumber(state, -1, 0.5)); lua_pop(state, 1);
            lua_rawgeti(state, -1, 3); def.halfExtents.z = static_cast<ffe::f32>(luaL_optnumber(state, -1, 0.5)); lua_pop(state, 1);
        }
        lua_pop(state, 1);

        // Scalar fields
        lua_getfield(state, 2, "radius");
        if (lua_type(state, -1) == LUA_TNUMBER) { def.radius = static_cast<ffe::f32>(lua_tonumber(state, -1)); }
        lua_pop(state, 1);

        lua_getfield(state, 2, "height");
        if (lua_type(state, -1) == LUA_TNUMBER) { def.height = static_cast<ffe::f32>(lua_tonumber(state, -1)); }
        lua_pop(state, 1);

        lua_getfield(state, 2, "mass");
        if (lua_type(state, -1) == LUA_TNUMBER) { def.mass = static_cast<ffe::f32>(lua_tonumber(state, -1)); }
        lua_pop(state, 1);

        lua_getfield(state, 2, "friction");
        if (lua_type(state, -1) == LUA_TNUMBER) { def.friction = static_cast<ffe::f32>(lua_tonumber(state, -1)); }
        lua_pop(state, 1);

        lua_getfield(state, 2, "restitution");
        if (lua_type(state, -1) == LUA_TNUMBER) { def.restitution = static_cast<ffe::f32>(lua_tonumber(state, -1)); }
        lua_pop(state, 1);

        // Use the entity's Transform3D position if it exists
        const auto* t3d = world->registry().try_get<ffe::Transform3D>(
            static_cast<entt::entity>(entityId));
        if (t3d != nullptr) {
            def.position = t3d->position;
            def.rotation = t3d->rotation;
        }

        // NaN/Inf guard — all floats are sanitised inside createBody()
        // Pass entityId so collision events report correct ECS entity IDs.
        const ffe::physics::BodyHandle3D handle = ffe::physics::createBody(def, static_cast<ffe::u32>(entityId));
        if (!ffe::physics::isValid(handle)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.createPhysicsBody: body creation failed");
            lua_pushboolean(state, 0);
            return 1;
        }

        // Attach RigidBody3D component
        ffe::RigidBody3D rb;
        rb.handle      = handle;
        rb.initialized = true;
        world->registry().emplace_or_replace<ffe::RigidBody3D>(
            static_cast<entt::entity>(entityId), rb);

        lua_pushboolean(state, 1);
        return 1;
    };
    lua_pushcfunction(L, ffe_createPhysicsBody);
    lua_setfield(L, -2, "createPhysicsBody");

    // ffe.destroyPhysicsBody(entityId: integer) -> nothing
    auto ffe_destroyPhysicsBody = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) { return 0; }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }

        auto* rb = world->registry().try_get<ffe::RigidBody3D>(
            static_cast<entt::entity>(entityId));
        if (rb != nullptr && rb->initialized) {
            ffe::physics::destroyBody(rb->handle);
            rb->handle = ffe::physics::BodyHandle3D{};
            rb->initialized = false;
        }
        world->registry().remove<ffe::RigidBody3D>(static_cast<entt::entity>(entityId));
        return 0;
    };
    lua_pushcfunction(L, ffe_destroyPhysicsBody);
    lua_setfield(L, -2, "destroyPhysicsBody");

    // ffe.applyForce(entityId, fx, fy, fz) -> nothing
    auto ffe_applyForce3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) { return 0; }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }

        const auto* rb = world->registry().try_get<ffe::RigidBody3D>(
            static_cast<entt::entity>(entityId));
        if (rb == nullptr || !rb->initialized) { return 0; }

        const ffe::f32 fx = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
        const ffe::f32 fy = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
        const ffe::f32 fz = static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0));
        if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.applyForce: non-finite input ignored");
            return 0;
        }
        ffe::physics::applyForce(rb->handle, {fx, fy, fz});
        return 0;
    };
    lua_pushcfunction(L, ffe_applyForce3D);
    lua_setfield(L, -2, "applyForce");

    // ffe.applyImpulse(entityId, ix, iy, iz) -> nothing
    auto ffe_applyImpulse3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) { return 0; }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }

        const auto* rb = world->registry().try_get<ffe::RigidBody3D>(
            static_cast<entt::entity>(entityId));
        if (rb == nullptr || !rb->initialized) { return 0; }

        const ffe::f32 ix = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
        const ffe::f32 iy = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
        const ffe::f32 iz = static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0));
        if (!std::isfinite(ix) || !std::isfinite(iy) || !std::isfinite(iz)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.applyImpulse: non-finite input ignored");
            return 0;
        }
        ffe::physics::applyImpulse(rb->handle, {ix, iy, iz});
        return 0;
    };
    lua_pushcfunction(L, ffe_applyImpulse3D);
    lua_setfield(L, -2, "applyImpulse");

    // ffe.setLinearVelocity(entityId, vx, vy, vz) -> nothing
    auto ffe_setLinearVelocity3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) { return 0; }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) { return 0; }

        const auto* rb = world->registry().try_get<ffe::RigidBody3D>(
            static_cast<entt::entity>(entityId));
        if (rb == nullptr || !rb->initialized) { return 0; }

        const ffe::f32 vx = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
        const ffe::f32 vy = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
        const ffe::f32 vz = static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0));
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setLinearVelocity: non-finite input ignored");
            return 0;
        }
        ffe::physics::setLinearVelocity(rb->handle, {vx, vy, vz});
        return 0;
    };
    lua_pushcfunction(L, ffe_setLinearVelocity3D);
    lua_setfield(L, -2, "setLinearVelocity");

    // ffe.getLinearVelocity(entityId) -> vx, vy, vz
    auto ffe_getLinearVelocity3D = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            lua_pushnumber(state, 0); lua_pushnumber(state, 0); lua_pushnumber(state, 0);
            return 3;
        }
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_pushnumber(state, 0); lua_pushnumber(state, 0); lua_pushnumber(state, 0);
            return 3;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        if (rawId < 0 || rawId > MAX_ENTITY_ID) {
            lua_pushnumber(state, 0); lua_pushnumber(state, 0); lua_pushnumber(state, 0);
            return 3;
        }
        const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
        if (!world->isValid(entityId)) {
            lua_pushnumber(state, 0); lua_pushnumber(state, 0); lua_pushnumber(state, 0);
            return 3;
        }

        const auto* rb = world->registry().try_get<ffe::RigidBody3D>(
            static_cast<entt::entity>(entityId));
        if (rb == nullptr || !rb->initialized) {
            lua_pushnumber(state, 0); lua_pushnumber(state, 0); lua_pushnumber(state, 0);
            return 3;
        }

        const glm::vec3 vel = ffe::physics::getLinearVelocity(rb->handle);
        lua_pushnumber(state, static_cast<double>(vel.x));
        lua_pushnumber(state, static_cast<double>(vel.y));
        lua_pushnumber(state, static_cast<double>(vel.z));
        return 3;
    };
    lua_pushcfunction(L, ffe_getLinearVelocity3D);
    lua_setfield(L, -2, "getLinearVelocity");

    // ffe.setGravity(gx, gy, gz) -> nothing
    auto ffe_setGravity3D = [](lua_State* state) -> int {
        const ffe::f32 gx = static_cast<ffe::f32>(luaL_optnumber(state, 1, 0.0));
        const ffe::f32 gy = static_cast<ffe::f32>(luaL_optnumber(state, 2, -9.81));
        const ffe::f32 gz = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
        if (!std::isfinite(gx) || !std::isfinite(gy) || !std::isfinite(gz)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setGravity: non-finite input ignored");
            return 0;
        }
        ffe::physics::setGravity({gx, gy, gz});
        return 0;
    };
    lua_pushcfunction(L, ffe_setGravity3D);
    lua_setfield(L, -2, "setGravity");

    // ffe.getGravity() -> gx, gy, gz
    auto ffe_getGravity3D = [](lua_State* state) -> int {
        const glm::vec3 g = ffe::physics::getGravity();
        lua_pushnumber(state, static_cast<double>(g.x));
        lua_pushnumber(state, static_cast<double>(g.y));
        lua_pushnumber(state, static_cast<double>(g.z));
        return 3;
    };
    lua_pushcfunction(L, ffe_getGravity3D);
    lua_setfield(L, -2, "getGravity");

    // ----------------------------------------------------------------
    // 3D collision callback bindings (Session 49)
    // ----------------------------------------------------------------

    // ffe.onCollision3D(callback) -> nothing
    // Registers a global Lua callback for 3D collision events.
    // callback(entityA, entityB, px, py, pz, nx, ny, nz, eventType)
    // eventType is "enter", "stay", or "exit".
    auto ffe_onCollision3D = [](lua_State* state) -> int {
        luaL_checktype(state, 1, LUA_TFUNCTION);

        // Release old ref if any.
        lua_pushlightuserdata(state, &s_collision3DCallbackKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { // LUA_NOREF
                luaL_unref(state, LUA_REGISTRYINDEX, oldRef);
            }
        }
        lua_pop(state, 1);

        // Create a new reference to the callback function.
        lua_pushvalue(state, 1);
        const int ref = luaL_ref(state, LUA_REGISTRYINDEX);

        // Store the ref integer in the registry under our key.
        lua_pushlightuserdata(state, &s_collision3DCallbackKey);
        lua_pushinteger(state, static_cast<lua_Integer>(ref));
        lua_settable(state, LUA_REGISTRYINDEX);

        return 0;
    };
    lua_pushcfunction(L, ffe_onCollision3D);
    lua_setfield(L, -2, "onCollision3D");

    // ffe.removeCollision3DCallback() -> nothing
    // Unregisters the 3D collision callback.
    auto ffe_removeCollision3DCallback = [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_collision3DCallbackKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { // LUA_NOREF
                luaL_unref(state, LUA_REGISTRYINDEX, oldRef);
            }
        }
        lua_pop(state, 1);

        // Store LUA_NOREF sentinel.
        lua_pushlightuserdata(state, &s_collision3DCallbackKey);
        lua_pushinteger(state, -2); // LUA_NOREF
        lua_settable(state, LUA_REGISTRYINDEX);

        return 0;
    };
    lua_pushcfunction(L, ffe_removeCollision3DCallback);
    lua_setfield(L, -2, "removeCollision3DCallback");

    // ffe.dispatchCollision3DEvents() -> nothing
    // Iterates getCollisionEvents3D() and calls the registered Lua callback.
    // Called once per frame from the game loop (or from Lua for testing).
    // No heap allocations — event strings are compile-time constants.
    auto ffe_dispatchCollision3DEvents = [](lua_State* state) -> int {
        // Retrieve the callback ref.
        lua_pushlightuserdata(state, &s_collision3DCallbackKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        const int ref = static_cast<int>(lua_tointeger(state, -1));
        lua_pop(state, 1);
        if (ref == -2) { return 0; } // LUA_NOREF

        ffe::u32 count = 0;
        const ffe::physics::CollisionEvent3D* events = ffe::physics::getCollisionEvents3D(count);
        if (count == 0 || events == nullptr) { return 0; }

        // Compile-time event type strings (no allocation).
        static const char* const s_eventTypeNames[] = {"enter", "stay", "exit"};

        for (ffe::u32 i = 0; i < count; ++i) {
            lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
            if (!lua_isfunction(state, -1)) {
                lua_pop(state, 1);
                // Unregister broken callback.
                luaL_unref(state, LUA_REGISTRYINDEX, ref);
                lua_pushlightuserdata(state, &s_collision3DCallbackKey);
                lua_pushinteger(state, -2); // LUA_NOREF
                lua_settable(state, LUA_REGISTRYINDEX);
                return 0;
            }

            const auto& evt = events[i];
            lua_pushinteger(state, static_cast<lua_Integer>(evt.entityA));
            lua_pushinteger(state, static_cast<lua_Integer>(evt.entityB));
            lua_pushnumber(state, static_cast<double>(evt.contactPoint.x));
            lua_pushnumber(state, static_cast<double>(evt.contactPoint.y));
            lua_pushnumber(state, static_cast<double>(evt.contactPoint.z));
            lua_pushnumber(state, static_cast<double>(evt.contactNormal.x));
            lua_pushnumber(state, static_cast<double>(evt.contactNormal.y));
            lua_pushnumber(state, static_cast<double>(evt.contactNormal.z));

            const auto typeIdx = static_cast<ffe::u8>(evt.type);
            lua_pushstring(state, s_eventTypeNames[typeIdx < 3 ? typeIdx : 0]);

            if (lua_pcall(state, 9, 0, 0) != 0) {
                const char* err = lua_tostring(state, -1);
                FFE_LOG_ERROR("ScriptEngine", "3D collision callback error: %s",
                              err != nullptr ? err : "(unknown)");
                lua_pop(state, 1);
                // Continue delivering remaining events despite error.
            }
        }
        return 0;
    };
    lua_pushcfunction(L, ffe_dispatchCollision3DEvents);
    lua_setfield(L, -2, "dispatchCollision3DEvents");

    // ----------------------------------------------------------------
    // Raycasting bindings (Session 49)
    // ----------------------------------------------------------------

    // ffe.castRay(ox, oy, oz, dx, dy, dz, maxDist) -> entity, hx, hy, hz, nx, ny, nz, dist | nil
    auto ffe_castRay = [](lua_State* state) -> int {
        const ffe::f32 ox = static_cast<ffe::f32>(luaL_checknumber(state, 1));
        const ffe::f32 oy = static_cast<ffe::f32>(luaL_checknumber(state, 2));
        const ffe::f32 oz = static_cast<ffe::f32>(luaL_checknumber(state, 3));
        const ffe::f32 dx = static_cast<ffe::f32>(luaL_checknumber(state, 4));
        const ffe::f32 dy = static_cast<ffe::f32>(luaL_checknumber(state, 5));
        const ffe::f32 dz = static_cast<ffe::f32>(luaL_checknumber(state, 6));
        const ffe::f32 maxDist = static_cast<ffe::f32>(luaL_checknumber(state, 7));

        // NaN/Inf guard
        if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz) ||
            !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz) ||
            !std::isfinite(maxDist)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.castRay: non-finite input — returning nil");
            lua_pushnil(state);
            return 1;
        }

        const ffe::physics::RayHit3D hit = ffe::physics::castRay({ox, oy, oz}, {dx, dy, dz}, maxDist);
        if (!hit.valid) {
            lua_pushnil(state);
            return 1;
        }

        lua_pushinteger(state, static_cast<lua_Integer>(hit.entityId));
        lua_pushnumber(state, static_cast<double>(hit.hitPoint.x));
        lua_pushnumber(state, static_cast<double>(hit.hitPoint.y));
        lua_pushnumber(state, static_cast<double>(hit.hitPoint.z));
        lua_pushnumber(state, static_cast<double>(hit.hitNormal.x));
        lua_pushnumber(state, static_cast<double>(hit.hitNormal.y));
        lua_pushnumber(state, static_cast<double>(hit.hitNormal.z));
        lua_pushnumber(state, static_cast<double>(hit.distance));
        return 8;
    };
    lua_pushcfunction(L, ffe_castRay);
    lua_setfield(L, -2, "castRay");

    // ffe.castRayAll(ox, oy, oz, dx, dy, dz, maxDist) -> array of tables | empty table
    // Each table: { entity=id, x=.., y=.., z=.., nx=.., ny=.., nz=.., distance=.. }
    auto ffe_castRayAll = [](lua_State* state) -> int {
        const ffe::f32 ox = static_cast<ffe::f32>(luaL_checknumber(state, 1));
        const ffe::f32 oy = static_cast<ffe::f32>(luaL_checknumber(state, 2));
        const ffe::f32 oz = static_cast<ffe::f32>(luaL_checknumber(state, 3));
        const ffe::f32 dx = static_cast<ffe::f32>(luaL_checknumber(state, 4));
        const ffe::f32 dy = static_cast<ffe::f32>(luaL_checknumber(state, 5));
        const ffe::f32 dz = static_cast<ffe::f32>(luaL_checknumber(state, 6));
        const ffe::f32 maxDist = static_cast<ffe::f32>(luaL_checknumber(state, 7));

        // NaN/Inf guard
        if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz) ||
            !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz) ||
            !std::isfinite(maxDist)) {
            FFE_LOG_WARN("ScriptEngine", "ffe.castRayAll: non-finite input — returning empty table");
            lua_newtable(state);
            return 1;
        }

        // Stack-allocated hit buffer — no heap allocation.
        ffe::physics::RayHit3D hits[ffe::physics::MAX_RAY_HITS];
        const ffe::u32 hitCount = ffe::physics::castRayAll({ox, oy, oz}, {dx, dy, dz}, maxDist,
                                                            hits, ffe::physics::MAX_RAY_HITS);

        // Create the result table.
        lua_createtable(state, static_cast<int>(hitCount), 0);

        for (ffe::u32 i = 0; i < hitCount; ++i) {
            lua_createtable(state, 0, 8); // 8 named fields

            lua_pushinteger(state, static_cast<lua_Integer>(hits[i].entityId));
            lua_setfield(state, -2, "entity");

            lua_pushnumber(state, static_cast<double>(hits[i].hitPoint.x));
            lua_setfield(state, -2, "x");
            lua_pushnumber(state, static_cast<double>(hits[i].hitPoint.y));
            lua_setfield(state, -2, "y");
            lua_pushnumber(state, static_cast<double>(hits[i].hitPoint.z));
            lua_setfield(state, -2, "z");

            lua_pushnumber(state, static_cast<double>(hits[i].hitNormal.x));
            lua_setfield(state, -2, "nx");
            lua_pushnumber(state, static_cast<double>(hits[i].hitNormal.y));
            lua_setfield(state, -2, "ny");
            lua_pushnumber(state, static_cast<double>(hits[i].hitNormal.z));
            lua_setfield(state, -2, "nz");

            lua_pushnumber(state, static_cast<double>(hits[i].distance));
            lua_setfield(state, -2, "distance");

            lua_rawseti(state, -2, static_cast<int>(i + 1)); // Lua arrays are 1-based
        }

        return 1;
    };
    lua_pushcfunction(L, ffe_castRayAll);
    lua_setfield(L, -2, "castRayAll");

    // ================================================================
    // Networking bindings — ffe.startServer, ffe.connectToServer, etc.
    // ================================================================

    // ffe.startServer(port) -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const int port = static_cast<int>(luaL_checkinteger(state, 1));
        if (port < 1 || port > 65535) {
            FFE_LOG_WARN("ScriptEngine", "ffe.startServer: invalid port %d", port);
            lua_pushboolean(state, 0);
            return 1;
        }
        const bool ok = ffe::networking::startServer(static_cast<uint16_t>(port));
        lua_pushboolean(state, ok ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "startServer");

    // ffe.stopServer() -> nil
    lua_pushcfunction(L, [](lua_State* /*state*/) -> int {
        ffe::networking::disconnectNetwork();
        return 0;
    });
    lua_setfield(L, -2, "stopServer");

    // ffe.isServer() -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushboolean(state, ffe::networking::isServer() ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isServer");

    // ffe.connectToServer(host, port) -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const char* host = luaL_checkstring(state, 1);
        const int port = static_cast<int>(luaL_checkinteger(state, 2));
        if (port < 1 || port > 65535) {
            FFE_LOG_WARN("ScriptEngine", "ffe.connectToServer: invalid port %d", port);
            lua_pushboolean(state, 0);
            return 1;
        }
        const bool ok = ffe::networking::connectToServer(host, static_cast<uint16_t>(port));
        lua_pushboolean(state, ok ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "connectToServer");

    // ffe.disconnect() -> nil
    lua_pushcfunction(L, [](lua_State* /*state*/) -> int {
        ffe::networking::disconnectNetwork();
        return 0;
    });
    lua_setfield(L, -2, "disconnect");

    // ffe.isConnected() -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushboolean(state, ffe::networking::isClient() ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isConnected");

    // ffe.getClientId() -> integer
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto* client = ffe::networking::getClient();
        if (client != nullptr) {
            lua_pushinteger(state, static_cast<lua_Integer>(client->clientId()));
        } else {
            lua_pushinteger(state, -1);
        }
        return 1;
    });
    lua_setfield(L, -2, "getClientId");

    // ffe.sendMessage(msgType, data) -> bool
    // msgType: integer [0..255], data: string
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const int msgType = static_cast<int>(luaL_checkinteger(state, 1));
        if (msgType < 0 || msgType > 255) {
            FFE_LOG_WARN("ScriptEngine", "ffe.sendMessage: msgType %d out of range [0,255]", msgType);
            lua_pushboolean(state, 0);
            return 1;
        }

        size_t dataLen = 0;
        const char* dataStr = luaL_optlstring(state, 2, "", &dataLen);

        // Truncate to MAX_PAYLOAD_SIZE - 1 (room for msgType byte in packet)
        if (dataLen > ffe::networking::MAX_PAYLOAD_SIZE - 1) {
            dataLen = ffe::networking::MAX_PAYLOAD_SIZE - 1;
        }

        const bool ok = ffe::networking::sendGameMessage(
            static_cast<uint8_t>(msgType),
            reinterpret_cast<const uint8_t*>(dataStr),
            static_cast<uint16_t>(dataLen));
        lua_pushboolean(state, ok ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "sendMessage");

    // ffe.onNetworkMessage(callback) -> nil
    // callback(senderId, msgType, data) or nil to clear
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Release previous ref if any
        lua_pushlightuserdata(state, &s_netMessageCallbackKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { // LUA_NOREF
                luaL_unref(state, LUA_REGISTRYINDEX, oldRef);
            }
        }
        lua_pop(state, 1);

        if (lua_isfunction(state, 1)) {
            lua_pushvalue(state, 1);
            const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(state, &s_netMessageCallbackKey);
            lua_pushinteger(state, static_cast<lua_Integer>(ref));
            lua_settable(state, LUA_REGISTRYINDEX);
        } else {
            lua_pushlightuserdata(state, &s_netMessageCallbackKey);
            lua_pushinteger(state, -2); // LUA_NOREF
            lua_settable(state, LUA_REGISTRYINDEX);
        }
        return 0;
    });
    lua_setfield(L, -2, "onNetworkMessage");

    // ffe.onClientConnected(callback) -> nil
    // callback(clientId) or nil to clear
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_netClientConnectedKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { luaL_unref(state, LUA_REGISTRYINDEX, oldRef); }
        }
        lua_pop(state, 1);

        if (lua_isfunction(state, 1)) {
            lua_pushvalue(state, 1);
            const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(state, &s_netClientConnectedKey);
            lua_pushinteger(state, static_cast<lua_Integer>(ref));
            lua_settable(state, LUA_REGISTRYINDEX);
        } else {
            lua_pushlightuserdata(state, &s_netClientConnectedKey);
            lua_pushinteger(state, -2);
            lua_settable(state, LUA_REGISTRYINDEX);
        }
        return 0;
    });
    lua_setfield(L, -2, "onClientConnected");

    // ffe.onClientDisconnected(callback) -> nil
    // callback(clientId) or nil to clear
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_netClientDisconnectedKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { luaL_unref(state, LUA_REGISTRYINDEX, oldRef); }
        }
        lua_pop(state, 1);

        if (lua_isfunction(state, 1)) {
            lua_pushvalue(state, 1);
            const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(state, &s_netClientDisconnectedKey);
            lua_pushinteger(state, static_cast<lua_Integer>(ref));
            lua_settable(state, LUA_REGISTRYINDEX);
        } else {
            lua_pushlightuserdata(state, &s_netClientDisconnectedKey);
            lua_pushinteger(state, -2);
            lua_settable(state, LUA_REGISTRYINDEX);
        }
        return 0;
    });
    lua_setfield(L, -2, "onClientDisconnected");

    // ffe.onConnected(callback) -> nil
    // Client-side: called when connected to server.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_netOnConnectedKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { luaL_unref(state, LUA_REGISTRYINDEX, oldRef); }
        }
        lua_pop(state, 1);

        if (lua_isfunction(state, 1)) {
            lua_pushvalue(state, 1);
            const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(state, &s_netOnConnectedKey);
            lua_pushinteger(state, static_cast<lua_Integer>(ref));
            lua_settable(state, LUA_REGISTRYINDEX);
        } else {
            lua_pushlightuserdata(state, &s_netOnConnectedKey);
            lua_pushinteger(state, -2);
            lua_settable(state, LUA_REGISTRYINDEX);
        }
        return 0;
    });
    lua_setfield(L, -2, "onConnected");

    // ffe.onDisconnected(callback) -> nil
    // Client-side: called when disconnected from server.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_netOnDisconnectedKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { luaL_unref(state, LUA_REGISTRYINDEX, oldRef); }
        }
        lua_pop(state, 1);

        if (lua_isfunction(state, 1)) {
            lua_pushvalue(state, 1);
            const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(state, &s_netOnDisconnectedKey);
            lua_pushinteger(state, static_cast<lua_Integer>(ref));
            lua_settable(state, LUA_REGISTRYINDEX);
        } else {
            lua_pushlightuserdata(state, &s_netOnDisconnectedKey);
            lua_pushinteger(state, -2);
            lua_settable(state, LUA_REGISTRYINDEX);
        }
        return 0;
    });
    lua_setfield(L, -2, "onDisconnected");

    // ffe.setNetworkTickRate(hz) -> nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const double hz = luaL_checknumber(state, 1);
        if (!std::isfinite(hz) || hz < 1.0 || hz > 120.0) {
            FFE_LOG_WARN("ScriptEngine", "ffe.setNetworkTickRate: invalid hz %.1f (clamped to [1,120])", hz);
        }
        ffe::networking::setNetworkTickRate(static_cast<float>(hz));
        return 0;
    });
    lua_setfield(L, -2, "setNetworkTickRate");

    // ================================================================
    // Prediction bindings — ffe.setLocalPlayer, ffe.sendInput, etc.
    // ================================================================

    // ffe.setLocalPlayer(entityId) -> nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto entityId = static_cast<uint32_t>(luaL_checkinteger(state, 1));
        ffe::networking::setLocalPlayer(entityId);
        return 0;
    });
    lua_setfield(L, -2, "setLocalPlayer");

    // ffe.sendInput(inputTable) -> bool
    // inputTable: {bits=number, aimX=number, aimY=number}
    // Engine fills in tickNumber and dt automatically.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        ffe::networking::InputCommand cmd;

        if (lua_istable(state, 1)) {
            lua_getfield(state, 1, "bits");
            if (lua_isnumber(state, -1)) {
                cmd.inputBits = static_cast<uint32_t>(lua_tointeger(state, -1));
            }
            lua_pop(state, 1);

            lua_getfield(state, 1, "aimX");
            if (lua_isnumber(state, -1)) {
                cmd.aimX = static_cast<float>(lua_tonumber(state, -1));
            }
            lua_pop(state, 1);

            lua_getfield(state, 1, "aimY");
            if (lua_isnumber(state, -1)) {
                cmd.aimY = static_cast<float>(lua_tonumber(state, -1));
            }
            lua_pop(state, 1);
        }

        const bool ok = ffe::networking::sendInput(cmd);
        lua_pushboolean(state, ok ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "sendInput");

    // ffe.onServerInput(callback) -> nil
    // Server-side: callback(clientId, inputTable) when input arrives.
    // inputTable: {bits, aimX, aimY, tick, dt}
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Release previous ref if any
        lua_pushlightuserdata(state, &s_netOnServerInputKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { luaL_unref(state, LUA_REGISTRYINDEX, oldRef); }
        }
        lua_pop(state, 1);

        if (lua_isfunction(state, 1)) {
            lua_pushvalue(state, 1);
            const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(state, &s_netOnServerInputKey);
            lua_pushinteger(state, static_cast<lua_Integer>(ref));
            lua_settable(state, LUA_REGISTRYINDEX);
        } else {
            lua_pushlightuserdata(state, &s_netOnServerInputKey);
            lua_pushinteger(state, -2); // LUA_NOREF
            lua_settable(state, LUA_REGISTRYINDEX);
        }
        return 0;
    });
    lua_setfield(L, -2, "onServerInput");

    // ffe.getPredictionError() -> number
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushnumber(state, static_cast<lua_Number>(ffe::networking::getPredictionError()));
        return 1;
    });
    lua_setfield(L, -2, "getPredictionError");

    // ffe.getNetworkTick() -> integer
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushinteger(state, static_cast<lua_Integer>(ffe::networking::getCurrentNetworkTick()));
        return 1;
    });
    lua_setfield(L, -2, "getNetworkTick");

    // ================================================================
    // Lag compensation bindings
    // ================================================================

    // ffe.performHitCheck(originX, originY, originZ, dirX, dirY, dirZ, maxDist, ignoreEntityId)
    // -> {hit=bool, entityId=number, x=number, y=number, z=number, distance=number} or nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const float ox = static_cast<float>(luaL_checknumber(state, 1));
        const float oy = static_cast<float>(luaL_checknumber(state, 2));
        const float oz = static_cast<float>(luaL_checknumber(state, 3));
        const float dx = static_cast<float>(luaL_checknumber(state, 4));
        const float dy = static_cast<float>(luaL_checknumber(state, 5));
        const float dz = static_cast<float>(luaL_checknumber(state, 6));
        const float maxDist = static_cast<float>(luaL_checknumber(state, 7));
        const auto ignoreId = static_cast<uint32_t>(luaL_checkinteger(state, 8));

        const auto result = ffe::networking::performHitCheck(
            ox, oy, oz, dx, dy, dz, maxDist, ignoreId);

        if (!result.hit) {
            lua_pushnil(state);
            return 1;
        }

        lua_newtable(state);
        lua_pushboolean(state, 1);
        lua_setfield(state, -2, "hit");
        lua_pushinteger(state, static_cast<lua_Integer>(result.hitEntityId));
        lua_setfield(state, -2, "entityId");
        lua_pushnumber(state, static_cast<lua_Number>(result.hitX));
        lua_setfield(state, -2, "x");
        lua_pushnumber(state, static_cast<lua_Number>(result.hitY));
        lua_setfield(state, -2, "y");
        lua_pushnumber(state, static_cast<lua_Number>(result.hitZ));
        lua_setfield(state, -2, "z");
        lua_pushnumber(state, static_cast<lua_Number>(result.distance));
        lua_setfield(state, -2, "distance");
        return 1;
    });
    lua_setfield(L, -2, "performHitCheck");

    // ffe.setLagCompensationWindow(ticks) -> nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto ticks = static_cast<uint32_t>(luaL_checkinteger(state, 1));
        ffe::networking::setLagCompensationWindow(ticks);
        return 0;
    });
    lua_setfield(L, -2, "setLagCompensationWindow");

    // ffe.onHitConfirm(callback) -> nil
    // callback(shooterEntityId, hitResult) where hitResult = {hit, entityId, x, y, z, distance}
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Release previous ref if any
        lua_pushlightuserdata(state, &s_netOnHitConfirmKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            const int oldRef = static_cast<int>(lua_tointeger(state, -1));
            if (oldRef != -2) { luaL_unref(state, LUA_REGISTRYINDEX, oldRef); }
        }
        lua_pop(state, 1);

        if (lua_isfunction(state, 1)) {
            lua_pushvalue(state, 1);
            const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(state, &s_netOnHitConfirmKey);
            lua_pushinteger(state, static_cast<lua_Integer>(ref));
            lua_settable(state, LUA_REGISTRYINDEX);
        } else {
            lua_pushlightuserdata(state, &s_netOnHitConfirmKey);
            lua_pushinteger(state, -2); // LUA_NOREF
            lua_settable(state, LUA_REGISTRYINDEX);
        }
        return 0;
    });
    lua_setfield(L, -2, "onHitConfirm");

    // ================================================================
    // Lobby / Matchmaking bindings
    // ================================================================

    // ffe.createLobby(name, maxPlayers) -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const char* name = luaL_checkstring(state, 1);
        const int maxP   = static_cast<int>(luaL_checkinteger(state, 2));
        if (maxP < 1 || maxP > static_cast<int>(ffe::networking::MAX_LOBBY_PLAYERS)) {
            lua_pushboolean(state, 0);
            return 1;
        }
        const bool ok = ffe::networking::createLobby(name, static_cast<uint32_t>(maxP));
        lua_pushboolean(state, ok ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "createLobby");

    // ffe.destroyLobby() -> nil
    lua_pushcfunction(L, [](lua_State* /*state*/) -> int {
        ffe::networking::destroyLobby();
        return 0;
    });
    lua_setfield(L, -2, "destroyLobby");

    // ffe.joinLobby(playerName) -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const char* name = luaL_checkstring(state, 1);
        const bool ok = ffe::networking::joinLobby(name);
        lua_pushboolean(state, ok ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "joinLobby");

    // ffe.leaveLobby() -> nil
    lua_pushcfunction(L, [](lua_State* /*state*/) -> int {
        ffe::networking::leaveLobby();
        return 0;
    });
    lua_setfield(L, -2, "leaveLobby");

    // ffe.setReady(bool) -> nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const bool ready = lua_toboolean(state, 1) != 0;
        ffe::networking::setReady(ready);
        return 0;
    });
    lua_setfield(L, -2, "setReady");

    // ffe.isInLobby() -> bool
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushboolean(state, ffe::networking::isInLobby() ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "isInLobby");

    // ffe.getLobbyPlayers() -> array of {id=number, name=string, ready=bool}
    lua_pushcfunction(L, [](lua_State* state) -> int {
        const auto& ls = ffe::networking::getLobbyState();
        lua_newtable(state);
        int idx = 1;
        for (uint32_t i = 0; i < ffe::networking::MAX_LOBBY_PLAYERS; ++i) {
            if (ls.players[i].connectionId == 0) { continue; }
            lua_newtable(state);
            lua_pushinteger(state, static_cast<lua_Integer>(ls.players[i].connectionId));
            lua_setfield(state, -2, "id");
            lua_pushstring(state, ls.players[i].name);
            lua_setfield(state, -2, "name");
            lua_pushboolean(state, ls.players[i].ready ? 1 : 0);
            lua_setfield(state, -2, "ready");
            lua_rawseti(state, -2, idx);
            ++idx;
        }
        return 1;
    });
    lua_setfield(L, -2, "getLobbyPlayers");

    // ffe.startLobbyGame() -> nil
    lua_pushcfunction(L, [](lua_State* /*state*/) -> int {
        ffe::networking::startLobbyGame();
        return 0;
    });
    lua_setfield(L, -2, "startLobbyGame");

    // ffe.onLobbyUpdate(callback) -> nil
    // callback(lobbyState) where lobbyState is a table with players array
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Release previous ref if any
        lua_pushlightuserdata(state, &s_netOnLobbyUpdateKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        const int oldRef = static_cast<int>(lua_tointeger(state, -1));
        lua_pop(state, 1);
        if (oldRef > 0) {
            luaL_unref(state, LUA_REGISTRYINDEX, oldRef);
        }

        if (lua_isfunction(state, 1)) {
            lua_pushvalue(state, 1);
            const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(state, &s_netOnLobbyUpdateKey);
            lua_pushinteger(state, static_cast<lua_Integer>(ref));
            lua_settable(state, LUA_REGISTRYINDEX);
        } else {
            lua_pushlightuserdata(state, &s_netOnLobbyUpdateKey);
            lua_pushinteger(state, -2); // LUA_NOREF
            lua_settable(state, LUA_REGISTRYINDEX);
        }
        return 0;
    });
    lua_setfield(L, -2, "onLobbyUpdate");

    // ffe.onGameStart(callback) -> nil
    lua_pushcfunction(L, [](lua_State* state) -> int {
        // Release previous ref if any
        lua_pushlightuserdata(state, &s_netOnGameStartKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        const int oldRef = static_cast<int>(lua_tointeger(state, -1));
        lua_pop(state, 1);
        if (oldRef > 0) {
            luaL_unref(state, LUA_REGISTRYINDEX, oldRef);
        }

        if (lua_isfunction(state, 1)) {
            lua_pushvalue(state, 1);
            const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(state, &s_netOnGameStartKey);
            lua_pushinteger(state, static_cast<lua_Integer>(ref));
            lua_settable(state, LUA_REGISTRYINDEX);
        } else {
            lua_pushlightuserdata(state, &s_netOnGameStartKey);
            lua_pushinteger(state, -2); // LUA_NOREF
            lua_settable(state, LUA_REGISTRYINDEX);
        }
        return 0;
    });
    lua_setfield(L, -2, "onGameStart");

    // ----------------------------------------------------------------
    // Terrain bindings (Phase 9 M1)
    // ----------------------------------------------------------------

    // ffe.loadTerrain(path: string, width: number, depth: number, heightScale: number) -> integer
    // Load terrain from a PNG heightmap. Returns terrain handle ID (0 on failure).
    // Cold path only — do NOT call per frame.
    auto ffe_loadTerrain = [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadTerrain: argument 1 must be a string (path)");
            lua_pushinteger(state, 0);
            return 1;
        }
        const char* const path = lua_tostring(state, 1);
        if (path == nullptr || path[0] == '\0') {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadTerrain: path is null or empty");
            lua_pushinteger(state, 0);
            return 1;
        }

        if (lua_type(state, 2) != LUA_TNUMBER ||
            lua_type(state, 3) != LUA_TNUMBER ||
            lua_type(state, 4) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadTerrain: arguments 2-4 must be numbers (width, depth, heightScale)");
            lua_pushinteger(state, 0);
            return 1;
        }

        const double width      = lua_tonumber(state, 2);
        const double depth       = lua_tonumber(state, 3);
        const double heightScale = lua_tonumber(state, 4);

        if (width <= 0.0 || depth <= 0.0 || !std::isfinite(width) ||
            !std::isfinite(depth) || !std::isfinite(heightScale)) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadTerrain: width/depth must be positive, heightScale must be finite");
            lua_pushinteger(state, 0);
            return 1;
        }

        ffe::renderer::TerrainConfig cfg;
        cfg.worldWidth      = static_cast<ffe::f32>(width);
        cfg.worldDepth      = static_cast<ffe::f32>(depth);
        cfg.heightScale     = static_cast<ffe::f32>(heightScale);
        cfg.chunkResolution = 64;
        cfg.chunkCountX     = 16;
        cfg.chunkCountZ     = 16;

        const ffe::renderer::TerrainHandle handle =
            ffe::renderer::loadTerrainFromImage(path, cfg);

        if (!ffe::renderer::isValid(handle)) {
            lua_pushinteger(state, 0);
            return 1;
        }

        // Create an ECS entity with Transform3D + Terrain components so that
        // terrainRenderSystem() can find and render this terrain.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (!lua_isnil(state, -1)) {
            auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
            lua_pop(state, 1);

            const ffe::EntityId entityId = world->createEntity();
            auto& t3d = world->registry().emplace<ffe::Transform3D>(
                static_cast<entt::entity>(entityId));
            // Centre the terrain at the world origin. The heightmap geometry is
            // baked in [0, worldWidth] x [0, worldDepth] local space (chunks
            // start at X=0, Z=0). Without this offset the terrain appears as a
            // green wedge in the sky on real GPUs because the camera looks at
            // (0,0,0) but the terrain spans only positive X/Z coordinates.
            t3d.position = glm::vec3(
                -cfg.worldWidth  * 0.5f,
                0.0f,
                -cfg.worldDepth  * 0.5f);
            t3d.scale    = glm::vec3(1.0f, 1.0f, 1.0f);

            ffe::Terrain terrainComp;
            terrainComp.terrainHandle = handle;
            world->registry().emplace<ffe::Terrain>(
                static_cast<entt::entity>(entityId), terrainComp);
        } else {
            lua_pop(state, 1);
        }

        lua_pushinteger(state, static_cast<lua_Integer>(handle.id));
        return 1;
    };
    lua_pushcfunction(L, ffe_loadTerrain);
    lua_setfield(L, -2, "loadTerrain");

    // ffe.getTerrainHeight(x: number, z: number) -> number
    // Get interpolated terrain height at world position.
    // Returns 0 if no terrain is loaded.
    auto ffe_getTerrainHeight = [](lua_State* state) -> int {
        const ffe::renderer::TerrainHandle terrain =
            ffe::renderer::getFirstActiveTerrain();

        const double x = lua_tonumber(state, 1);
        const double z = lua_tonumber(state, 2);

        const ffe::f32 height = ffe::renderer::getTerrainHeight(
            terrain, static_cast<ffe::f32>(x), static_cast<ffe::f32>(z));
        lua_pushnumber(state, static_cast<double>(height));
        return 1;
    };
    lua_pushcfunction(L, ffe_getTerrainHeight);
    lua_setfield(L, -2, "getTerrainHeight");

    // ffe.unloadTerrain() -> nothing
    // Unload all terrains and free GPU resources.
    auto ffe_unloadTerrain = [](lua_State* /*state*/) -> int {
        ffe::renderer::unloadAllTerrains();
        return 0;
    };
    lua_pushcfunction(L, ffe_unloadTerrain);
    lua_setfield(L, -2, "unloadTerrain");

    // ffe.setTerrainTexture(textureId: integer) -> nothing
    // Set the diffuse texture for the first active terrain.
    auto ffe_setTerrainTexture = [](lua_State* state) -> int {
        const ffe::renderer::TerrainHandle terrain =
            ffe::renderer::getFirstActiveTerrain();
        if (!ffe::renderer::isValid(terrain)) { return 0; }

        const lua_Integer rawId = lua_tointeger(state, 1);
        const ffe::rhi::TextureHandle tex{static_cast<ffe::u32>(rawId > 0 ? rawId : 0)};
        ffe::renderer::setTerrainTexture(terrain, tex);
        return 0;
    };
    lua_pushcfunction(L, ffe_setTerrainTexture);
    lua_setfield(L, -2, "setTerrainTexture");

    // ffe.setTerrainSplatMap(textureId: integer) -> nothing
    // Set the splat map texture for the first active terrain.
    auto ffe_setTerrainSplatMap = [](lua_State* state) -> int {
        const ffe::renderer::TerrainHandle terrain =
            ffe::renderer::getFirstActiveTerrain();
        if (!ffe::renderer::isValid(terrain)) { return 0; }

        const lua_Integer rawId = lua_tointeger(state, 1);
        const ffe::rhi::TextureHandle tex{static_cast<ffe::u32>(rawId > 0 ? rawId : 0)};
        ffe::renderer::setTerrainSplatMap(terrain, tex);
        return 0;
    };
    lua_pushcfunction(L, ffe_setTerrainSplatMap);
    lua_setfield(L, -2, "setTerrainSplatMap");

    // ffe.setTerrainLayer(layerIndex: integer, textureId: integer, uvScale: number) -> nothing
    // Set a terrain texture layer (0-3) on the first active terrain.
    auto ffe_setTerrainLayer = [](lua_State* state) -> int {
        const ffe::renderer::TerrainHandle terrain =
            ffe::renderer::getFirstActiveTerrain();
        if (!ffe::renderer::isValid(terrain)) { return 0; }

        const lua_Integer rawLayer = lua_tointeger(state, 1);
        if (rawLayer < 0 || rawLayer > 3) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.setTerrainLayer: layerIndex must be 0-3, got %lld",
                          static_cast<long long>(rawLayer));
            return 0;
        }

        const lua_Integer rawId = lua_tointeger(state, 2);
        const ffe::rhi::TextureHandle tex{static_cast<ffe::u32>(rawId > 0 ? rawId : 0)};

        const lua_Number rawScale = lua_tonumber(state, 3);
        const ffe::f32 uvScale = (rawScale > 0.0) ? static_cast<ffe::f32>(rawScale) : 16.0f;

        ffe::renderer::setTerrainLayer(terrain,
                                       static_cast<ffe::u32>(rawLayer),
                                       tex, uvScale);
        return 0;
    };
    lua_pushcfunction(L, ffe_setTerrainLayer);
    lua_setfield(L, -2, "setTerrainLayer");

    // ffe.setTerrainTriplanar(enabled: boolean, threshold: number) -> nothing
    // Enable/disable triplanar projection on the first active terrain.
    auto ffe_setTerrainTriplanar = [](lua_State* state) -> int {
        const ffe::renderer::TerrainHandle terrain =
            ffe::renderer::getFirstActiveTerrain();
        if (!ffe::renderer::isValid(terrain)) { return 0; }

        const bool enabled = lua_toboolean(state, 1) != 0;
        const lua_Number rawThreshold = lua_tonumber(state, 2);
        const ffe::f32 threshold = (rawThreshold > 0.0 && rawThreshold <= 1.0)
            ? static_cast<ffe::f32>(rawThreshold) : 0.7f;

        ffe::renderer::setTerrainTriplanar(terrain, enabled, threshold);
        return 0;
    };
    lua_pushcfunction(L, ffe_setTerrainTriplanar);
    lua_setfield(L, -2, "setTerrainTriplanar");

    // ffe.setTerrainLodDistances(lod1Distance: number, lod2Distance: number) -> nothing
    // Set LOD transition distances for the first active terrain.
    // lod1Distance: distance beyond which chunks switch from full to half resolution.
    // lod2Distance: distance beyond which chunks switch from half to quarter resolution.
    auto ffe_setTerrainLodDistances = [](lua_State* state) -> int {
        const ffe::renderer::TerrainHandle terrain =
            ffe::renderer::getFirstActiveTerrain();
        if (!ffe::renderer::isValid(terrain)) { return 0; }

        const double rawLod1 = lua_tonumber(state, 1);
        const double rawLod2 = lua_tonumber(state, 2);

        const ffe::f32 lod1 = (rawLod1 > 0.0) ? static_cast<ffe::f32>(rawLod1) : 100.0f;
        const ffe::f32 lod2 = (rawLod2 > 0.0) ? static_cast<ffe::f32>(rawLod2) : 200.0f;

        ffe::renderer::setTerrainLodDistances(terrain, lod1, lod2);
        return 0;
    };
    lua_pushcfunction(L, ffe_setTerrainLodDistances);
    lua_setfield(L, -2, "setTerrainLodDistances");

    // ffe.setTerrainStreamingRadius(radius: integer) -> nothing
    // Enable background streaming for the first active terrain.
    // radius: Chebyshev chunk-grid distance within which chunks are kept on GPU.
    // 0 = disabled (all chunks remain eagerly loaded -- default).
    // Valid range: 0 <= radius <= 32.
    auto ffe_setTerrainStreamingRadius = [](lua_State* state) -> int {
        const ffe::renderer::TerrainHandle terrain =
            ffe::renderer::getFirstActiveTerrain();
        if (!ffe::renderer::isValid(terrain)) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.setTerrainStreamingRadius: no active terrain");
            return 0;
        }

        const lua_Integer rawRadius = lua_tointeger(state, 1);
        if (rawRadius < 0 || rawRadius > 32) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.setTerrainStreamingRadius: radius must be 0-32, got %lld",
                          static_cast<long long>(rawRadius));
            return 0;
        }

        ffe::renderer::setTerrainStreamingRadius(terrain,
                                                  static_cast<int>(rawRadius));
        return 0;
    };
    lua_pushcfunction(L, ffe_setTerrainStreamingRadius);
    lua_setfield(L, -2, "setTerrainStreamingRadius");

    // ffe.getTerrainChunkCount() -> integer
    // Return the number of terrain chunks currently resident on the GPU
    // (state EAGER or LOADED). Useful for debug display and streaming monitoring.
    auto ffe_getTerrainChunkCount = [](lua_State* state) -> int {
        const ffe::renderer::TerrainHandle terrain =
            ffe::renderer::getFirstActiveTerrain();
        if (!ffe::renderer::isValid(terrain)) {
            lua_pushinteger(state, 0);
            return 1;
        }
        const int count = ffe::renderer::getTerrainLoadedChunkCount(terrain);
        lua_pushinteger(state, static_cast<lua_Integer>(count));
        return 1;
    };
    lua_pushcfunction(L, ffe_getTerrainChunkCount);
    lua_setfield(L, -2, "getTerrainChunkCount");

    // ----------------------------------------------------------------
    // Vegetation bindings (Phase 9 M5)
    // ----------------------------------------------------------------

    // ffe.addVegetationPatch(terrainHandle: integer, density: integer, textureHandle: integer) -> integer
    // Scatter grass instances across a terrain surface and upload to GPU.
    // density is clamped to [1, 256] by VegetationSystem. textureHandle == 0 uses solid green fallback.
    // Returns VegetationHandle.id on success; 0 if MAX_VEGETATION_PATCHES reached or terrain invalid.
    // Cold path only — do NOT call per frame.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushinteger(state, 0); return 1; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawTerrain = lua_tointeger(state, 1);
        const lua_Integer rawDensity = lua_tointeger(state, 2);
        const lua_Integer rawTexture = lua_tointeger(state, 3);

        if (rawTerrain <= 0) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.addVegetationPatch: terrainHandle must be > 0, got %lld",
                          static_cast<long long>(rawTerrain));
            lua_pushinteger(state, 0);
            return 1;
        }

        ffe::renderer::TerrainHandle terrain{static_cast<ffe::u32>(rawTerrain)};
        ffe::renderer::VegetationConfig cfg;
        cfg.density       = static_cast<ffe::u32>(rawDensity > 0 ? rawDensity : 1);
        cfg.textureHandle = ffe::rhi::TextureHandle{
            static_cast<ffe::u32>(rawTexture > 0 ? rawTexture : 0)};

        const ffe::renderer::VegetationHandle h =
            engine->m_vegetationSystem.addPatch(terrain, cfg);

        lua_pushinteger(state, static_cast<lua_Integer>(h.id));
        return 1;
    });
    lua_setfield(L, -2, "addVegetationPatch");

    // ffe.removeVegetationPatch(handle: integer) -> nil
    // Remove a grass patch and free its GPU resources. No-op on invalid handle.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawHandle = lua_tointeger(state, 1);
        ffe::renderer::VegetationHandle h{
            static_cast<ffe::u32>(rawHandle > 0 ? rawHandle : 0)};
        engine->m_vegetationSystem.removePatch(h);
        return 0;
    });
    lua_setfield(L, -2, "removeVegetationPatch");

    // ffe.addTree(x: number, y: number, z: number, scale: number) -> nil
    // Add a tree at world position (x, y, z) with uniform scale.
    // Caller supplies Y directly — use ffe.getTerrainHeight() first if needed.
    // Silently ignores the call if MAX_TREES (512) is reached.
    // scale <= 0 is clamped to 1.0.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const double rawX     = lua_tonumber(state, 1);
        const double rawY     = lua_tonumber(state, 2);
        const double rawZ     = lua_tonumber(state, 3);
        const double rawScale = lua_tonumber(state, 4);

        const ffe::f32 x     = static_cast<ffe::f32>(rawX);
        const ffe::f32 y     = static_cast<ffe::f32>(rawY);
        const ffe::f32 z     = static_cast<ffe::f32>(rawZ);
        const ffe::f32 scale = (rawScale > 0.0) ? static_cast<ffe::f32>(rawScale) : 1.0f;

        engine->m_vegetationSystem.addTree(x, y, z, scale);
        return 0;
    });
    lua_setfield(L, -2, "addTree");

    // ffe.clearTrees() -> nil
    // Remove all trees and reset GPU instance buffers. Does not affect grass patches.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        engine->m_vegetationSystem.clearTrees();
        return 0;
    });
    lua_setfield(L, -2, "clearTrees");

    // ----------------------------------------------------------------
    // Water bindings
    // ----------------------------------------------------------------

    // ffe.createWater(waterLevel: number) -> integer
    // Create a water entity with Water + Transform3D components.
    // Sets WaterConfig::enabled = true and waterLevel. Returns entity ID.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushinteger(state, 0); return 1; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const double rawLevel = lua_tonumber(state, 1);
        const ffe::f32 waterLevel = static_cast<ffe::f32>(rawLevel);

        // Create entity with Water + Transform3D
        const ffe::EntityId entityId = world->createEntity();
        world->registry().emplace<ffe::renderer::Water>(static_cast<entt::entity>(entityId));
        auto& t3d = world->registry().emplace<ffe::Transform3D>(static_cast<entt::entity>(entityId));
        t3d.position = glm::vec3(0.0f, waterLevel, 0.0f);
        t3d.scale    = glm::vec3(1.0f, 1.0f, 1.0f);

        // Enable water in the config
        auto& waterCfg = world->registry().ctx().get<ffe::renderer::WaterConfig>();
        waterCfg.enabled    = true;
        waterCfg.waterLevel = waterLevel;

        lua_pushinteger(state, static_cast<lua_Integer>(entityId));
        return 1;
    });
    lua_setfield(L, -2, "createWater");

    // ffe.setWaterLevel(level: number) -> nothing
    // Set the Y height of the water plane.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const double rawLevel = lua_tonumber(state, 1);
        auto& waterCfg = world->registry().ctx().get<ffe::renderer::WaterConfig>();
        waterCfg.waterLevel = static_cast<ffe::f32>(rawLevel);
        return 0;
    });
    lua_setfield(L, -2, "setWaterLevel");

    // ffe.setWaterColor(r: number, g: number, b: number, a: number) -> nothing
    // Set the shallow water color. Deep color is auto-derived (RGB * 0.3, A * 1.5 clamped).
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const ffe::f32 r = static_cast<ffe::f32>(lua_tonumber(state, 1));
        const ffe::f32 g = static_cast<ffe::f32>(lua_tonumber(state, 2));
        const ffe::f32 b = static_cast<ffe::f32>(lua_tonumber(state, 3));
        const ffe::f32 a = lua_gettop(state) >= 4
            ? static_cast<ffe::f32>(lua_tonumber(state, 4)) : 0.6f;

        auto& waterCfg = world->registry().ctx().get<ffe::renderer::WaterConfig>();
        waterCfg.shallowColor = glm::vec4(r, g, b, a);
        // Auto-derive deep color: darken RGB by 0.3, increase alpha by 1.5 (clamped)
        const ffe::f32 deepA = (a * 1.5f > 1.0f) ? 1.0f : a * 1.5f;
        waterCfg.deepColor = glm::vec4(r * 0.3f, g * 0.3f, b * 0.3f, deepA);
        return 0;
    });
    lua_setfield(L, -2, "setWaterColor");

    // ffe.setWaterDeepColor(r: number, g: number, b: number) -> nothing
    // Directly set the deep water color (advanced usage).
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const ffe::f32 r = static_cast<ffe::f32>(lua_tonumber(state, 1));
        const ffe::f32 g = static_cast<ffe::f32>(lua_tonumber(state, 2));
        const ffe::f32 b = static_cast<ffe::f32>(lua_tonumber(state, 3));

        auto& waterCfg = world->registry().ctx().get<ffe::renderer::WaterConfig>();
        waterCfg.deepColor.x = r;
        waterCfg.deepColor.y = g;
        waterCfg.deepColor.z = b;
        return 0;
    });
    lua_setfield(L, -2, "setWaterDeepColor");

    // ffe.setWaterOpacity(opacity: number) -> nothing
    // Set shallow color alpha (0.0 = invisible, 1.0 = opaque).
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const ffe::f32 opacity = static_cast<ffe::f32>(lua_tonumber(state, 1));
        auto& waterCfg = world->registry().ctx().get<ffe::renderer::WaterConfig>();
        waterCfg.shallowColor.w = opacity;
        return 0;
    });
    lua_setfield(L, -2, "setWaterOpacity");

    // ffe.setWaterWaveSpeed(speed: number) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const ffe::f32 speed = static_cast<ffe::f32>(lua_tonumber(state, 1));
        auto& waterCfg = world->registry().ctx().get<ffe::renderer::WaterConfig>();
        waterCfg.waveSpeed = speed;
        return 0;
    });
    lua_setfield(L, -2, "setWaterWaveSpeed");

    // ffe.setWaterWaveScale(scale: number) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const ffe::f32 scale = static_cast<ffe::f32>(lua_tonumber(state, 1));
        auto& waterCfg = world->registry().ctx().get<ffe::renderer::WaterConfig>();
        waterCfg.waveScale = scale;
        return 0;
    });
    lua_setfield(L, -2, "setWaterWaveScale");

    // ffe.removeWater() -> nothing
    // Destroy all Water entities and disable water rendering.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // Collect Water entity IDs first, then destroy (avoid iterator invalidation)
        std::vector<ffe::EntityId> toDestroy;
        auto waterView = world->registry().view<ffe::renderer::Water>();
        for (const auto entity : waterView) {
            toDestroy.push_back(static_cast<ffe::EntityId>(entt::to_integral(entity)));
        }
        for (const auto eid : toDestroy) {
            world->destroyEntity(eid);
        }

        // Disable water in config
        auto& waterCfg = world->registry().ctx().get<ffe::renderer::WaterConfig>();
        waterCfg.enabled = false;
        return 0;
    });
    lua_setfield(L, -2, "removeWater");

    // ----------------------------------------------------------------
    // WaterManager bindings (Phase 9 M6)
    // ----------------------------------------------------------------
    // These bindings front the WaterManager (animated water planes,
    // Fresnel blending, optional planar reflection). Each surface is
    // identified by a uint32_t handle returned from ffe.createWaterSurface.
    // All operations are cold-path only — do NOT call per frame.

    // ffe.createWaterSurface(x, y, z, width, depth) -> integer
    // Create a water plane centred at (x, y, z) with the given extents.
    // Returns WaterHandle.id on success; 0 on error (at capacity or invalid args).
    // NOTE: lambda stored in a local to avoid preprocessor misparse of braced
    //       aggregate initializers inside the lua_pushcfunction macro argument.
    {
        auto fn_createWaterSurface = [](lua_State* state) -> int {
            lua_pushlightuserdata(state, &s_engineRegistryKey);
            lua_gettable(state, LUA_REGISTRYINDEX);
            if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_pushinteger(state, 0); return 1; }
            auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
            lua_pop(state, 1);

            const double rawX     = lua_tonumber(state, 1);
            const double rawY     = lua_tonumber(state, 2);
            const double rawZ     = lua_tonumber(state, 3);
            const double rawWidth = lua_tonumber(state, 4);
            const double rawDepth = lua_tonumber(state, 5);

            // Validate: all components must be finite.
            if (!std::isfinite(rawX) || !std::isfinite(rawY) || !std::isfinite(rawZ) ||
                !std::isfinite(rawWidth) || !std::isfinite(rawDepth)) {
                FFE_LOG_ERROR("ScriptEngine",
                              "ffe.createWaterSurface: all arguments must be finite numbers");
                lua_pushinteger(state, 0);
                return 1;
            }
            // width and depth must be positive.
            if (rawWidth <= 0.0 || rawDepth <= 0.0) {
                FFE_LOG_ERROR("ScriptEngine",
                              "ffe.createWaterSurface: width and depth must be > 0, got width=%.3f depth=%.3f",
                              rawWidth, rawDepth);
                lua_pushinteger(state, 0);
                return 1;
            }

            ffe::renderer::WaterPlane plane;
            plane.x     = static_cast<ffe::f32>(rawX);
            plane.y     = static_cast<ffe::f32>(rawY);
            plane.z     = static_cast<ffe::f32>(rawZ);
            plane.width = static_cast<ffe::f32>(rawWidth);
            plane.depth = static_cast<ffe::f32>(rawDepth);
            const ffe::renderer::WaterSurfaceConfig cfg{}; // defaults

            const ffe::renderer::WaterHandle h =
                engine->m_waterManager.createWater(plane, cfg);

            lua_pushinteger(state, static_cast<lua_Integer>(h.id));
            return 1;
        };
        lua_pushcfunction(L, fn_createWaterSurface);
    }
    lua_setfield(L, -2, "createWaterSurface");

    // ffe.destroyWaterSurface(handle: integer) -> nil
    // Destroy a water surface and free its GPU resources. No-op on invalid handle.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        engine->m_waterManager.destroyWater(
            ffe::renderer::WaterHandle{static_cast<ffe::u32>(rawId > 0 ? rawId : 0)});
        return 0;
    });
    lua_setfield(L, -2, "destroyWaterSurface");

    // ffe.setWaterSurfaceColor(handle, r, g, b) -> nil
    // Update the shallow water color on an existing surface.
    // r, g, b are in [0, 1].
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId = lua_tointeger(state, 1);
        const ffe::f32 r = static_cast<ffe::f32>(lua_tonumber(state, 2));
        const ffe::f32 g = static_cast<ffe::f32>(lua_tonumber(state, 3));
        const ffe::f32 b = static_cast<ffe::f32>(lua_tonumber(state, 4));

        ffe::renderer::WaterHandle h{static_cast<ffe::u32>(rawId > 0 ? rawId : 0)};
        // WaterManager::setWaterConfig requires fetching the current config first.
        // We store a temporary with the updated color and forward it.
        // (WaterManager is not a singleton — retrieve via engine member.)
        // There is no getWaterConfig() on WaterManager; construct defaults and update color only.
        ffe::renderer::WaterSurfaceConfig cfg{};
        cfg.waterColor = glm::vec3(r, g, b);
        engine->m_waterManager.setWaterConfig(h, cfg);
        return 0;
    });
    lua_setfield(L, -2, "setWaterSurfaceColor");

    // ffe.setWaterSurfaceWave(handle, speed, scale, amplitude) -> nil
    // Update wave animation parameters.
    // speed and amplitude are clamped to [0, 10]; scale accepted as-is.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId   = lua_tointeger(state, 1);
        const double rawSpeed     = lua_tonumber(state, 2);
        const double rawScale     = lua_tonumber(state, 3);
        const double rawAmplitude = lua_tonumber(state, 4);

        // Clamp speed and amplitude to [0, 10]; reject negatives.
        const ffe::f32 speed = static_cast<ffe::f32>(
            rawSpeed < 0.0 ? 0.0 : (rawSpeed > 10.0 ? 10.0 : rawSpeed));
        const ffe::f32 scale = static_cast<ffe::f32>(rawScale);
        const ffe::f32 amplitude = static_cast<ffe::f32>(
            rawAmplitude < 0.0 ? 0.0 : (rawAmplitude > 10.0 ? 10.0 : rawAmplitude));

        ffe::renderer::WaterHandle h{static_cast<ffe::u32>(rawId > 0 ? rawId : 0)};
        ffe::renderer::WaterSurfaceConfig cfg{};
        cfg.waveSpeed     = speed;
        cfg.waveScale     = scale;
        cfg.waveAmplitude = amplitude;
        engine->m_waterManager.setWaterConfig(h, cfg);
        return 0;
    });
    lua_setfield(L, -2, "setWaterSurfaceWave");

    // ffe.setWaterSurfaceFresnel(handle, power, reflectionStrength) -> nil
    // Update Fresnel parameters.
    // power must be > 0; reflectionStrength is clamped to [0, 1].
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId      = lua_tointeger(state, 1);
        const double rawPower        = lua_tonumber(state, 2);
        const double rawReflStrength = lua_tonumber(state, 3);

        if (rawPower <= 0.0) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.setWaterSurfaceFresnel: power must be > 0, got %.3f",
                          rawPower);
            return 0;
        }

        const ffe::f32 power = static_cast<ffe::f32>(rawPower);
        // Clamp reflectionStrength to [0, 1].
        const ffe::f32 reflStrength = static_cast<ffe::f32>(
            rawReflStrength < 0.0 ? 0.0 : (rawReflStrength > 1.0 ? 1.0 : rawReflStrength));

        ffe::renderer::WaterHandle h{static_cast<ffe::u32>(rawId > 0 ? rawId : 0)};
        ffe::renderer::WaterSurfaceConfig cfg{};
        cfg.fresnelPower        = power;
        cfg.reflectionStrength  = reflStrength;
        engine->m_waterManager.setWaterConfig(h, cfg);
        return 0;
    });
    lua_setfield(L, -2, "setWaterSurfaceFresnel");

    // ffe.setWaterSurfaceReflection(handle, enabled: boolean) -> nil
    // Enable or disable planar reflection for a water surface.
    // On LEGACY tier the reflection FBO is never allocated — this controls
    // whether createWater() will attempt to allocate it on STANDARD+.
    lua_pushcfunction(L, [](lua_State* state) -> int {
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        const lua_Integer rawId  = lua_tointeger(state, 1);
        const bool enabled       = lua_toboolean(state, 2) != 0;

        ffe::renderer::WaterHandle h{static_cast<ffe::u32>(rawId > 0 ? rawId : 0)};
        ffe::renderer::WaterSurfaceConfig cfg{};
        cfg.reflectionEnabled = enabled;
        engine->m_waterManager.setWaterConfig(h, cfg);
        return 0;
    });
    lua_setfield(L, -2, "setWaterSurfaceReflection");

    // ================================================================
    // Prefab system bindings (Phase 10 M1)
    // ================================================================
    //
    // ffe.loadPrefab(path: string) -> integer
    //   Load a prefab JSON file from a path relative to the asset root.
    //   Returns integer handle id > 0 on success, 0 on failure.
    //   Cold path only — call at startup or scene load, not per frame.
    //
    // ffe.instantiatePrefab(handle: integer [, overrides: table]) -> integer
    //   Create a new entity from a prefab template, applying optional overrides.
    //   overrides table format: { ComponentName = { field = value, ... }, ... }
    //   Returns entity id on success, ffe.NULL_ENTITY (or 0) on failure.
    //
    // ffe.unloadPrefab(handle: integer) -> nothing
    //   Release a prefab slot. The handle becomes invalid immediately.
    // ================================================================

    // ffe.loadPrefab(path: string) -> integer
    lua_pushcfunction(L, [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TSTRING) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadPrefab: argument 1 must be a string (path)");
            lua_pushinteger(state, 0);
            return 1;
        }
        const char* path = lua_tostring(state, 1);
        if (path == nullptr || path[0] == '\0') {
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadPrefab: path is null or empty");
            lua_pushinteger(state, 0);
            return 1;
        }

        // Retrieve ScriptEngine pointer from registry (for prefab system + asset root).
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            FFE_LOG_ERROR("ScriptEngine", "ffe.loadPrefab: ScriptEngine not in registry");
            lua_pushinteger(state, 0);
            return 1;
        }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // Lazily propagate the engine's asset root to the prefab system.
        // This is safe to call repeatedly — setAssetRoot is idempotent.
        if (engine->m_assetRoot[0] != '\0') {
            engine->m_prefabSystem.setAssetRoot(engine->m_assetRoot);
        }

        const ffe::PrefabHandle handle = engine->m_prefabSystem.loadPrefab(path);
        lua_pushinteger(state, static_cast<lua_Integer>(handle.id));
        return 1;
    });
    lua_setfield(L, -2, "loadPrefab");

    // ffe.instantiatePrefab(handle: integer [, overrides: table]) -> integer
    lua_pushcfunction(L, [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.instantiatePrefab: argument 1 must be a number (handle)");
            lua_pushinteger(state, static_cast<lua_Integer>(ffe::NULL_ENTITY));
            return 1;
        }

        // Retrieve World pointer.
        lua_pushlightuserdata(state, &s_worldRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            FFE_LOG_ERROR("ScriptEngine", "ffe.instantiatePrefab: world not registered");
            lua_pushinteger(state, static_cast<lua_Integer>(ffe::NULL_ENTITY));
            return 1;
        }
        auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // Retrieve ScriptEngine pointer.
        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            FFE_LOG_ERROR("ScriptEngine", "ffe.instantiatePrefab: ScriptEngine not in registry");
            lua_pushinteger(state, static_cast<lua_Integer>(ffe::NULL_ENTITY));
            return 1;
        }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        // Validate and extract handle id.
        const lua_Integer rawHandle = lua_tointeger(state, 1);
        if (rawHandle <= 0) {
            FFE_LOG_ERROR("ScriptEngine",
                          "ffe.instantiatePrefab: invalid handle %" PRId64,
                          static_cast<long long>(rawHandle));
            lua_pushinteger(state, static_cast<lua_Integer>(ffe::NULL_ENTITY));
            return 1;
        }
        const ffe::PrefabHandle handle{static_cast<ffe::u32>(rawHandle)};

        // Build PrefabOverrides from the optional second argument (table).
        ffe::PrefabOverrides overrides{};
        if (lua_istable(state, 2)) {
            // Iterate top-level keys (component names).
            lua_pushnil(state);  // first key
            while (lua_next(state, 2) != 0 && overrides.count < ffe::PrefabOverrides::MAX) {
                // stack: ... | compKey (-2) | compVal (-1)
                if (lua_type(state, -2) != LUA_TSTRING || !lua_istable(state, -1)) {
                    lua_pop(state, 1);  // pop value, keep key for next()
                    continue;
                }

                const char* compName = lua_tostring(state, -2);
                if (compName == nullptr) {
                    lua_pop(state, 1);
                    continue;
                }

                // Truncate component name to 31 chars.
                char compBuf[32];
                strncpy(compBuf, compName, 31);
                compBuf[31] = '\0';

                // Iterate inner table (field names and values).
                const int innerTblIdx = lua_gettop(state);  // absolute index of inner table
                lua_pushnil(state);  // first field key
                while (lua_next(state, innerTblIdx) != 0 && overrides.count < ffe::PrefabOverrides::MAX) {
                    // stack: ... | compKey | compVal | fieldKey (-2) | fieldVal (-1)
                    if (lua_type(state, -2) != LUA_TSTRING) {
                        lua_pop(state, 1);
                        continue;
                    }

                    const char* fieldName = lua_tostring(state, -2);
                    if (fieldName == nullptr) {
                        lua_pop(state, 1);
                        continue;
                    }

                    // Truncate field name to 31 chars.
                    char fieldBuf[32];
                    strncpy(fieldBuf, fieldName, 31);
                    fieldBuf[31] = '\0';

                    if (lua_isnumber(state, -1)) {
                        const float v = static_cast<float>(lua_tonumber(state, -1));
                        overrides.set(compBuf, fieldBuf, v);
                    } else if (lua_isboolean(state, -1)) {
                        const bool v = lua_toboolean(state, -1) != 0;
                        overrides.set(compBuf, fieldBuf, v);
                    } else {
                        FFE_LOG_WARN("ScriptEngine",
                                     "ffe.instantiatePrefab: override '%s.%s' has unsupported type — ignored",
                                     compBuf, fieldBuf);
                    }

                    lua_pop(state, 1);  // pop field value, keep field key for next()
                }
                // Pop inner table value; key stays for outer next().
                lua_pop(state, 1);
            }
        }

        const ffe::EntityId eid = engine->m_prefabSystem.instantiatePrefab(*world, handle, overrides);
        lua_pushinteger(state, static_cast<lua_Integer>(eid));
        return 1;
    });
    lua_setfield(L, -2, "instantiatePrefab");

    // ffe.unloadPrefab(handle: integer) -> nothing
    lua_pushcfunction(L, [](lua_State* state) -> int {
        if (lua_type(state, 1) != LUA_TNUMBER) {
            FFE_LOG_ERROR("ScriptEngine", "ffe.unloadPrefab: argument 1 must be a number (handle)");
            return 0;
        }
        const lua_Integer rawHandle = lua_tointeger(state, 1);
        if (rawHandle <= 0) {
            return 0;  // invalid handle — no-op
        }

        lua_pushlightuserdata(state, &s_engineRegistryKey);
        lua_gettable(state, LUA_REGISTRYINDEX);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            FFE_LOG_ERROR("ScriptEngine", "ffe.unloadPrefab: ScriptEngine not in registry");
            return 0;
        }
        auto* engine = static_cast<ffe::ScriptEngine*>(lua_touserdata(state, -1));
        lua_pop(state, 1);

        engine->m_prefabSystem.unloadPrefab(ffe::PrefabHandle{static_cast<ffe::u32>(rawHandle)});
        return 0;
    });
    lua_setfield(L, -2, "unloadPrefab");

    lua_setglobal(L, "ffe");
}

} // namespace ffe
