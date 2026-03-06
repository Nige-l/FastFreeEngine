#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
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

// =============================================================================
// Lua shutdown() callback — called by ScriptEngine::shutdown() before lua_close
// =============================================================================

TEST_CASE("Lua shutdown() function is called when ScriptEngine::shutdown() is invoked", "[scripting][lifecycle][shutdown_cb]") {
    // Verify the callback fires by having it set ShutdownSignal via ffe.requestShutdown().
    // After engine.shutdown() returns the Lua state is gone, but the World is still alive
    // in C++ — so we can read ShutdownSignal.requested from there.
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());

    ffe::World world;
    world.registry().ctx().emplace<ffe::ShutdownSignal>();
    engine.setWorld(&world);

    // Define the Lua shutdown() callback: it calls ffe.requestShutdown().
    REQUIRE(engine.doString("function shutdown() ffe.requestShutdown() end"));

    // At this point the signal must be false — shutdown() hasn't been called yet.
    REQUIRE_FALSE(world.registry().ctx().get<ffe::ShutdownSignal>().requested);

    // Trigger the C++ shutdown — this must invoke the Lua shutdown() callback.
    engine.shutdown();

    // The callback must have run and set the signal to true.
    REQUIRE(world.registry().ctx().get<ffe::ShutdownSignal>().requested == true);
}

TEST_CASE("Missing Lua shutdown() function is a silent no-op", "[scripting][lifecycle][shutdown_cb]") {
    // No shutdown() function defined — ScriptEngine::shutdown() must complete normally
    // without logging an error or crashing.
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());
    // No shutdown() function in Lua state — must be a silent no-op.
    engine.shutdown();
    REQUIRE_FALSE(engine.isInitialised());
}

TEST_CASE("Erroring Lua shutdown() function does not crash the engine", "[scripting][lifecycle][shutdown_cb]") {
    // A Lua error inside shutdown() must be caught and logged (error is swallowed).
    // The C++ engine must continue its own shutdown to completion.
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());

    // Define a shutdown() function that throws a Lua error.
    REQUIRE(engine.doString("function shutdown() error('deliberate shutdown error') end"));

    // Must not crash — error is caught by lua_pcall and logged.
    engine.shutdown();

    // Engine must have completed shutdown (isInitialised returns false after shutdown).
    REQUIRE_FALSE(engine.isInitialised());
}

TEST_CASE("Lua shutdown() function can call ffe.log() — Lua state is valid during callback", "[scripting][lifecycle][shutdown_cb]") {
    // Verifies that the ffe table and its functions are still accessible when
    // ScriptEngine::shutdown() invokes the Lua shutdown() callback. The Lua state
    // is fully valid at that point — lua_close has NOT been called yet.
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());

    // shutdown() calls ffe.log — if the ffe table is gone this will error.
    REQUIRE(engine.doString("function shutdown() ffe.log('shutdown callback ran') end"));

    // Must not crash or log a Lua error (ffe.log must succeed inside shutdown()).
    engine.shutdown();

    REQUIRE_FALSE(engine.isInitialised());
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

// =============================================================================
// ffe.fillTransform — zero-allocation transform query
// =============================================================================

TEST_CASE("ffe.fillTransform returns true and fills table when entity has Transform", "[scripting][ecs][fillTransform]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    ffe::Transform& t = world.addComponent<ffe::Transform>(entity);
    t.position = {3.0f, 7.0f, 0.0f};
    t.rotation = 1.5f;
    t.scale    = {2.0f, 4.0f, 1.0f};
    fix.engine.setWorld(&world);

    const std::string script =
        "local buf = {}\n"
        "local ok = ffe.fillTransform(" + std::to_string(entity) + ", buf)\n"
        "assert(ok == true)\n"
        "assert(math.abs(buf.x - 3.0) < 0.001)\n"
        "assert(math.abs(buf.y - 7.0) < 0.001)\n"
        "assert(math.abs(buf.rotation - 1.5) < 0.001)\n"
        "assert(math.abs(buf.scaleX - 2.0) < 0.001)\n"
        "assert(math.abs(buf.scaleY - 4.0) < 0.001)\n";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.fillTransform returns false when entity is invalid", "[scripting][ecs][fillTransform]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // 4294967295 is NULL_ENTITY — always invalid.
    REQUIRE(fix.engine.doString(
        "local buf = {}\n"
        "local ok = ffe.fillTransform(4294967295, buf)\n"
        "assert(ok == false)\n"
    ));
}

TEST_CASE("ffe.fillTransform returns false when entity has no Transform", "[scripting][ecs][fillTransform]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    // No Transform added.
    fix.engine.setWorld(&world);

    const std::string script =
        "local buf = {}\n"
        "local ok = ffe.fillTransform(" + std::to_string(entity) + ", buf)\n"
        "assert(ok == false)\n";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.fillTransform returns false when no World is set", "[scripting][ecs][fillTransform]") {
    ScriptFixture fix;
    // No setWorld() call.
    REQUIRE(fix.engine.doString(
        "local buf = {}\n"
        "local ok = ffe.fillTransform(0, buf)\n"
        "assert(ok == false)\n"
    ));
}

TEST_CASE("ffe.fillTransform with non-integer arg 1 raises Lua error", "[scripting][ecs][fillTransform]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Passing a string where an integer is expected — luaL_checkinteger raises a Lua error.
    REQUIRE_FALSE(fix.engine.doString("ffe.fillTransform('hello', {})"));
}

TEST_CASE("ffe.fillTransform with non-table arg 2 raises Lua error", "[scripting][ecs][fillTransform]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Passing a number where a table is expected — luaL_checktype raises a Lua error.
    REQUIRE_FALSE(fix.engine.doString("ffe.fillTransform(0, 42)"));
}

TEST_CASE("ffe.fillTransform with negative entity ID returns false", "[scripting][ecs][fillTransform]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "local buf = {}\n"
        "local ok = ffe.fillTransform(-1, buf)\n"
        "assert(ok == false)\n"
    ));
}

TEST_CASE("ffe.fillTransform table values match C++ Transform component", "[scripting][ecs][fillTransform]") {
    // Verify exact match between C++ component values and Lua table values.
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    ffe::Transform& t = world.addComponent<ffe::Transform>(entity);
    t.position = {-10.5f, 200.25f, 0.0f};
    t.rotation = -3.14f;
    t.scale    = {0.5f, 0.75f, 1.0f};
    fix.engine.setWorld(&world);

    const std::string script =
        "local buf = {}\n"
        "local ok = ffe.fillTransform(" + std::to_string(entity) + ", buf)\n"
        "assert(ok == true)\n"
        "assert(math.abs(buf.x - (-10.5)) < 0.001)\n"
        "assert(math.abs(buf.y - 200.25) < 0.001)\n"
        "assert(math.abs(buf.rotation - (-3.14)) < 0.01)\n"
        "assert(math.abs(buf.scaleX - 0.5) < 0.001)\n"
        "assert(math.abs(buf.scaleY - 0.75) < 0.001)\n";
    REQUIRE(fix.engine.doString(script.c_str()));
}

// =============================================================================
// ffe.loadSound — type guard, path safety, and rejection tests
// =============================================================================

