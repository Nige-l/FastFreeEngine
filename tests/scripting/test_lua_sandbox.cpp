#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "core/input.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/texture_loader.h"

#include <string>

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

// =============================================================================
// Input API — key constants and query functions
// =============================================================================

TEST_CASE("ffe key constants are accessible as integers", "[scripting][input]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_W) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_A) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_S) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_D) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_SPACE) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_ESCAPE) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_ENTER) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_UP) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_DOWN) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_LEFT) == 'number')"));
    REQUIRE(fix.engine.doString("assert(type(ffe.KEY_RIGHT) == 'number')"));
}

TEST_CASE("ffe key constants have correct GLFW values", "[scripting][input]") {
    ScriptFixture fix;
    // Values must match Key enum in input.h (which matches GLFW_KEY_*).
    REQUIRE(fix.engine.doString("assert(ffe.KEY_W == 87)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_A == 65)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_S == 83)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_D == 68)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_SPACE == 32)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_ESCAPE == 256)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_ENTER == 257)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_UP == 265)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_DOWN == 264)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_LEFT == 263)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_RIGHT == 262)"));
}

TEST_CASE("ffe.isKeyHeld is callable and returns bool", "[scripting][input]") {
    // Input not initialised in headless test — key state is all false.
    ScriptFixture fix;
    ffe::initInput(nullptr);  // headless — no GLFW window
    REQUIRE(fix.engine.doString("local held = ffe.isKeyHeld(ffe.KEY_W); assert(type(held) == 'boolean')"));
    ffe::shutdownInput();
}

TEST_CASE("ffe.isKeyPressed is callable and returns bool", "[scripting][input]") {
    ScriptFixture fix;
    ffe::initInput(nullptr);
    REQUIRE(fix.engine.doString("local v = ffe.isKeyPressed(ffe.KEY_SPACE); assert(type(v) == 'boolean')"));
    ffe::shutdownInput();
}

TEST_CASE("ffe.isKeyReleased is callable and returns bool", "[scripting][input]") {
    ScriptFixture fix;
    ffe::initInput(nullptr);
    REQUIRE(fix.engine.doString("local v = ffe.isKeyReleased(ffe.KEY_ESCAPE); assert(type(v) == 'boolean')"));
    ffe::shutdownInput();
}

TEST_CASE("ffe.isKeyHeld with out-of-range key code returns false", "[scripting][input]") {
    ScriptFixture fix;
    ffe::initInput(nullptr);
    // Key code 9999 is well outside MAX_KEYS (512) — must return false, not crash.
    REQUIRE(fix.engine.doString("local v = ffe.isKeyHeld(9999); assert(v == false)"));
    ffe::shutdownInput();
}

TEST_CASE("ffe.getMouseX and getMouseY return numbers", "[scripting][input]") {
    ScriptFixture fix;
    ffe::initInput(nullptr);
    REQUIRE(fix.engine.doString("local x = ffe.getMouseX(); assert(type(x) == 'number')"));
    REQUIRE(fix.engine.doString("local y = ffe.getMouseY(); assert(type(y) == 'number')"));
    ffe::shutdownInput();
}

// =============================================================================
// ECS bindings — setWorld / getTransform / setTransform
// =============================================================================

TEST_CASE("setWorld can be called with a valid World", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    // Must not crash or error.
    fix.engine.setWorld(&world);
}

TEST_CASE("setWorld can be called with nullptr to clear the World", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    fix.engine.setWorld(nullptr);  // must not crash
}

TEST_CASE("ffe.getTransform returns nil when no World is registered", "[scripting][ecs]") {
    ScriptFixture fix;
    // No setWorld() call — getTransform must return nil, not crash.
    REQUIRE(fix.engine.doString("local t = ffe.getTransform(0); assert(t == nil)"));
}

TEST_CASE("ffe.getTransform returns nil for invalid entity", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    // Entity 0xFFFFFFFF is NULL_ENTITY — always invalid.
    REQUIRE(fix.engine.doString("local t = ffe.getTransform(4294967295); assert(t == nil)"));
}

