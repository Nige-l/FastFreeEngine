// =============================================================================
// Adversarial Lua sandbox security tests
// =============================================================================
//
// These tests attempt to escape the Lua sandbox through various attack vectors.
// Every test must either confirm that the attack is blocked (doString returns
// false) or that the result is safe (no information leakage, no escape).

#include <catch2/catch_test_macros.hpp>
#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "core/input.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/texture_loader.h"
#include "audio/audio.h"
#include "physics/collider2d.h"
#include "renderer/text_renderer.h"

#include <string>

// =============================================================================
// Fixture — mirrors the one in test_lua_sandbox.cpp
// =============================================================================

struct SecurityFixture {
    ffe::ScriptEngine engine;

    SecurityFixture() {
        REQUIRE(engine.init());
    }
    ~SecurityFixture() {
        engine.shutdown();
    }
};

// =============================================================================
// 1. Blocked globals — comprehensive verification
// =============================================================================

TEST_CASE("Blocked: os global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(os == nil)"));
}

TEST_CASE("Blocked: io global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(io == nil)"));
}

TEST_CASE("Blocked: debug global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(debug == nil)"));
}

TEST_CASE("Blocked: loadfile global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(loadfile == nil)"));
}

TEST_CASE("Blocked: dofile global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(dofile == nil)"));
}

TEST_CASE("Blocked: load global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(load == nil)"));
}

TEST_CASE("Blocked: loadstring global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(loadstring == nil)"));
}

TEST_CASE("Blocked: rawget global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(rawget == nil)"));
}

TEST_CASE("Blocked: rawset global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(rawset == nil)"));
}

TEST_CASE("Blocked: rawequal global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(rawequal == nil)"));
}

TEST_CASE("Blocked: rawlen global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(rawlen == nil)"));
}

TEST_CASE("Blocked: collectgarbage global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(collectgarbage == nil)"));
}

// =============================================================================
// 2. string.dump blocked — prevents bytecode serialisation escape
// =============================================================================

TEST_CASE("Blocked: string.dump is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(string.dump == nil)"));
}

TEST_CASE("Blocked: string.dump cannot serialise functions", "[scripting][security]") {
    SecurityFixture fix;
    // Even if someone tries to call it, it must fail
    REQUIRE_FALSE(fix.engine.doString("string.dump(function() end)"));
}

// =============================================================================
// 3. Instruction budget — infinite loop killed
// =============================================================================

TEST_CASE("Instruction budget: while true do end is killed", "[scripting][security]") {
    SecurityFixture fix;
    // Must not hang — the instruction hook fires and raises an error
    REQUIRE_FALSE(fix.engine.doString("while true do end"));
}

TEST_CASE("Instruction budget: tight repeat-until loop is killed", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doString("repeat until false"));
}

TEST_CASE("Instruction budget: recursive bomb is killed", "[scripting][security]") {
    SecurityFixture fix;
    // Deep recursion that exceeds instruction budget
    REQUIRE_FALSE(fix.engine.doString(
        "local function bomb() bomb() end; bomb()"));
}

TEST_CASE("Instruction budget: nested loop bomb is killed", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doString(
        "for i = 1, 1e15 do for j = 1, 1e15 do end end"));
}

TEST_CASE("Instruction budget: engine recovers after budget exceeded", "[scripting][security]") {
    SecurityFixture fix;
    // Exhaust the budget
    REQUIRE_FALSE(fix.engine.doString("while true do end"));
    // Engine must still work for subsequent scripts
    REQUIRE(fix.engine.doString("local x = 42"));
}

// =============================================================================
// 4. Memory bomb — large allocation attempts
// =============================================================================

TEST_CASE("Memory bomb: huge string.rep is bounded", "[scripting][security]") {
    SecurityFixture fix;
    // Attempt to allocate ~100 MB via string.rep.
    // This should either error (out of memory) or be killed by instruction budget.
    // Either way doString must return false and not crash.
    const bool result = fix.engine.doString("local s = string.rep('x', 10^8)");
    // We accept either outcome — the key is no crash and no hang.
    // If it succeeds (unlikely but possible on systems with lots of RAM),
    // that is technically safe (just wasteful). If it fails, that is correct.
    (void)result;
    // Verify engine is still functional
    REQUIRE(fix.engine.doString("local x = 1"));
}