TEST_CASE("ffe.loadSound exists as a callable in the ffe table", "[scripting][audio]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(type(ffe.loadSound) == 'function')"));
}

TEST_CASE("ffe.loadSound with non-string argument returns nil (type guard)", "[scripting][audio][security]") {
    ScriptFixture fix;
    // Number: lua_type check rejects before lua_tostring coercion.
    REQUIRE(fix.engine.doString("local h = ffe.loadSound(42); assert(h == nil)"));
    // Boolean:
    REQUIRE(fix.engine.doString("local h = ffe.loadSound(true); assert(h == nil)"));
    // nil (no argument):
    REQUIRE(fix.engine.doString("local h = ffe.loadSound(nil); assert(h == nil)"));
    // Table:
    REQUIRE(fix.engine.doString("local h = ffe.loadSound({}); assert(h == nil)"));
}

TEST_CASE("ffe.loadSound with path traversal returns nil", "[scripting][audio][security]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local h = ffe.loadSound('../evil.wav'); assert(h == nil)"));
    REQUIRE(fix.engine.doString("local h = ffe.loadSound('../../etc/passwd'); assert(h == nil)"));
}

TEST_CASE("ffe.loadSound with absolute path returns nil", "[scripting][audio][security]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local h = ffe.loadSound('/etc/passwd'); assert(h == nil)"));
}

TEST_CASE("ffe.loadSound with empty string returns nil", "[scripting][audio][security]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("local h = ffe.loadSound(''); assert(h == nil)"));
}

// =============================================================================
// ffe.unloadSound — handle validation
// =============================================================================

TEST_CASE("ffe.unloadSound exists as a callable in the ffe table", "[scripting][audio]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(type(ffe.unloadSound) == 'function')"));
}

TEST_CASE("ffe.unloadSound rejects handle <= 0", "[scripting][audio][security]") {
    ScriptFixture fix;
    // handle = 0: null sentinel — must be rejected (no-op).
    REQUIRE(fix.engine.doString("ffe.unloadSound(0)"));
    // handle = -1: negative — must be rejected (no-op).
    REQUIRE(fix.engine.doString("ffe.unloadSound(-1)"));
    // handle = -999: large negative — must be rejected (no-op).
    REQUIRE(fix.engine.doString("ffe.unloadSound(-999)"));
}

TEST_CASE("ffe.unloadSound rejects handle > UINT32_MAX", "[scripting][audio][security]") {
    ScriptFixture fix;
    // 4294967296 == UINT32_MAX + 1 == 2^32. lua_Integer is 64-bit,
    // so this value is representable and must be range-checked.
    REQUIRE(fix.engine.doString("ffe.unloadSound(4294967296)"));
}

TEST_CASE("ffe.unloadSound with non-integer argument raises Lua error", "[scripting][audio]") {
    ScriptFixture fix;
    // luaL_checkinteger raises a Lua error for non-integer args.
    REQUIRE_FALSE(fix.engine.doString("ffe.unloadSound('hello')"));
}

// =============================================================================
// ffe.playSound — handle validation and optional volume
// =============================================================================

TEST_CASE("ffe.playSound exists as a callable in the ffe table", "[scripting][audio]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(type(ffe.playSound) == 'function')"));
}

TEST_CASE("ffe.playSound rejects handle <= 0", "[scripting][audio][security]") {
    ScriptFixture fix;
    // handle = 0: null sentinel — rejected.
    REQUIRE(fix.engine.doString("ffe.playSound(0)"));
    // handle = -1: negative — rejected.
    REQUIRE(fix.engine.doString("ffe.playSound(-1)"));
}

TEST_CASE("ffe.playSound with optional volume parameter does not crash", "[scripting][audio]") {
    // Without audio init, playSound on a valid-range handle is a no-op,
    // but it must not crash. We test that the volume path parses correctly.
    ScriptFixture fix;
    // Valid handle range (1) + explicit volume — must not crash or error.
    REQUIRE(fix.engine.doString("ffe.playSound(1, 0.5)"));
    // Valid handle range (1) + no volume (default 1.0) — must not crash.
    REQUIRE(fix.engine.doString("ffe.playSound(1)"));
}

TEST_CASE("ffe.playSound with non-integer handle raises Lua error", "[scripting][audio]") {
    ScriptFixture fix;
    // luaL_checkinteger raises a Lua error for non-integer args.
    REQUIRE_FALSE(fix.engine.doString("ffe.playSound('hello')"));
}

TEST_CASE("ffe.playSound rejects handle > UINT32_MAX", "[scripting][audio][security]") {
    ScriptFixture fix;
    // 4294967296 == 2^32 — must be rejected as out of range.
    REQUIRE(fix.engine.doString("ffe.playSound(4294967296)"));
}

// =============================================================================
// ffe.setMasterVolume — type validation
// =============================================================================

TEST_CASE("ffe.setMasterVolume exists as a callable in the ffe table", "[scripting][audio]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(type(ffe.setMasterVolume) == 'function')"));
}

TEST_CASE("ffe.setMasterVolume accepts a number argument", "[scripting][audio]") {
    ScriptFixture fix;
    // Valid volume values — must not crash or error.
    REQUIRE(fix.engine.doString("ffe.setMasterVolume(0.5)"));
    REQUIRE(fix.engine.doString("ffe.setMasterVolume(0.0)"));
    REQUIRE(fix.engine.doString("ffe.setMasterVolume(1.0)"));
}

TEST_CASE("ffe.setMasterVolume rejects non-number argument (Lua error)", "[scripting][audio]") {
    ScriptFixture fix;
    // luaL_checknumber raises a Lua error for non-number args.
    REQUIRE_FALSE(fix.engine.doString("ffe.setMasterVolume('loud')"));
    REQUIRE_FALSE(fix.engine.doString("ffe.setMasterVolume(true)"));
    REQUIRE_FALSE(fix.engine.doString("ffe.setMasterVolume(nil)"));
}

// =============================================================================
// ffe.addSpriteAnimation — sprite animation component from Lua
// =============================================================================

TEST_CASE("ffe.addSpriteAnimation returns true for valid entity with Sprite", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    // addSpriteAnimation does not require Sprite, but we add one to match typical usage.
    ffe::Sprite& s = world.addComponent<ffe::Sprite>(entity);
    s.size = {32.0f, 32.0f};
    s.texture = ffe::rhi::TextureHandle{1};
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addSpriteAnimation(" + std::to_string(entity) + ", 4, 2, 0.1, true)\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(world.hasComponent<ffe::SpriteAnimation>(entity));

    const ffe::SpriteAnimation& anim = world.getComponent<ffe::SpriteAnimation>(entity);
    REQUIRE(anim.frameCount == 4);
    REQUIRE(anim.columns == 2);
    REQUIRE(anim.frameTime == Catch::Approx(0.1f));
    REQUIRE(anim.looping == true);
    REQUIRE(anim.playing == false);  // must start stopped
    REQUIRE(anim.currentFrame == 0);
}

TEST_CASE("ffe.addSpriteAnimation returns false for invalid entity", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Entity 999999 does not exist in the world.
    REQUIRE(fix.engine.doString(
        "local ok = ffe.addSpriteAnimation(999999, 4, 2, 0.1, true)\n"
        "assert(ok == false)"));
}