TEST_CASE("ffe.getTransform returns nil for entity without Transform", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    // No Transform added — getTransform must return nil.
    fix.engine.setWorld(&world);
    // Store entity id as a Lua global before running the query.
    const bool ok = fix.engine.doString(
        ("local t = ffe.getTransform(" + std::to_string(entity) + "); assert(t == nil)").c_str()
    );
    REQUIRE(ok);
}

TEST_CASE("ffe.getTransform returns a table with correct fields for valid entity", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    ffe::Transform& t = world.addComponent<ffe::Transform>(entity);
    t.position = {3.0f, 7.0f, 0.0f};
    t.scale    = {2.0f, 2.0f, 1.0f};
    t.rotation = 1.5f;

    fix.engine.setWorld(&world);

    const std::string script =
        "local t = ffe.getTransform(" + std::to_string(entity) + ")\n"
        "assert(t ~= nil)\n"
        "assert(type(t) == 'table')\n"
        "assert(t.x ~= nil)\n"
        "assert(t.y ~= nil)\n"
        "assert(t.z ~= nil)\n"
        "assert(t.scaleX ~= nil)\n"
        "assert(t.scaleY ~= nil)\n"
        "assert(t.rotation ~= nil)\n";

    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.getTransform returns correct position values", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    ffe::Transform& t = world.addComponent<ffe::Transform>(entity);
    t.position = {5.0f, 10.0f, 0.0f};
    t.scale    = {1.0f, 1.0f, 1.0f};
    t.rotation = 0.0f;

    fix.engine.setWorld(&world);

    const std::string script =
        "local t = ffe.getTransform(" + std::to_string(entity) + ")\n"
        "assert(t ~= nil)\n"
        "assert(math.abs(t.x - 5.0) < 0.001)\n"
        "assert(math.abs(t.y - 10.0) < 0.001)\n";

    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.setTransform updates Transform component", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    world.addComponent<ffe::Transform>(entity);

    fix.engine.setWorld(&world);

    // Set x=4, y=8, rotation=0.5, scaleX=2, scaleY=3.
    const std::string script =
        "ffe.setTransform(" + std::to_string(entity) + ", 4, 8, 0.5, 2, 3)";

    REQUIRE(fix.engine.doString(script.c_str()));

    const ffe::Transform& t = world.getComponent<ffe::Transform>(entity);
    REQUIRE(t.position.x == Catch::Approx(4.0f));
    REQUIRE(t.position.y == Catch::Approx(8.0f));
    REQUIRE(t.rotation   == Catch::Approx(0.5f));
    REQUIRE(t.scale.x    == Catch::Approx(2.0f));
    REQUIRE(t.scale.y    == Catch::Approx(3.0f));
}

TEST_CASE("ffe.setTransform on invalid entity is a no-op", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    // Should not crash or return an error.
    REQUIRE(fix.engine.doString("ffe.setTransform(4294967295, 1, 2, 3, 4, 5)"));
}

TEST_CASE("ffe.setTransform without World registered is a no-op", "[scripting][ecs]") {
    ScriptFixture fix;
    // No setWorld() — should not crash.
    REQUIRE(fix.engine.doString("ffe.setTransform(0, 1, 2, 3, 4, 5)"));
}

TEST_CASE("ffe.getTransform and setTransform round-trip preserves values", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    world.addComponent<ffe::Transform>(entity);

    fix.engine.setWorld(&world);

    const std::string script =
        "ffe.setTransform(" + std::to_string(entity) + ", 12, 34, 0.25, 1.5, 2.5)\n"
        "local t = ffe.getTransform(" + std::to_string(entity) + ")\n"
        "assert(t ~= nil)\n"
        "assert(math.abs(t.x - 12) < 0.001)\n"
        "assert(math.abs(t.y - 34) < 0.001)\n"
        "assert(math.abs(t.rotation - 0.25) < 0.001)\n"
        "assert(math.abs(t.scaleX - 1.5) < 0.001)\n"
        "assert(math.abs(t.scaleY - 2.5) < 0.001)\n";

    REQUIRE(fix.engine.doString(script.c_str()));
}

// =============================================================================
// Security fix — NaN and Infinity rejection in setTransform
// =============================================================================