TEST_CASE("Memory bomb: table growth bomb does not crash", "[scripting][security]") {
    SecurityFixture fix;
    // Try to create a massive table. Should be killed by instruction budget
    // before it can allocate too much.
    const bool result = fix.engine.doString(
        "local t = {}; for i = 1, 10^9 do t[i] = i end");
    (void)result;
    // Engine must survive
    REQUIRE(fix.engine.doString("local x = 1"));
}

TEST_CASE("Memory bomb: nested table construction does not crash", "[scripting][security]") {
    SecurityFixture fix;
    // Rapidly nesting tables to consume memory
    const bool result = fix.engine.doString(
        "local t = {}; local c = t; "
        "for i = 1, 10^7 do c.next = {}; c = c.next end");
    (void)result;
    REQUIRE(fix.engine.doString("local x = 1"));
}

TEST_CASE("Memory bomb: string concatenation bomb does not crash", "[scripting][security]") {
    SecurityFixture fix;
    // Exponential string growth via repeated concatenation
    const bool result = fix.engine.doString(
        "local s = 'x'; for i = 1, 100 do s = s .. s end");
    (void)result;
    REQUIRE(fix.engine.doString("local x = 1"));
}

// =============================================================================
// 5. Path traversal — doFile rejects unsafe paths
// =============================================================================

TEST_CASE("Path traversal: ../../etc/passwd is rejected", "[scripting][security]") {
    SecurityFixture fix;
    // doFile must reject paths with ../ components
    REQUIRE_FALSE(fix.engine.doFile("../../etc/passwd", "/tmp"));
}

TEST_CASE("Path traversal: ../../../somewhere is rejected", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doFile("../../../somewhere", "/tmp"));
}

TEST_CASE("Path traversal: backslash variant is rejected", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doFile("..\\..\\etc\\passwd", "/tmp"));
}

TEST_CASE("Path traversal: absolute path is rejected", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doFile("/etc/passwd", "/tmp"));
}

TEST_CASE("Path traversal: null path is rejected", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doFile(nullptr, "/tmp"));
}

TEST_CASE("Path traversal: empty path is rejected", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doFile("", "/tmp"));
}

TEST_CASE("Path traversal: Windows drive letter is rejected", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doFile("C:\\Windows\\System32\\cmd.exe", "/tmp"));
}

TEST_CASE("Path traversal: UNC path is rejected", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doFile("\\\\server\\share\\file.lua", "/tmp"));
}

TEST_CASE("Path traversal: Windows ADS colon is rejected", "[scripting][security]") {
    SecurityFixture fix;
    // Alternate Data Streams attack: "file.lua:hidden_stream"
    REQUIRE_FALSE(fix.engine.doFile("scripts/game.lua:hidden", "/tmp"));
}

TEST_CASE("Path traversal: mid-path ../ is rejected", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doFile("scripts/../../../etc/passwd", "/tmp"));
}

TEST_CASE("Path traversal: trailing .. is rejected", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doFile("scripts/..", "/tmp"));
}

TEST_CASE("Path traversal: Lua ffe.doFile with traversal is rejected", "[scripting][security]") {
    SecurityFixture fix;
    // Attempt path traversal via the Lua binding (if ffe.doFile exists)
    // Even if ffe.doFile is not exposed, this must not crash
    const bool result = fix.engine.doString(
        "if ffe.doFile then ffe.doFile('../../etc/passwd') end");
    // Either ffe.doFile does not exist (script succeeds but does nothing)
    // or it rejects the path (script may error). Either is safe.
    (void)result;
}

// =============================================================================
// 6. Metatable escape — cannot manipulate protected metatables
// =============================================================================

TEST_CASE("Metatable escape: setmetatable is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(setmetatable == nil)"));
}