TEST_CASE("ffe.addSpriteAnimation rejects frameCount <= 0", "[scripting][animation][security]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addSpriteAnimation(" + std::to_string(entity) + ", 0, 1, 0.1, true)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::SpriteAnimation>(entity));
}

TEST_CASE("ffe.addSpriteAnimation rejects negative frameCount", "[scripting][animation][security]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addSpriteAnimation(" + std::to_string(entity) + ", -1, 1, 0.1, true)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::SpriteAnimation>(entity));
}

TEST_CASE("ffe.addSpriteAnimation rejects columns <= 0", "[scripting][animation][security]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addSpriteAnimation(" + std::to_string(entity) + ", 4, 0, 0.1, true)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::SpriteAnimation>(entity));
}

TEST_CASE("ffe.addSpriteAnimation rejects columns > frameCount", "[scripting][animation][security]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // frameCount=4, columns=5 -> invalid
    const std::string script =
        "local ok = ffe.addSpriteAnimation(" + std::to_string(entity) + ", 4, 5, 0.1, true)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::SpriteAnimation>(entity));
}

TEST_CASE("ffe.addSpriteAnimation rejects frameTime <= 0", "[scripting][animation][security]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addSpriteAnimation(" + std::to_string(entity) + ", 4, 2, 0.0, true)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::SpriteAnimation>(entity));

    // Also negative
    const std::string script2 =
        "local ok = ffe.addSpriteAnimation(" + std::to_string(entity) + ", 4, 2, -0.5, true)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script2.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::SpriteAnimation>(entity));
}

TEST_CASE("ffe.addSpriteAnimation rejects non-finite frameTime (inf/nan)", "[scripting][animation][security]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // math.huge is Lua's infinity
    const std::string script =
        "local ok = ffe.addSpriteAnimation(" + std::to_string(entity) + ", 4, 2, math.huge, true)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::SpriteAnimation>(entity));

    // 0/0 is NaN in Lua
    const std::string script2 =
        "local ok = ffe.addSpriteAnimation(" + std::to_string(entity) + ", 4, 2, 0/0, true)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script2.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::SpriteAnimation>(entity));
}

// =============================================================================
// ffe.playAnimation / ffe.stopAnimation
// =============================================================================

TEST_CASE("ffe.playAnimation sets playing state", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // Add animation via C++ for direct setup
    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(entity);
    anim.frameCount = 4;
    anim.columns = 2;
    anim.frameTime = 0.1f;
    anim.playing = false;

    const std::string script =
        "ffe.playAnimation(" + std::to_string(entity) + ")";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(anim.playing == true);
    REQUIRE(anim.elapsed == Catch::Approx(0.0f));  // elapsed reset on play
}

TEST_CASE("ffe.stopAnimation clears playing state", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(entity);
    anim.frameCount = 4;
    anim.columns = 2;
    anim.frameTime = 0.1f;
    anim.playing = true;

    const std::string script =
        "ffe.stopAnimation(" + std::to_string(entity) + ")";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(anim.playing == false);
}

TEST_CASE("ffe.playAnimation no-op for invalid entity", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Entity 999999 does not exist — must not crash, returns nothing.
    REQUIRE(fix.engine.doString("ffe.playAnimation(999999)"));
}

TEST_CASE("ffe.stopAnimation no-op for invalid entity", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.stopAnimation(999999)"));
}

TEST_CASE("ffe.playAnimation no-op for entity without SpriteAnimation", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // Entity exists but has no SpriteAnimation — must not crash.
    const std::string script =
        "ffe.playAnimation(" + std::to_string(entity) + ")";
    REQUIRE(fix.engine.doString(script.c_str()));
}

// =============================================================================
// ffe.setAnimationFrame
// =============================================================================

TEST_CASE("ffe.setAnimationFrame clamps frame to valid range", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(entity);
    anim.frameCount = 4;
    anim.columns = 4;
    anim.frameTime = 0.1f;
    // Also add Sprite so UV update path runs.
    ffe::Sprite& spr = world.addComponent<ffe::Sprite>(entity);
    spr.size = {32.0f, 32.0f};
    spr.texture = ffe::rhi::TextureHandle{1};

    // Set frame within range
    std::string script =
        "ffe.setAnimationFrame(" + std::to_string(entity) + ", 2)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(anim.currentFrame == 2);

    // Set frame above range — should clamp to frameCount-1 = 3
    script = "ffe.setAnimationFrame(" + std::to_string(entity) + ", 100)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(anim.currentFrame == 3);

    // Set frame below range (negative) — should clamp to 0
    script = "ffe.setAnimationFrame(" + std::to_string(entity) + ", -5)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(anim.currentFrame == 0);
}

TEST_CASE("ffe.setAnimationFrame updates UVs immediately", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(entity);
    anim.frameCount = 4;
    anim.columns = 2;  // 2 columns, 2 rows
    anim.frameTime = 0.1f;

    ffe::Sprite& spr = world.addComponent<ffe::Sprite>(entity);
    spr.size = {32.0f, 32.0f};
    spr.texture = ffe::rhi::TextureHandle{1};

    // Set to frame 3 (row=1, col=1 in a 2x2 grid)
    const std::string script =
        "ffe.setAnimationFrame(" + std::to_string(entity) + ", 3)";
    REQUIRE(fix.engine.doString(script.c_str()));

    // Expected: col=3%2=1, row=3/2=1, uWidth=0.5, vHeight=0.5
    REQUIRE(spr.uvMin.x == Catch::Approx(0.5f));
    REQUIRE(spr.uvMin.y == Catch::Approx(0.5f));
    REQUIRE(spr.uvMax.x == Catch::Approx(1.0f));
    REQUIRE(spr.uvMax.y == Catch::Approx(1.0f));
}

TEST_CASE("ffe.setAnimationFrame no-op for entity without SpriteAnimation", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // Entity exists but has no SpriteAnimation — must not crash.
    const std::string script =
        "ffe.setAnimationFrame(" + std::to_string(entity) + ", 0)";
    REQUIRE(fix.engine.doString(script.c_str()));
}

// =============================================================================
// ffe.isAnimationPlaying
// =============================================================================

TEST_CASE("ffe.isAnimationPlaying returns false before playAnimation", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(entity);
    anim.frameCount = 4;
    anim.columns = 2;
    anim.frameTime = 0.1f;
    anim.playing = false;

    const std::string script =
        "assert(ffe.isAnimationPlaying(" + std::to_string(entity) + ") == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.isAnimationPlaying returns true after playAnimation", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(entity);
    anim.frameCount = 4;
    anim.columns = 2;
    anim.frameTime = 0.1f;
    anim.playing = false;

    const std::string script =
        "ffe.playAnimation(" + std::to_string(entity) + ")\n"
        "assert(ffe.isAnimationPlaying(" + std::to_string(entity) + ") == true)";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.isAnimationPlaying returns false after stopAnimation", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(entity);
    anim.frameCount = 4;
    anim.columns = 2;
    anim.frameTime = 0.1f;
    anim.playing = false;

    const std::string script =
        "ffe.playAnimation(" + std::to_string(entity) + ")\n"
        "ffe.stopAnimation(" + std::to_string(entity) + ")\n"
        "assert(ffe.isAnimationPlaying(" + std::to_string(entity) + ") == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.isAnimationPlaying returns false for entity without SpriteAnimation", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "assert(ffe.isAnimationPlaying(" + std::to_string(entity) + ") == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.isAnimationPlaying returns false for invalid entity", "[scripting][animation]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("assert(ffe.isAnimationPlaying(999999) == false)"));
}