TEST_CASE("setTransform with NaN x is rejected without crash", "[scripting][ecs][security]") {
    // NaN rejection only fires once entity validation passes, so we need a real entity
    // with a Transform component. The binding must not crash or corrupt the component.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    ffe::Transform& t = world.addComponent<ffe::Transform>(entity);
    t.position = {1.0f, 2.0f, 0.0f};
    t.scale    = {1.0f, 1.0f, 1.0f};
    t.rotation = 0.0f;

    fix.engine.setWorld(&world);

    // 0/0 produces NaN in Lua. setTransform must reject it and not crash.
    // The binding returns false (pushes boolean 0) on rejection, so doString succeeds
    // (the script itself does not error — the C function returns a false value).
    const std::string script =
        "ffe.setTransform(" + std::to_string(entity) + ", 0/0, 0, 0, 1, 1)";
    REQUIRE(fix.engine.doString(script.c_str()));

    // Original values must be unchanged — the rejection must be a no-write.
    REQUIRE(t.position.x == Catch::Approx(1.0f));
    REQUIRE(t.position.y == Catch::Approx(2.0f));
}

TEST_CASE("setTransform with Infinity is rejected without crash", "[scripting][ecs][security]") {
    // math.huge is +Inf in Lua. The std::isfinite check must catch it.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    ffe::Transform& t = world.addComponent<ffe::Transform>(entity);
    t.position = {3.0f, 4.0f, 0.0f};
    t.scale    = {1.0f, 1.0f, 1.0f};
    t.rotation = 0.0f;

    fix.engine.setWorld(&world);

    // math.huge is accessible (math library is whitelisted). Must not crash.
    const std::string script =
        "ffe.setTransform(" + std::to_string(entity) + ", math.huge, 0, 0, 1, 1)";
    REQUIRE(fix.engine.doString(script.c_str()));

    // Values must be unchanged.
    REQUIRE(t.position.x == Catch::Approx(3.0f));
    REQUIRE(t.position.y == Catch::Approx(4.0f));
}

// =============================================================================
// ECS bindings — getTransform with negative entity ID
// =============================================================================

TEST_CASE("ffe.getTransform returns nil for negative entity ID", "[scripting][ecs]") {
    // The binding rejects rawId < 0 before any World lookup, returning nil.
    // This must not crash even when a World is registered.
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("local t = ffe.getTransform(-1); assert(t == nil)"));
}

// =============================================================================
// Input API — negative key code
// =============================================================================

TEST_CASE("ffe.isKeyHeld with negative key code returns false", "[scripting][input]") {
    // The binding guards: code < 0 returns false immediately.
    ScriptFixture fix;
    ffe::initInput(nullptr);
    REQUIRE(fix.engine.doString("local v = ffe.isKeyHeld(-1); assert(v == false)"));
    ffe::shutdownInput();
}

// =============================================================================
// callFunction() — per-frame Lua dispatch
// =============================================================================

TEST_CASE("callFunction returns false when function does not exist", "[scripting][callfunction]") {
    // No function defined — callFunction must return false without crashing.
    ScriptFixture fix;
    REQUIRE_FALSE(fix.engine.callFunction("nonexistent", 0, 0.016));
}

TEST_CASE("callFunction returns true when function exists and succeeds", "[scripting][callfunction]") {
    // A valid no-op function — callFunction must return true.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("function ping(id, dt) return end"));
    REQUIRE(fix.engine.callFunction("ping", 0, 0.016));
}

TEST_CASE("callFunction passes entityId correctly", "[scripting][callfunction]") {
    // The function stores its first argument; we read it back via assert.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("function checkId(id, dt) lastId = id end"));
    REQUIRE(fix.engine.callFunction("checkId", 42, 0.0));
    // assert() is available via the base library whitelist.
    REQUIRE(fix.engine.doString("assert(lastId == 42)"));
}

TEST_CASE("callFunction passes dt correctly", "[scripting][callfunction]") {
    // The function stores its second argument; we verify it within float tolerance.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("function checkDt(id, dt) lastDt = dt end"));
    REQUIRE(fix.engine.callFunction("checkDt", 0, 0.016));
    REQUIRE(fix.engine.doString("assert(math.abs(lastDt - 0.016) < 0.0001)"));
}