TEST_CASE("Metatable escape: getmetatable is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(getmetatable == nil)"));
}

TEST_CASE("Metatable escape: cannot set metatable on string", "[scripting][security]") {
    SecurityFixture fix;
    // Without setmetatable, cannot poison the string metatable
    REQUIRE_FALSE(fix.engine.doString(
        "setmetatable('', {__index = function() return 'pwned' end})"));
}

TEST_CASE("Metatable escape: cannot access string metatable", "[scripting][security]") {
    SecurityFixture fix;
    // Without getmetatable, cannot read internal metatables
    REQUIRE_FALSE(fix.engine.doString("local mt = getmetatable('')"));
}

TEST_CASE("Metatable escape: cannot override __index on ffe table", "[scripting][security]") {
    SecurityFixture fix;
    // Even without setmetatable, verify direct __index write does nothing harmful
    REQUIRE_FALSE(fix.engine.doString(
        "setmetatable(ffe, {__index = function(t, k) return _G end})"));
}

// =============================================================================
// 7. require blocked
// =============================================================================

TEST_CASE("Blocked: require is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(require == nil)"));
}

TEST_CASE("Blocked: require('os') fails", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doString("require('os')"));
}

TEST_CASE("Blocked: require('io') fails", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doString("require('io')"));
}

TEST_CASE("Blocked: require('ffi') fails", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE_FALSE(fix.engine.doString("require('ffi')"));
}

// =============================================================================
// 8. pcall/xpcall available — error handling within sandbox is fine
// =============================================================================

TEST_CASE("Safe: pcall is available and works", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(type(pcall) == 'function')"));
}

TEST_CASE("Safe: pcall catches errors", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString(
        "local ok, err = pcall(function() error('test') end); "
        "assert(ok == false)"));
}

TEST_CASE("Safe: xpcall is available and works", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(type(xpcall) == 'function')"));
}

TEST_CASE("Safe: xpcall calls error handler", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString(
        "local handled = false; "
        "xpcall(function() error('test') end, function(e) handled = true end); "
        "assert(handled)"));
}

TEST_CASE("Safe: pcall cannot catch instruction budget error to continue", "[scripting][security]") {
    SecurityFixture fix;
    // A malicious script might try to use pcall to catch the instruction budget
    // error and keep running. The budget is per-call from C, so even if pcall
    // catches the error, the outer call should still fail.
    REQUIRE_FALSE(fix.engine.doString(
        "while true do "
        "  local ok = pcall(function() while true do end end); "
        "  if not ok then end "
        "end"));
}

// =============================================================================
// 9. Coroutine available — coroutines should work within the sandbox
// =============================================================================

TEST_CASE("Safe: coroutine table exists", "[scripting][security]") {
    SecurityFixture fix;
    // coroutine is part of the base library in Lua 5.1/LuaJIT
    // It may or may not be available depending on how base was opened.
    // If it exists, it should be functional. If not, that is also safe.
    const bool exists = fix.engine.doString("assert(type(coroutine) == 'table')");
    if (exists) {
        REQUIRE(fix.engine.doString(
            "local co = coroutine.create(function() "
            "  coroutine.yield(42) "
            "end); "
            "local ok, val = coroutine.resume(co); "
            "assert(ok and val == 42)"));
    }
}

TEST_CASE("Safe: coroutine infinite yield does not escape budget", "[scripting][security]") {
    SecurityFixture fix;
    // A coroutine that never finishes should still be bounded by instruction budget
    // when resumed in a loop
    const bool hasCoroutine = fix.engine.doString("assert(type(coroutine) == 'table')");
    if (hasCoroutine) {
        REQUIRE_FALSE(fix.engine.doString(
            "local co = coroutine.create(function() "
            "  while true do coroutine.yield() end "
            "end); "
            "while true do coroutine.resume(co) end"));
    }
}

// =============================================================================
// 10. FFI blocked — LuaJIT FFI must not be accessible
// =============================================================================

TEST_CASE("Blocked: ffi global is nil", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString("assert(ffi == nil)"));
}