// =============================================================================
// Collision bindings — addCollider / removeCollider / setCollisionCallback
// =============================================================================

TEST_CASE("ffe.addCollider returns true for valid entity with AABB params", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addCollider(" + std::to_string(entity) + ", 'aabb', 16, 16)\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(world.hasComponent<ffe::Collider2D>(entity));
    const auto& col = world.getComponent<ffe::Collider2D>(entity);
    REQUIRE(col.shape == ffe::ColliderShape::AABB);
    REQUIRE(col.halfWidth == Catch::Approx(16.0f));
    REQUIRE(col.halfHeight == Catch::Approx(16.0f));
}

TEST_CASE("ffe.addCollider returns true for valid entity with circle params", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addCollider(" + std::to_string(entity) + ", 'circle', 10, 0)\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));
    const auto& col = world.getComponent<ffe::Collider2D>(entity);
    REQUIRE(col.shape == ffe::ColliderShape::CIRCLE);
    REQUIRE(col.halfWidth == Catch::Approx(10.0f));
}

TEST_CASE("ffe.addCollider returns false for invalid entity", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Entity 999999 does not exist.
    REQUIRE(fix.engine.doString(
        "local ok = ffe.addCollider(999999, 'aabb', 16, 16)\n"
        "assert(ok == false)"));
}

TEST_CASE("ffe.addCollider rejects invalid shape string", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addCollider(" + std::to_string(entity) + ", 'polygon', 16, 16)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::Collider2D>(entity));
}

TEST_CASE("ffe.addCollider rejects negative halfWidth", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addCollider(" + std::to_string(entity) + ", 'aabb', -5, 16)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::Collider2D>(entity));
}

TEST_CASE("ffe.addCollider rejects zero halfWidth", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addCollider(" + std::to_string(entity) + ", 'aabb', 0, 16)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.addCollider rejects negative halfHeight", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addCollider(" + std::to_string(entity) + ", 'aabb', 16, -1)\n"
        "assert(ok == false)";
    REQUIRE(fix.engine.doString(script.c_str()));
}

TEST_CASE("ffe.addCollider defaults layer=0xFFFF, mask=0xFFFF, isTrigger=false", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addCollider(" + std::to_string(entity) + ", 'aabb', 8, 8)\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));

    const auto& col = world.getComponent<ffe::Collider2D>(entity);
    REQUIRE(col.layer == 0xFFFF);
    REQUIRE(col.mask == 0xFFFF);
    REQUIRE(col.isTrigger == false);
}

TEST_CASE("ffe.addCollider with explicit layer, mask, and isTrigger", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "local ok = ffe.addCollider(" + std::to_string(entity) + ", 'aabb', 8, 8, 1, 2, true)\n"
        "assert(ok == true)";
    REQUIRE(fix.engine.doString(script.c_str()));

    const auto& col = world.getComponent<ffe::Collider2D>(entity);
    REQUIRE(col.layer == 1);
    REQUIRE(col.mask == 2);
    REQUIRE(col.isTrigger == true);
}

TEST_CASE("ffe.removeCollider removes Collider2D from entity", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    world.addComponent<ffe::Collider2D>(entity);
    REQUIRE(world.hasComponent<ffe::Collider2D>(entity));

    fix.engine.setWorld(&world);
    const std::string script = "ffe.removeCollider(" + std::to_string(entity) + ")";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE_FALSE(world.hasComponent<ffe::Collider2D>(entity));
}

TEST_CASE("ffe.removeCollider is no-op for entity without collider", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    // No Collider2D added.
    fix.engine.setWorld(&world);

    const std::string script = "ffe.removeCollider(" + std::to_string(entity) + ")";
    REQUIRE(fix.engine.doString(script.c_str())); // Must not crash.
}

TEST_CASE("ffe.setCollisionCallback accepts a function", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.setCollisionCallback(function(a, b) end)"));
}

TEST_CASE("ffe.setCollisionCallback accepts nil to unregister", "[scripting][collision]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Register, then unregister with nil.
    REQUIRE(fix.engine.doString("ffe.setCollisionCallback(function(a, b) end)"));
    REQUIRE(fix.engine.doString("ffe.setCollisionCallback(nil)"));
}

// =============================================================================
// ffe.setHudText — HUD text buffer from Lua
// =============================================================================

TEST_CASE("ffe.setHudText sets text and verifies HudTextBuffer contents", "[scripting][hud]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::HudTextBuffer>();
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.setHudText('hello')"));

    const auto* buf = world.registry().ctx().find<ffe::HudTextBuffer>();
    REQUIRE(buf != nullptr);
    CHECK(std::string(buf->text) == "hello");
}

TEST_CASE("ffe.setHudText clears text with empty string", "[scripting][hud]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::HudTextBuffer>();
    fix.engine.setWorld(&world);

    // Set some text first, then clear it.
    REQUIRE(fix.engine.doString("ffe.setHudText('something')"));
    REQUIRE(fix.engine.doString("ffe.setHudText('')"));

    const auto* buf = world.registry().ctx().find<ffe::HudTextBuffer>();
    REQUIRE(buf != nullptr);
    CHECK(std::string(buf->text).empty());
}

TEST_CASE("ffe.setHudText handles nil argument (clears text)", "[scripting][hud]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::HudTextBuffer>();
    fix.engine.setWorld(&world);

    // Set text, then pass nil — must clear without crashing.
    REQUIRE(fix.engine.doString("ffe.setHudText('before')"));
    REQUIRE(fix.engine.doString("ffe.setHudText(nil)"));

    const auto* buf = world.registry().ctx().find<ffe::HudTextBuffer>();
    REQUIRE(buf != nullptr);
    CHECK(std::string(buf->text).empty());
}

TEST_CASE("ffe.setHudText handles non-string argument (clears text)", "[scripting][hud]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::HudTextBuffer>();
    fix.engine.setWorld(&world);

    // Set text, then pass a number — not a string, so it must clear the buffer.
    REQUIRE(fix.engine.doString("ffe.setHudText('before')"));
    REQUIRE(fix.engine.doString("ffe.setHudText(42)"));

    const auto* buf = world.registry().ctx().find<ffe::HudTextBuffer>();
    REQUIRE(buf != nullptr);
    CHECK(std::string(buf->text).empty());

    // Boolean argument — also not a string.
    REQUIRE(fix.engine.doString("ffe.setHudText('before2')"));
    REQUIRE(fix.engine.doString("ffe.setHudText(true)"));
    CHECK(std::string(buf->text).empty());

    // Table argument — also not a string.
    REQUIRE(fix.engine.doString("ffe.setHudText('before3')"));
    REQUIRE(fix.engine.doString("ffe.setHudText({})"));
    CHECK(std::string(buf->text).empty());
}