TEST_CASE("callFunction returns false and does not crash when function errors", "[scripting][callfunction]") {
    // A Lua error inside the function must be caught by lua_pcall and returned as false.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("function boom(id, dt) error('test') end"));
    REQUIRE_FALSE(fix.engine.callFunction("boom", 0, 0.0));
    // Engine must remain operational after the error.
    REQUIRE(fix.engine.doString("local x = 1 + 1"));
}

// =============================================================================
// ffe.requestShutdown() — ShutdownSignal via ECS context
// =============================================================================

TEST_CASE("ffe.requestShutdown with World sets ShutdownSignal.requested", "[scripting][shutdown]") {
    // A World with ShutdownSignal registered in the context must have .requested
    // set to true after ffe.requestShutdown() is called from Lua.
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::ShutdownSignal>();
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.requestShutdown()"));
    REQUIRE(world.registry().ctx().get<ffe::ShutdownSignal>().requested == true);
}

TEST_CASE("ffe.requestShutdown without World is a no-op", "[scripting][shutdown]") {
    // No setWorld() call — ffe.requestShutdown() must not crash or error.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.requestShutdown()"));
}

// =============================================================================
// Entity lifecycle bindings — createEntity, destroyEntity,
// addTransform, addSprite, addPreviousTransform
// =============================================================================

TEST_CASE("ffe.createEntity returns an integer when World is registered", "[scripting][ecs][lifecycle]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    // createEntity must return a Lua integer (type 'number').
    REQUIRE(fix.engine.doString("local id = ffe.createEntity(); assert(type(id) == 'number')"));
}

TEST_CASE("ffe.createEntity returns nil when no World is registered", "[scripting][ecs][lifecycle]") {
    ScriptFixture fix;
    // No setWorld() — must return nil, not crash.
    REQUIRE(fix.engine.doString("local id = ffe.createEntity(); assert(id == nil)"));
}

TEST_CASE("ffe.destroyEntity destroys a valid entity", "[scripting][ecs][lifecycle]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // Destroy via Lua, then verify from C++ that the entity is no longer valid.
    const std::string script = "ffe.destroyEntity(" + std::to_string(entity) + ")";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.isValid(entity));
}

TEST_CASE("ffe.destroyEntity with invalid ID is a no-op without crash", "[scripting][ecs][lifecycle]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    // NULL_ENTITY (4294967295) is always invalid — must not crash.
    REQUIRE(fix.engine.doString("ffe.destroyEntity(4294967295)"));
}

TEST_CASE("ffe.destroyEntity with out-of-range ID (UINT32_MAX) is a no-op", "[scripting][ecs][lifecycle][security]") {
    // UINT32_MAX == 4294967295 is NULL_ENTITY and also equals UINT32_MAX.
    // The full two-sided check (H-1) treats it as out-of-range (> UINT32_MAX-1).
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("ffe.destroyEntity(4294967295)"));
}

TEST_CASE("ffe.addTransform succeeds on a valid entity without existing Transform", "[scripting][ecs][lifecycle]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addTransform(" + std::to_string(entity) + ", 1, 2, 0.5, 1, 1)\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(world.hasComponent<ffe::Transform>(entity));

    const ffe::Transform& t = world.getComponent<ffe::Transform>(entity);
    REQUIRE(t.position.x == Catch::Approx(1.0f));
    REQUIRE(t.position.y == Catch::Approx(2.0f));
    REQUIRE(t.rotation   == Catch::Approx(0.5f));
    REQUIRE(t.scale.x    == Catch::Approx(1.0f));
    REQUIRE(t.scale.y    == Catch::Approx(1.0f));
}

TEST_CASE("ffe.addTransform with NaN x returns false without crash", "[scripting][ecs][lifecycle][security]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // 0/0 is NaN in Lua. addTransform must reject it and return false.
    const std::string script =
        "local ok = ffe.addTransform(" + std::to_string(entity) + ", 0/0, 0, 0, 1, 1)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    // Entity must not have an invalid Transform as a side-effect.
    REQUIRE_FALSE(world.hasComponent<ffe::Transform>(entity));
}