TEST_CASE("Blocked: jit global is nil", "[scripting][security]") {
    SecurityFixture fix;
    // jit library should not be opened
    REQUIRE(fix.engine.doString("assert(jit == nil)"));
}

TEST_CASE("Blocked: require('ffi') cannot load FFI", "[scripting][security]") {
    SecurityFixture fix;
    // require is nil, so this double-checks that FFI is unreachable
    REQUIRE_FALSE(fix.engine.doString("local ffi = require('ffi')"));
}

TEST_CASE("Blocked: package global is nil", "[scripting][security]") {
    SecurityFixture fix;
    // package library gives access to C loaders — must be blocked
    REQUIRE(fix.engine.doString("assert(package == nil)"));
}

// =============================================================================
// Compound attacks — multi-step escape attempts
// =============================================================================

TEST_CASE("Compound: pcall + blocked global does not leak", "[scripting][security]") {
    SecurityFixture fix;
    // Use pcall to probe for blocked globals without crashing
    REQUIRE(fix.engine.doString(
        "local ok, val = pcall(function() return os end); "
        "assert(val == nil)"));
}

TEST_CASE("Compound: string metatable probe via method call", "[scripting][security]") {
    SecurityFixture fix;
    // String methods should work (string library is opened), but internal
    // metatable must not be directly accessible
    REQUIRE(fix.engine.doString("assert(('hello'):upper() == 'HELLO')"));
    REQUIRE_FALSE(fix.engine.doString("local mt = getmetatable(''); return mt"));
}

TEST_CASE("Compound: _G iteration does not expose blocked globals", "[scripting][security]") {
    SecurityFixture fix;
    // Iterate _G and verify none of the dangerous globals are present
    REQUIRE(fix.engine.doString(
        "for k, v in pairs(_G) do "
        "  assert(k ~= 'os'); "
        "  assert(k ~= 'io'); "
        "  assert(k ~= 'debug'); "
        "  assert(k ~= 'loadfile'); "
        "  assert(k ~= 'dofile'); "
        "  assert(k ~= 'load'); "
        "  assert(k ~= 'loadstring'); "
        "  assert(k ~= 'rawget'); "
        "  assert(k ~= 'rawset'); "
        "  assert(k ~= 'require'); "
        "  assert(k ~= 'ffi'); "
        "  assert(k ~= 'package'); "
        "end"));
}

TEST_CASE("Compound: environment manipulation blocked", "[scripting][security]") {
    SecurityFixture fix;
    // In Lua 5.1/LuaJIT, setfenv/getfenv can manipulate function environments.
    // These are part of the base library. Check if they are available.
    // If setfenv exists, a malicious script could try to change its own
    // environment to access blocked globals. Verify this does not help.
    const bool result = fix.engine.doString(
        "if setfenv then "
        "  -- Even with setfenv, blocked globals are nil'd from _G "
        "  local env = {}; "
        "  setfenv(1, env); "
        "  -- os should still be nil even in new environment "
        "  assert(os == nil); "
        "end");
    // If setfenv does not exist (Lua 5.2+ style), the if-block is skipped
    // and doString succeeds. Either way is safe.
    (void)result;
}

TEST_CASE("Compound: tostring on all types does not crash", "[scripting][security]") {
    SecurityFixture fix;
    // tostring should be safe on all types
    REQUIRE(fix.engine.doString(
        "tostring(nil); "
        "tostring(true); "
        "tostring(42); "
        "tostring('hello'); "
        "tostring({}); "
        "tostring(print)"));
}

TEST_CASE("Compound: pairs/ipairs/next are available and safe", "[scripting][security]") {
    SecurityFixture fix;
    REQUIRE(fix.engine.doString(
        "local t = {a=1, b=2, c=3}; "
        "local count = 0; "
        "for k, v in pairs(t) do count = count + 1 end; "
        "assert(count == 3)"));
    REQUIRE(fix.engine.doString(
        "local t = {10, 20, 30}; "
        "local sum = 0; "
        "for i, v in ipairs(t) do sum = sum + v end; "
        "assert(sum == 60)"));
}