TEST_CASE("ffe.setHudText truncates text longer than HUD_TEXT_BUFFER_SIZE - 1", "[scripting][hud][security]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::HudTextBuffer>();
    fix.engine.setWorld(&world);

    // Build a Lua string longer than HUD_TEXT_BUFFER_SIZE (256).
    // string.rep('A', 300) produces 300 'A' characters.
    REQUIRE(fix.engine.doString("ffe.setHudText(string.rep('A', 300))"));

    const auto* buf = world.registry().ctx().find<ffe::HudTextBuffer>();
    REQUIRE(buf != nullptr);
    // Must be truncated to HUD_TEXT_BUFFER_SIZE - 1 = 255 characters.
    CHECK(std::strlen(buf->text) == ffe::HUD_TEXT_BUFFER_SIZE - 1);
    // Must be null-terminated.
    CHECK(buf->text[ffe::HUD_TEXT_BUFFER_SIZE - 1] == '\0');
    // All stored characters must be 'A'.
    for (ffe::u32 i = 0; i < ffe::HUD_TEXT_BUFFER_SIZE - 1; ++i) {
        CHECK(buf->text[i] == 'A');
    }
}

TEST_CASE("ffe.setHudText works when HudTextBuffer is emplaced in ECS context", "[scripting][hud]") {
    // Verifies the binding correctly finds the HudTextBuffer in the ECS context
    // when it has been emplaced (not just default-constructed by something else).
    ScriptFixture fix;
    ffe::World world;
    auto& hudBuf = world.registry().ctx().emplace<ffe::HudTextBuffer>();
    // Pre-fill to confirm overwrite works.
    std::strncpy(hudBuf.text, "old text", ffe::HUD_TEXT_BUFFER_SIZE - 1);
    hudBuf.text[ffe::HUD_TEXT_BUFFER_SIZE - 1] = '\0';
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.setHudText('new text')"));

    CHECK(std::string(hudBuf.text) == "new text");
}

TEST_CASE("ffe.setHudText is no-op when no World is set (does not crash)", "[scripting][hud]") {
    ScriptFixture fix;
    // No setWorld() call — World pointer is nullptr in Lua registry.
    // Must not crash; the binding logs an error and returns 0.
    REQUIRE(fix.engine.doString("ffe.setHudText('should not crash')"));
}

// =============================================================================
// Camera shake bindings
// =============================================================================

TEST_CASE("ffe.cameraShake sets CameraShake in ECS context", "[scripting][camera]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::CameraShake>();
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.cameraShake(10, 0.5)"));
    const auto& shake = world.registry().ctx().get<ffe::CameraShake>();
    CHECK(shake.intensity == Catch::Approx(10.0f));
    CHECK(shake.duration == Catch::Approx(0.5f));
    CHECK(shake.elapsed == Catch::Approx(0.0f));
}

TEST_CASE("ffe.cameraShake clamps values to valid range", "[scripting][camera]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::CameraShake>();
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.cameraShake(200, 10)"));
    const auto& shake = world.registry().ctx().get<ffe::CameraShake>();
    CHECK(shake.intensity == Catch::Approx(100.0f));
    CHECK(shake.duration == Catch::Approx(5.0f));
}

TEST_CASE("ffe.cameraShake without World is a no-op", "[scripting][camera]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.cameraShake(5, 0.3)"));
}

TEST_CASE("ffe.cameraShake rejects NaN silently", "[scripting][camera]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::CameraShake>();
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.cameraShake(0/0, 0.5)"));
    const auto& shake = world.registry().ctx().get<ffe::CameraShake>();
    CHECK(shake.intensity == Catch::Approx(0.0f)); // unchanged from default
}

// =============================================================================
// Background color bindings
// =============================================================================

TEST_CASE("ffe.setBackgroundColor sets ClearColor in ECS context", "[scripting][camera]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::ClearColor>();
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.setBackgroundColor(0.5, 0.6, 0.7)"));
    const auto& cc = world.registry().ctx().get<ffe::ClearColor>();
    CHECK(cc.r == Catch::Approx(0.5f));
    CHECK(cc.g == Catch::Approx(0.6f));
    CHECK(cc.b == Catch::Approx(0.7f));
}

TEST_CASE("ffe.setBackgroundColor clamps values to [0,1]", "[scripting][camera]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::ClearColor>();
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("ffe.setBackgroundColor(-1, 2, 0.5)"));
    const auto& cc = world.registry().ctx().get<ffe::ClearColor>();
    CHECK(cc.r == Catch::Approx(0.0f));
    CHECK(cc.g == Catch::Approx(1.0f));
    CHECK(cc.b == Catch::Approx(0.5f));
}

TEST_CASE("ffe.setBackgroundColor without World is a no-op", "[scripting][camera]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.setBackgroundColor(0.5, 0.5, 0.5)"));
}

// =============================================================================
// Mouse button bindings
// =============================================================================

TEST_CASE("ffe.isMousePressed returns boolean", "[scripting][input]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(type(ffe.isMousePressed(ffe.MOUSE_LEFT)) == 'boolean')"));
}

TEST_CASE("ffe.isMouseHeld returns boolean", "[scripting][input]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(type(ffe.isMouseHeld(ffe.MOUSE_LEFT)) == 'boolean')"));
}

TEST_CASE("ffe.isMouseReleased returns boolean", "[scripting][input]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(type(ffe.isMouseReleased(ffe.MOUSE_LEFT)) == 'boolean')"));
}

TEST_CASE("Mouse button constants are defined", "[scripting][input]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(ffe.MOUSE_LEFT ~= nil)"));
    REQUIRE(fix.engine.doString("assert(ffe.MOUSE_RIGHT ~= nil)"));
    REQUIRE(fix.engine.doString("assert(ffe.MOUSE_MIDDLE ~= nil)"));
}

TEST_CASE("Full alphabet key constants are defined", "[scripting][input]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "for _, k in ipairs({'A','B','C','D','E','F','G','H','I','J','K','L','M',"
        "'N','O','P','Q','R','S','T','U','V','W','X','Y','Z'}) do "
        "assert(ffe['KEY_' .. k] ~= nil, 'missing KEY_' .. k) end"));
}

TEST_CASE("Number key constants are defined", "[scripting][input]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "for i = 0, 9 do assert(ffe['KEY_' .. i] ~= nil, 'missing KEY_' .. i) end"));
}

TEST_CASE("Modifier and function key constants are defined", "[scripting][input]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(ffe.KEY_LEFT_SHIFT ~= nil)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_LEFT_CTRL ~= nil)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_TAB ~= nil)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_BACKSPACE ~= nil)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_F1 ~= nil)"));
    REQUIRE(fix.engine.doString("assert(ffe.KEY_F5 ~= nil)"));
}

// =============================================================================
// Entity count binding
// =============================================================================

TEST_CASE("ffe.getEntityCount returns 0 when no World is set", "[scripting][ecs]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(ffe.getEntityCount() == 0)"));
}

TEST_CASE("ffe.getEntityCount returns correct count after creating entities", "[scripting][ecs]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString("assert(ffe.getEntityCount() == 0)"));
    REQUIRE(fix.engine.doString("ffe.createEntity()"));
    REQUIRE(fix.engine.doString(
        "local c = ffe.getEntityCount(); "
        "if c ~= 1 then error('expected 1, got ' .. tostring(c)) end"));
    REQUIRE(fix.engine.doString("ffe.createEntity()"));
    REQUIRE(fix.engine.doString(
        "local c = ffe.getEntityCount(); "
        "if c ~= 2 then error('expected 2, got ' .. tostring(c)) end"));
}