TEST_CASE("ffe.addTransform overwrites existing Transform with warning", "[scripting][ecs][lifecycle]") {
    // H-2 guard: calling addTransform on an entity that already has Transform
    // must overwrite via emplace_or_replace (not UB).
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    world.addComponent<ffe::Transform>(entity);  // pre-existing Transform
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addTransform(" + std::to_string(entity) + ", 10, 20, 0, 2, 3)\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));

    const ffe::Transform& t = world.getComponent<ffe::Transform>(entity);
    REQUIRE(t.position.x == Catch::Approx(10.0f));
    REQUIRE(t.position.y == Catch::Approx(20.0f));
}

TEST_CASE("ffe.addSprite succeeds with a valid texture handle", "[scripting][ecs][lifecycle]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // Use handle id=1 — headless RHI ignores actual GPU validity.
    const std::string script =
        "local ok = ffe.addSprite(" + std::to_string(entity) + ", 1, 32, 32, 1, 1, 1, 1, 0)\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(world.hasComponent<ffe::Sprite>(entity));

    const ffe::Sprite& sprite = world.getComponent<ffe::Sprite>(entity);
    REQUIRE(sprite.texture.id == 1u);
    REQUIRE(sprite.size.x == Catch::Approx(32.0f));
    REQUIRE(sprite.size.y == Catch::Approx(32.0f));
}

TEST_CASE("ffe.addSprite with texture handle 0 returns false (null sentinel)", "[scripting][ecs][lifecycle][security]") {
    // M-1: rawHandle <= 0 must be rejected; 0 is TextureHandle null sentinel.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addSprite(" + std::to_string(entity) + ", 0, 32, 32, 1, 1, 1, 1, 0)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::Sprite>(entity));
}

TEST_CASE("ffe.addPreviousTransform copies position from existing Transform", "[scripting][ecs][lifecycle]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    ffe::Transform& t = world.addComponent<ffe::Transform>(entity);
    t.position = {5.0f, 10.0f, 0.0f};
    t.scale    = {2.0f, 3.0f, 1.0f};
    t.rotation = 1.0f;
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addPreviousTransform(" + std::to_string(entity) + ")\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(world.hasComponent<ffe::PreviousTransform>(entity));

    const ffe::PreviousTransform& pt = world.getComponent<ffe::PreviousTransform>(entity);
    REQUIRE(pt.position.x == Catch::Approx(5.0f));
    REQUIRE(pt.position.y == Catch::Approx(10.0f));
    REQUIRE(pt.rotation   == Catch::Approx(1.0f));
    REQUIRE(pt.scale.x    == Catch::Approx(2.0f));
    REQUIRE(pt.scale.y    == Catch::Approx(3.0f));
}

TEST_CASE("ffe.addPreviousTransform on entity without Transform sets zero position", "[scripting][ecs][lifecycle]") {
    // If there is no Transform, PreviousTransform should be zeroed and a warning logged.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    // No Transform added.
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addPreviousTransform(" + std::to_string(entity) + ")\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(world.hasComponent<ffe::PreviousTransform>(entity));

    const ffe::PreviousTransform& pt = world.getComponent<ffe::PreviousTransform>(entity);
    REQUIRE(pt.position.x == Catch::Approx(0.0f));
    REQUIRE(pt.position.y == Catch::Approx(0.0f));
    REQUIRE(pt.rotation   == Catch::Approx(0.0f));
}

TEST_CASE("ffe.createEntity and ffe.getTransform round-trip via Lua", "[scripting][ecs][lifecycle]") {
    // Full round-trip: create entity in Lua, add transform in Lua, read it back.
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    const std::string script =
        "local id = ffe.createEntity()\n"
        "assert(id ~= nil)\n"
        "local ok = ffe.addTransform(id, 7, 14, 0, 1, 1)\n"
        "assert(ok == true)\n"
        "local t = ffe.getTransform(id)\n"
        "assert(t ~= nil)\n"
        "assert(math.abs(t.x - 7) < 0.001)\n"
        "assert(math.abs(t.y - 14) < 0.001)\n";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.destroyEntity with negative ID is a no-op", "[scripting][ecs][lifecycle][security]") {
    // H-1: negative IDs must be rejected immediately, no crash.
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    REQUIRE(fix.engine.doString("ffe.destroyEntity(-1)"));
}

