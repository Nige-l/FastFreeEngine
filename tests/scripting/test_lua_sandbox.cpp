#include <catch2/catch_test_macros.hpp>
#include "scripting/script_engine.h"

// =============================================================================
// Fixtures
// =============================================================================

// ScriptFixture: creates a fully-initialised ScriptEngine for each test case
// and shuts it down on destruction.
struct ScriptFixture {
    ffe::ScriptEngine engine;

    ScriptFixture() {
        REQUIRE(engine.init());
    }
    ~ScriptFixture() {
        engine.shutdown();
    }
};

// =============================================================================
// Lifecycle
// =============================================================================

TEST_CASE("ScriptEngine init and shutdown succeed", "[scripting][lifecycle]") {
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());
    REQUIRE(engine.isInitialised());
    engine.shutdown();
    REQUIRE_FALSE(engine.isInitialised());
}

TEST_CASE("doString executes valid Lua and returns true", "[scripting][basic]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local x = 1 + 1"));
}

TEST_CASE("doString returns false on syntax error without crashing", "[scripting][error]") {
    ScriptFixture fix;
    // Missing 'end' — syntax error
    REQUIRE_FALSE(fix.engine.doString("if true then"));
}

TEST_CASE("doString returns false on runtime error without crashing", "[scripting][error]") {
    ScriptFixture fix;
    // Calling nil as a function — runtime error
    REQUIRE_FALSE(fix.engine.doString("local f = nil; f()"));
}

// =============================================================================
// Sandbox — blocked libraries and globals
// =============================================================================

TEST_CASE("io library is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // io is not opened; accessing io.open must produce a runtime error, not crash.
    REQUIRE_FALSE(fix.engine.doString("io.open('test.txt', 'r')"));
}

TEST_CASE("os library is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // os is not opened; accessing os.execute must produce a runtime error.
    REQUIRE_FALSE(fix.engine.doString("os.execute('ls')"));
}

TEST_CASE("require is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // 'require' is set to nil in setupSandbox(); attempting to call it is a runtime error.
    REQUIRE_FALSE(fix.engine.doString("require('os')"));
}

TEST_CASE("load function is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // 'load' is a CRITICAL removal (ADR-004). Calling it must fail.
    REQUIRE_FALSE(fix.engine.doString("load('return 1')()"));
}

TEST_CASE("dofile is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // 'dofile' is set to nil. Calling it is a runtime error.
    REQUIRE_FALSE(fix.engine.doString("dofile('test.lua')"));
}

TEST_CASE("debug library is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // debug table is explicitly nil'd in setupSandbox().
    REQUIRE_FALSE(fix.engine.doString("debug.traceback()"));
}

TEST_CASE("setmetatable is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // setmetatable removed — HIGH ADR-004 finding (sandbox escape via metamethods).
    REQUIRE_FALSE(fix.engine.doString("setmetatable({}, {__index = function() end})"));
}

TEST_CASE("rawset is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // rawset removed — HIGH ADR-004 finding (__newindex bypass).
    REQUIRE_FALSE(fix.engine.doString("rawset({}, 'k', 'v')"));
}

TEST_CASE("rawget is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // rawget removed — HIGH ADR-004 finding (__index bypass).
    REQUIRE_FALSE(fix.engine.doString("rawget({}, 'k')"));
}

TEST_CASE("collectgarbage is not accessible", "[scripting][sandbox]") {
    ScriptFixture fix;
    // collectgarbage removed — DoS vector via GC pressure.
    REQUIRE_FALSE(fix.engine.doString("collectgarbage('collect')"));
}

// =============================================================================
// Instruction budget — infinite loop protection
// =============================================================================

TEST_CASE("Infinite loop is terminated by instruction budget", "[scripting][sandbox][budget]") {
    ScriptFixture fix;
    // The instruction hook fires after 1,000,000 instructions and calls luaL_error.
    // doString catches the error via lua_pcall and returns false.
    // This must NOT hang the test runner.
    REQUIRE_FALSE(fix.engine.doString("while true do end"));
}

TEST_CASE("Large finite loop completes without error", "[scripting][budget]") {
    ScriptFixture fix;
    // A 10,000-iteration loop is well within the 1,000,000-instruction budget.
    REQUIRE(fix.engine.doString("local s = 0; for i = 1, 10000 do s = s + i end"));
}

// =============================================================================
// ffe.* API
// =============================================================================

TEST_CASE("ffe.log is accessible and succeeds", "[scripting][api]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.log('hello from test')"));
}

TEST_CASE("ffe table exists and is a table", "[scripting][api]") {
    ScriptFixture fix;
    // type() is part of the base library (safe). If 'ffe' is not a table this errors.
    REQUIRE(fix.engine.doString("assert(type(ffe) == 'table')"));
}

// =============================================================================
// Error isolation — no internal path leakage
// =============================================================================

TEST_CASE("Script error does not leak C++ source file paths", "[scripting][security]") {
    // After a runtime error, the error message is logged via FFE_LOG_ERROR.
    // We cannot directly intercept the log here without a test hook, but we CAN
    // verify that doString returns false (confirming the error path was taken)
    // and that the engine remains operational (no crash, no undefined state).
    //
    // For path-leakage verification we rely on code review of script_engine.cpp:
    // the error message is taken from lua_tostring(L, -1), which is the Lua
    // error string — Lua errors contain source locations in "[string]:N" format,
    // not C++ file paths. This test asserts the error is caught cleanly.
    ScriptFixture fix;

    // Trigger a runtime error
    const bool result = fix.engine.doString("error('deliberate error')");
    REQUIRE_FALSE(result);

    // Engine must remain functional after the error
    REQUIRE(fix.engine.doString("local x = 42"));
}

// =============================================================================
// Independence — multiple doString calls
// =============================================================================

TEST_CASE("Multiple doString calls work independently", "[scripting][basic]") {
    ScriptFixture fix;

    // First script succeeds
    REQUIRE(fix.engine.doString("local a = 10"));

    // Second script succeeds — no state contamination from first
    REQUIRE(fix.engine.doString("local b = 20"));
}

TEST_CASE("Failed doString does not corrupt engine state", "[scripting][error]") {
    ScriptFixture fix;

    // Cause a runtime error
    REQUIRE_FALSE(fix.engine.doString("error('boom')"));

    // Engine must still work for subsequent scripts
    REQUIRE(fix.engine.doString("local x = 1 + 1"));
    REQUIRE(fix.engine.doString("ffe.log('still alive')"));
}

TEST_CASE("Lua global state persists between doString calls on same engine", "[scripting][basic]") {
    ScriptFixture fix;

    // Set a global in one call
    REQUIRE(fix.engine.doString("MY_VALUE = 99"));

    // Read it back in a subsequent call
    REQUIRE(fix.engine.doString("assert(MY_VALUE == 99)"));
}

// =============================================================================
// doString called before init
// =============================================================================

TEST_CASE("doString before init returns false without crash", "[scripting][lifecycle]") {
    ffe::ScriptEngine engine;
    // init() NOT called — engine is uninitialised
    REQUIRE_FALSE(engine.doString("local x = 1"));
}

// =============================================================================
// Math library is available (it IS in the whitelist)
// =============================================================================

TEST_CASE("math library is accessible", "[scripting][api]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local x = math.sqrt(4); assert(x == 2)"));
}

TEST_CASE("string library is accessible", "[scripting][api]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local s = string.upper('hello'); assert(s == 'HELLO')"));
}

TEST_CASE("table library is accessible", "[scripting][api]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local t = {3,1,2}; table.sort(t); assert(t[1] == 1)"));
}