// =============================================================================
// drawText binding
// =============================================================================

TEST_CASE("ffe.drawText is callable without World (no-op)", "[scripting][text]") {
    ScriptFixture fix;
    // Should not error even with no world
    REQUIRE(fix.engine.doString("ffe.drawText('hello', 10, 20)"));
}

TEST_CASE("ffe.drawText queues glyphs when TextRenderer is available", "[scripting][text]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Create a TextRenderer and register it in ECS context
    ffe::renderer::TextRenderer tr{};
    // Manually set up minimal state (no GPU — headless)
    tr.screenWidth  = 1280.0f;
    tr.screenHeight = 720.0f;
    tr.glyphCount   = 0;
    tr.glyphs       = static_cast<ffe::renderer::GlyphQuad*>(
        std::malloc(ffe::renderer::MAX_TEXT_GLYPHS * sizeof(ffe::renderer::GlyphQuad)));
    world.registry().ctx().emplace<ffe::renderer::TextRenderer*>(&tr);

    ffe::renderer::beginText(tr);
    REQUIRE(fix.engine.doString("ffe.drawText('Hi', 10, 20, 2, 1, 0, 0, 1)"));
    // "Hi" = 2 characters = 2 glyphs
    REQUIRE(tr.glyphCount == 2);

    std::free(tr.glyphs);
    tr.glyphs = nullptr;
}

TEST_CASE("ffe.drawText rejects NaN values (no-op)", "[scripting][text]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    ffe::renderer::TextRenderer tr{};
    tr.screenWidth  = 1280.0f;
    tr.screenHeight = 720.0f;
    tr.glyphCount   = 0;
    tr.glyphs       = static_cast<ffe::renderer::GlyphQuad*>(
        std::malloc(ffe::renderer::MAX_TEXT_GLYPHS * sizeof(ffe::renderer::GlyphQuad)));
    world.registry().ctx().emplace<ffe::renderer::TextRenderer*>(&tr);

    ffe::renderer::beginText(tr);
    REQUIRE(fix.engine.doString("ffe.drawText('test', 0/0, 20)"));
    REQUIRE(tr.glyphCount == 0); // NaN x rejected

    std::free(tr.glyphs);
    tr.glyphs = nullptr;
}

TEST_CASE("ffe.drawText clamps color values to [0,1]", "[scripting][text]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    ffe::renderer::TextRenderer tr{};
    tr.screenWidth  = 1280.0f;
    tr.screenHeight = 720.0f;
    tr.glyphCount   = 0;
    tr.glyphs       = static_cast<ffe::renderer::GlyphQuad*>(
        std::malloc(ffe::renderer::MAX_TEXT_GLYPHS * sizeof(ffe::renderer::GlyphQuad)));
    world.registry().ctx().emplace<ffe::renderer::TextRenderer*>(&tr);

    ffe::renderer::beginText(tr);
    // r=2.0 should be clamped to 1.0, g=-1.0 should be clamped to 0.0
    REQUIRE(fix.engine.doString("ffe.drawText('A', 0, 0, 1, 2.0, -1.0, 0.5, 1.0)"));
    REQUIRE(tr.glyphCount == 1);
    REQUIRE(tr.glyphs[0].r == Catch::Approx(1.0f));
    REQUIRE(tr.glyphs[0].g == Catch::Approx(0.0f));
    REQUIRE(tr.glyphs[0].b == Catch::Approx(0.5f));

    std::free(tr.glyphs);
    tr.glyphs = nullptr;
}

// =============================================================================
// Screen dimension queries
// =============================================================================

TEST_CASE("ffe.getScreenWidth returns 0 when no World is set", "[scripting][text]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(ffe.getScreenWidth() == 0)"));
}

TEST_CASE("ffe.getScreenWidth/Height return TextRenderer dimensions", "[scripting][text]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    ffe::renderer::TextRenderer tr{};
    tr.screenWidth  = 1280.0f;
    tr.screenHeight = 720.0f;
    tr.glyphCount   = 0;
    tr.glyphs       = nullptr;
    world.registry().ctx().emplace<ffe::renderer::TextRenderer*>(&tr);

    REQUIRE(fix.engine.doString("assert(ffe.getScreenWidth() == 1280)"));
    REQUIRE(fix.engine.doString("assert(ffe.getScreenHeight() == 720)"));
}

// =============================================================================
// drawRect binding
// =============================================================================

TEST_CASE("ffe.drawRect queues a glyph quad", "[scripting][text]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    ffe::renderer::TextRenderer tr{};
    tr.screenWidth  = 1280.0f;
    tr.screenHeight = 720.0f;
    tr.glyphCount   = 0;
    tr.glyphs       = static_cast<ffe::renderer::GlyphQuad*>(
        std::malloc(ffe::renderer::MAX_TEXT_GLYPHS * sizeof(ffe::renderer::GlyphQuad)));
    world.registry().ctx().emplace<ffe::renderer::TextRenderer*>(&tr);

    ffe::renderer::beginText(tr);
    REQUIRE(fix.engine.doString("ffe.drawRect(10, 20, 200, 50, 0, 0, 0, 0.7)"));
    REQUIRE(tr.glyphCount == 1);
    REQUIRE(tr.glyphs[0].x == Catch::Approx(10.0f));
    REQUIRE(tr.glyphs[0].width == Catch::Approx(200.0f));
    REQUIRE(tr.glyphs[0].a == Catch::Approx(0.7f));

    std::free(tr.glyphs);
    tr.glyphs = nullptr;
}

// =============================================================================
// ffe.setSpriteFlip
// =============================================================================

TEST_CASE("ffe.setSpriteFlip sets flipX and flipY on Sprite component", "[scripting][ecs][sprite]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    // Create entity with Transform + Sprite
    const std::string setup =
        "ffe.addTransform(" + std::to_string(entity) + ", 0, 0, 0, 1, 1)\n"
        "ffe.addSprite(" + std::to_string(entity) + ", 1, 32, 32, 1, 1, 1, 1, 0)\n";
    REQUIRE(fix.engine.doString(setup.c_str()));

    // Set flipX=true, flipY=false
    const std::string flipScript =
        "ffe.setSpriteFlip(" + std::to_string(entity) + ", true, false)\n";
    REQUIRE(fix.engine.doString(flipScript.c_str()));

    const ffe::Sprite& s = world.getComponent<ffe::Sprite>(entity);
    REQUIRE(s.flipX == true);
    REQUIRE(s.flipY == false);

    // Set flipY=true too
    const std::string flipBoth =
        "ffe.setSpriteFlip(" + std::to_string(entity) + ", true, true)\n";
    REQUIRE(fix.engine.doString(flipBoth.c_str()));

    const ffe::Sprite& s2 = world.getComponent<ffe::Sprite>(entity);
    REQUIRE(s2.flipX == true);
    REQUIRE(s2.flipY == true);

    // Unflip both
    const std::string unflip =
        "ffe.setSpriteFlip(" + std::to_string(entity) + ", false, false)\n";
    REQUIRE(fix.engine.doString(unflip.c_str()));

    const ffe::Sprite& s3 = world.getComponent<ffe::Sprite>(entity);
    REQUIRE(s3.flipX == false);
    REQUIRE(s3.flipY == false);
}