// =============================================================================
// Additional lifecycle coverage — gaps identified in test review
// =============================================================================

TEST_CASE("ffe.addTransform with Infinity (math.huge) returns false without crash", "[scripting][ecs][lifecycle][security]") {
    // addTransform has the same std::isfinite guard as setTransform.
    // +Inf via math.huge must be rejected and return false.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addTransform(" + std::to_string(entity) + ", math.huge, 0, 0, 1, 1)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    // Entity must not have gained a Transform as a side-effect of the rejected call.
    REQUIRE_FALSE(world.hasComponent<ffe::Transform>(entity));
}

TEST_CASE("ffe.addSprite with texture handle UINT32_MAX+1 returns false (overflow rejection)", "[scripting][ecs][lifecycle][security]") {
    // The binding checks rawHandle > UINT32_MAX. Lua integers are 64-bit on all
    // modern platforms so UINT32_MAX+1 (4294967296) is representable and must be
    // rejected to prevent a silent truncation to 0 when cast to u32.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // 4294967296 == UINT32_MAX + 1 == 2^32
    const std::string script =
        "local ok = ffe.addSprite(" + std::to_string(entity) + ", 4294967296, 32, 32, 1, 1, 1, 1, 0)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::Sprite>(entity));
}

TEST_CASE("ffe.addPreviousTransform called twice on same entity is safe (H-2 overwrite)", "[scripting][ecs][lifecycle]") {
    // H-2: emplace_or_replace must not UB, crash, or corrupt state on a second call.
    // The second call should silently overwrite and return true.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    ffe::Transform& t = world.addComponent<ffe::Transform>(entity);
    t.position = {1.0f, 2.0f, 0.0f};
    t.scale    = {1.0f, 1.0f, 1.0f};
    t.rotation = 0.0f;
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok1 = ffe.addPreviousTransform(" + std::to_string(entity) + ")\n"
        "assert(ok1 == true)\n"
        "local ok2 = ffe.addPreviousTransform(" + std::to_string(entity) + ")\n"
        "assert(ok2 == true)";
    REQUIRE(fix.engine.doString(script.c_str()));

    // Component must still be valid and readable from C++.
    REQUIRE(world.hasComponent<ffe::PreviousTransform>(entity));
    const ffe::PreviousTransform& pt = world.getComponent<ffe::PreviousTransform>(entity);
    REQUIRE(pt.position.x == Catch::Approx(1.0f));
    REQUIRE(pt.position.y == Catch::Approx(2.0f));
}

TEST_CASE("ffe.addTransform on destroyed entity returns false", "[scripting][ecs][lifecycle]") {
    // After destroyEntity, the entity ID is invalid. addTransform must detect this
    // via isValid() and return false without crashing.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    world.destroyEntity(entity);  // destroy from C++ before Lua call
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addTransform(" + std::to_string(entity) + ", 1, 2, 0, 1, 1)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.addSprite with zero width returns false (invalid dimension)", "[scripting][ecs][lifecycle][security]") {
    // The addSprite binding enforces width > 0 (SEC-E-6). Zero width must be rejected.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addSprite(" + std::to_string(entity) + ", 1, 0, 32, 1, 1, 1, 1, 0)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::Sprite>(entity));
}

// =============================================================================
// ffe.loadTexture / ffe.unloadTexture bindings
// =============================================================================

TEST_CASE("ffe.loadTexture exists as a callable in the ffe table", "[scripting][texture]") {
    // The binding must be registered — type() of a C function is 'function'.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(type(ffe.loadTexture) == 'function')"));
}

TEST_CASE("ffe.loadTexture with nil argument returns nil without crash", "[scripting][texture][security]") {
    // Passing nil (no argument) must return nil, not crash.
    // The type guard (MEDIUM-1) fires: lua_type(state, 1) is LUA_TNIL, not LUA_TSTRING.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local h = ffe.loadTexture(nil); assert(h == nil)"));
}

TEST_CASE("ffe.loadTexture with a number argument returns nil (MEDIUM-1 type guard)", "[scripting][texture][security]") {
    // MEDIUM-1: lua_tostring silently coerces numbers to strings, which could forward
    // "0" as a path. The explicit lua_type check must reject non-string arguments.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local h = ffe.loadTexture(0); assert(h == nil)"));
    REQUIRE(fix.engine.doString("local h = ffe.loadTexture(42); assert(h == nil)"));
}

TEST_CASE("ffe.loadTexture with a boolean argument returns nil (MEDIUM-1 type guard)", "[scripting][texture][security]") {
    // Same MEDIUM-1 guard — boolean coercion must not reach C++.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local h = ffe.loadTexture(true); assert(h == nil)"));
}

TEST_CASE("ffe.loadTexture with a path traversal string returns nil", "[scripting][texture][security]") {
    // Path traversal must be rejected by the C++ isPathSafe() layer. The binding
    // forwards the string to C++ (which returns an invalid handle), and the binding
    // converts id=0 to nil. No crash, no file opened.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local h = ffe.loadTexture('../../../etc/passwd'); assert(h == nil)"));
}

TEST_CASE("ffe.unloadTexture with handle = 0 is a no-op", "[scripting][texture][security]") {
    // LOW-2: handle 0 is the null sentinel — must be rejected before calling C++.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.unloadTexture(0)"));
}

TEST_CASE("ffe.unloadTexture with handle = -1 is a no-op", "[scripting][texture][security]") {
    // LOW-2: negative handles are out of range — must be rejected.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.unloadTexture(-1)"));
}

TEST_CASE("ffe.unloadTexture with handle > UINT32_MAX is a no-op", "[scripting][texture][security]") {
    // LOW-2: 4294967296 == UINT32_MAX + 1 == 2^32. lua_Integer is 64-bit, so this
    // value is representable and must be range-checked to prevent truncation to 0.
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.unloadTexture(4294967296)"));
}

TEST_CASE("ffe.loadTexture with valid file returns a non-nil integer handle", "[scripting][texture][requires_rhi]") {
    // Full integration: headless RHI initialised, asset root set from C++,
    // then ffe.loadTexture("checkerboard.png") called from Lua.
    // The result must be a non-nil integer (the u32 texture handle id).
    //
    // NOTE: This test requires a display-capable GL context OR headless RHI support.
    // The headless RHI returns valid handles (stubs), so this works without a display.

    ffe::rhi::RhiConfig config;
    config.headless = true;
    const ffe::rhi::RhiResult result = ffe::rhi::init(config);
    if (result != ffe::rhi::RhiResult::OK) {
        // RHI unavailable — skip gracefully.
        SUCCEED("RHI not available in this environment — skipping texture load test");
        return;
    }

    // setAssetRoot has write-once semantics. This test process may already have
    // a root set (from a prior test run in the same process). Use the two-argument
    // overload via a direct C++ call to validate the path, then set root if needed.
    // Because catch_discover_tests runs each TEST_CASE in a fresh subprocess, the
    // write-once global is clean when this test runs.
    const bool rootSet = ffe::renderer::setAssetRoot("/home/nigel/FastFreeEngine/assets/textures");
    if (!rootSet && ffe::renderer::getAssetRoot()[0] == '\0') {
        // Root was not set and the current root is empty — cannot proceed.
        ffe::rhi::shutdown();
        SUCCEED("setAssetRoot failed and no root is set — skipping");
        return;
    }

    ScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local h = ffe.loadTexture('checkerboard.png')\n"
        "assert(h ~= nil, 'expected non-nil handle from loadTexture')\n"
        "assert(type(h) == 'number', 'expected integer handle')\n"
        "assert(h > 0, 'expected positive handle id')\n"
        // Clean up the texture immediately.
        "ffe.unloadTexture(h)\n"
    ));

    ffe::rhi::shutdown();
}