TEST_CASE("ffe.setSpriteFlip on invalid entity is a no-op", "[scripting][ecs][sprite]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Invalid entity ID — should not crash
    REQUIRE(fix.engine.doString("ffe.setSpriteFlip(999999, true, false)"));
}

// =============================================================================
// Tilemap API: ffe.addTilemap, ffe.setTile, ffe.getTile
// =============================================================================

TEST_CASE("ffe.addTilemap creates a tilemap component", "[scripting][tilemap]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "ffe.addTransform(" + std::to_string(entity) + ", 0, 0, 0, 1, 1)\n"
        "local ok = ffe.addTilemap(" + std::to_string(entity) + ", 8, 6, 16, 16, 1, 4, 16, 0)\n"
        "assert(ok == true)\n";
    REQUIRE(fix.engine.doString(script.c_str()));
    REQUIRE(world.hasComponent<ffe::Tilemap>(entity));

    const ffe::Tilemap& tm = world.getComponent<ffe::Tilemap>(entity);
    REQUIRE(tm.width == 8);
    REQUIRE(tm.height == 6);
    REQUIRE(tm.tileWidth == Catch::Approx(16.0f));
    REQUIRE(tm.tiles != nullptr);

    ffe::destroyTilemap(world.getComponent<ffe::Tilemap>(entity));
}

TEST_CASE("ffe.setTile and ffe.getTile round-trip", "[scripting][tilemap]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string setup =
        "ffe.addTransform(" + std::to_string(entity) + ", 0, 0, 0, 1, 1)\n"
        "ffe.addTilemap(" + std::to_string(entity) + ", 4, 4, 16, 16, 1, 4, 16, 0)\n"
        "ffe.setTile(" + std::to_string(entity) + ", 2, 3, 7)\n"
        "local v = ffe.getTile(" + std::to_string(entity) + ", 2, 3)\n"
        "assert(v == 7, 'expected 7, got ' .. tostring(v))\n";
    REQUIRE(fix.engine.doString(setup.c_str()));

    ffe::destroyTilemap(world.getComponent<ffe::Tilemap>(entity));
}

TEST_CASE("ffe.getTile returns 0 for unset tiles", "[scripting][tilemap]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "ffe.addTransform(" + std::to_string(entity) + ", 0, 0, 0, 1, 1)\n"
        "ffe.addTilemap(" + std::to_string(entity) + ", 4, 4, 16, 16, 1, 4, 16, 0)\n"
        "local v = ffe.getTile(" + std::to_string(entity) + ", 0, 0)\n"
        "assert(v == 0)\n";
    REQUIRE(fix.engine.doString(script.c_str()));

    ffe::destroyTilemap(world.getComponent<ffe::Tilemap>(entity));
}

TEST_CASE("ffe.setTile out of bounds is a no-op", "[scripting][tilemap]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "ffe.addTransform(" + std::to_string(entity) + ", 0, 0, 0, 1, 1)\n"
        "ffe.addTilemap(" + std::to_string(entity) + ", 4, 4, 16, 16, 1, 4, 16, 0)\n"
        "ffe.setTile(" + std::to_string(entity) + ", 10, 10, 5)\n"; // out of bounds — no crash
    REQUIRE(fix.engine.doString(script.c_str()));

    ffe::destroyTilemap(world.getComponent<ffe::Tilemap>(entity));
}

TEST_CASE("ffe.addTilemap with zero texture returns false", "[scripting][tilemap]") {
    ScriptFixture fix;
    ffe::World world;
    const ffe::EntityId entity = world.createEntity();
    fix.engine.setWorld(&world);

    const std::string script =
        "ffe.addTransform(" + std::to_string(entity) + ", 0, 0, 0, 1, 1)\n"
        "local ok = ffe.addTilemap(" + std::to_string(entity) + ", 4, 4, 16, 16, 0, 4, 16, 0)\n"
        "assert(ok == false)\n";
    REQUIRE(fix.engine.doString(script.c_str()));
}

// =============================================================================
// Timer API: ffe.after, ffe.every, ffe.cancelTimer
// =============================================================================

TEST_CASE("ffe.after fires callback once after elapsed time", "[scripting][timer]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "g_fired = 0\n"
        "ffe.after(0.1, function() g_fired = g_fired + 1 end)\n"
    ));

    // Tick 5 times at 1/60 = 0.0833s total — should not fire yet
    for (int i = 0; i < 5; ++i) fix.engine.tickTimers(1.0f / 60.0f);
    REQUIRE(fix.engine.doString("assert(g_fired == 0)"));

    // Tick 2 more = 7 ticks = 0.1167s — should fire once
    for (int i = 0; i < 2; ++i) fix.engine.tickTimers(1.0f / 60.0f);
    REQUIRE(fix.engine.doString("assert(g_fired == 1)"));

    // Tick 10 more — should NOT fire again (one-shot)
    for (int i = 0; i < 10; ++i) fix.engine.tickTimers(1.0f / 60.0f);
    REQUIRE(fix.engine.doString("assert(g_fired == 1)"));
}

TEST_CASE("ffe.every fires callback repeatedly", "[scripting][timer]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "g_count = 0\n"
        "ffe.every(0.05, function() g_count = g_count + 1 end)\n"
    ));

    // Tick 6 times at 1/60 = 0.1s total — should fire twice (0.05 and 0.1)
    for (int i = 0; i < 6; ++i) fix.engine.tickTimers(1.0f / 60.0f);
    REQUIRE(fix.engine.doString("assert(g_count == 2, 'expected 2, got ' .. g_count)"));
}

TEST_CASE("ffe.cancelTimer stops a repeating timer", "[scripting][timer]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "g_val = 0\n"
        "g_tid = ffe.every(0.05, function() g_val = g_val + 1 end)\n"
    ));

    // Tick to fire once
    for (int i = 0; i < 4; ++i) fix.engine.tickTimers(1.0f / 60.0f);
    REQUIRE(fix.engine.doString("assert(g_val == 1)"));

    // Cancel and tick more — should not fire again
    REQUIRE(fix.engine.doString("ffe.cancelTimer(g_tid)"));
    for (int i = 0; i < 10; ++i) fix.engine.tickTimers(1.0f / 60.0f);
    REQUIRE(fix.engine.doString("assert(g_val == 1)"));
}

TEST_CASE("ffe.after returns timer ID as integer", "[scripting][timer]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "local id = ffe.after(1.0, function() end)\n"
        "assert(type(id) == 'number')\n"
        "assert(id >= 0)\n"
    ));
}

TEST_CASE("ffe.after with zero delay fires on next tick", "[scripting][timer]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "g_zero = false\n"
        "ffe.after(0, function() g_zero = true end)\n"
    ));

    fix.engine.tickTimers(1.0f / 60.0f);
    REQUIRE(fix.engine.doString("assert(g_zero == true)"));
}

TEST_CASE("ffe.cancelTimer with invalid ID is a no-op", "[scripting][timer]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Should not crash
    REQUIRE(fix.engine.doString("ffe.cancelTimer(-1)"));
    REQUIRE(fix.engine.doString("ffe.cancelTimer(999)"));
}

TEST_CASE("ffe.after with negative delay returns nil", "[scripting][timer]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "local id = ffe.after(-1, function() end)\n"
        "assert(id == nil)\n"
    ));
}

TEST_CASE("Timer callback error cancels the timer", "[scripting][timer]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "g_err_count = 0\n"
        "ffe.every(0.01, function() g_err_count = g_err_count + 1; error('boom') end)\n"
    ));

    // First tick fires and errors — timer should be cancelled
    fix.engine.tickTimers(0.02f);
    REQUIRE(fix.engine.doString("assert(g_err_count == 1)"));

    // More ticks — should NOT fire again (cancelled on error)
    fix.engine.tickTimers(0.02f);
    fix.engine.tickTimers(0.02f);
    REQUIRE(fix.engine.doString("assert(g_err_count == 1)"));
}

// =============================================================================
// Scene management: ffe.destroyAllEntities, ffe.cancelAllTimers
// =============================================================================

TEST_CASE("ffe.destroyAllEntities clears all entities", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    // Create several entities
    REQUIRE(fix.engine.doString(
        "g_e1 = ffe.createEntity()\n"
        "g_e2 = ffe.createEntity()\n"
        "g_e3 = ffe.createEntity()\n"
        "assert(ffe.getEntityCount() == 3)\n"
    ));

    REQUIRE(fix.engine.doString("ffe.destroyAllEntities()"));
    REQUIRE(fix.engine.doString("assert(ffe.getEntityCount() == 0)"));
}

TEST_CASE("ffe.destroyAllEntities cancels all timers", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "g_count = 0\n"
        "ffe.every(0.01, function() g_count = g_count + 1 end)\n"
        "ffe.every(0.01, function() g_count = g_count + 1 end)\n"
    ));

    // Tick once — both timers fire
    fix.engine.tickTimers(0.02f);
    REQUIRE(fix.engine.doString("assert(g_count == 2)"));

    // Destroy all — timers should be cancelled
    REQUIRE(fix.engine.doString("ffe.destroyAllEntities()"));

    // Tick again — count should not increase
    fix.engine.tickTimers(0.02f);
    REQUIRE(fix.engine.doString("assert(g_count == 2)"));
}

TEST_CASE("ffe.destroyAllEntities with no world is a no-op", "[scripting][scene]") {
    ScriptFixture fix;
    // No world set — should not crash
    REQUIRE(fix.engine.doString("ffe.destroyAllEntities()"));
}

TEST_CASE("ffe.cancelAllTimers cancels all active timers", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "g_val = 0\n"
        "ffe.every(0.01, function() g_val = g_val + 1 end)\n"
        "ffe.after(0.01, function() g_val = g_val + 10 end)\n"
    ));

    fix.engine.tickTimers(0.02f);
    REQUIRE(fix.engine.doString("assert(g_val == 11)"));

    REQUIRE(fix.engine.doString("ffe.cancelAllTimers()"));

    fix.engine.tickTimers(0.02f);
    REQUIRE(fix.engine.doString("assert(g_val == 11)"));
}

TEST_CASE("ffe.destroyAllEntities allows new entities after clear", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "ffe.createEntity()\n"
        "ffe.createEntity()\n"
    ));
    REQUIRE(fix.engine.doString("assert(ffe.getEntityCount() == 2)"));

    REQUIRE(fix.engine.doString("ffe.destroyAllEntities()"));
    REQUIRE(fix.engine.doString("assert(ffe.getEntityCount() == 0)"));

    // Create new entities after clearing
    REQUIRE(fix.engine.doString(
        "local e = ffe.createEntity()\n"
        "assert(e ~= nil)\n"
        "assert(ffe.getEntityCount() == 1)\n"
    ));
}

TEST_CASE("ffe.destroyAllEntities clears collision callback", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::CollisionCallbackRef>();
    fix.engine.setWorld(&world);

    // Register a collision callback
    REQUIRE(fix.engine.doString(
        "g_cb_called = false\n"
        "ffe.setCollisionCallback(function(a, b) g_cb_called = true end)\n"
    ));

    // Verify the callback ref is set in ECS context
    REQUIRE(world.registry().ctx().get<ffe::CollisionCallbackRef>().luaRef != -2);

    // Destroy all — collision callback should be cleared
    REQUIRE(fix.engine.doString("ffe.destroyAllEntities()"));

    // The collision callback ref in ECS context should be LUA_NOREF (-2)
    REQUIRE(world.registry().ctx().get<ffe::CollisionCallbackRef>().luaRef == -2);
}

TEST_CASE("ffe.destroyAllEntities twice in a row is safe", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    world.registry().ctx().emplace<ffe::CollisionCallbackRef>();
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "ffe.createEntity()\n"
        "ffe.createEntity()\n"
    ));

    REQUIRE(fix.engine.doString("ffe.destroyAllEntities()"));
    REQUIRE(fix.engine.doString("ffe.destroyAllEntities()"));
    REQUIRE(fix.engine.doString("assert(ffe.getEntityCount() == 0)"));
}

TEST_CASE("ffe.cancelAllTimers does not affect entities", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);

    REQUIRE(fix.engine.doString(
        "ffe.createEntity()\n"
        "ffe.createEntity()\n"
        "ffe.every(0.01, function() end)\n"
    ));

    REQUIRE(fix.engine.doString("ffe.cancelAllTimers()"));
    REQUIRE(fix.engine.doString("assert(ffe.getEntityCount() == 2)"));
}

TEST_CASE("ffe.loadScene rejects path traversal", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    fix.engine.setScriptRoot("/tmp");

    // These should all fail without crashing
    REQUIRE(fix.engine.doString("ffe.loadScene('../../../etc/passwd')"));
    REQUIRE(fix.engine.doString("ffe.loadScene('/absolute/path.lua')"));
    REQUIRE(fix.engine.doString("ffe.loadScene('')"));
}

TEST_CASE("ffe.loadScene with non-string argument is a no-op", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    fix.engine.setScriptRoot("/tmp");

    REQUIRE(fix.engine.doString("ffe.loadScene(42)"));
    REQUIRE(fix.engine.doString("ffe.loadScene(nil)"));
    REQUIRE(fix.engine.doString("ffe.loadScene({})"));
}

TEST_CASE("ffe.loadScene without script root is a no-op", "[scripting][scene]") {
    ScriptFixture fix;
    ffe::World world;
    fix.engine.setWorld(&world);
    // No setScriptRoot call — should log error and not crash
    REQUIRE(fix.engine.doString("ffe.loadScene('test.lua')"));
}

TEST_CASE("setScriptRoot is write-once", "[scripting][scene]") {
    ScriptFixture fix;
    REQUIRE(fix.engine.setScriptRoot("/tmp/first"));
    // Second call should fail (write-once)
    REQUIRE_FALSE(fix.engine.setScriptRoot("/tmp/second"));
    REQUIRE(std::string(fix.engine.scriptRoot()) == "/tmp/first");
}

TEST_CASE("setScriptRoot rejects null and empty", "[scripting][scene]") {
    ScriptFixture fix;
    REQUIRE_FALSE(fix.engine.setScriptRoot(nullptr));
    REQUIRE_FALSE(fix.engine.setScriptRoot(""));
    REQUIRE(fix.engine.scriptRoot() == nullptr);
}
