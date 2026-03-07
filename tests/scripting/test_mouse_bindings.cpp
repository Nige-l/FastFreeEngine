// test_mouse_bindings.cpp -- Catch2 unit tests for mouse delta and cursor
// capture Lua bindings:
//   ffe.getMouseDeltaX()       -> number
//   ffe.getMouseDeltaY()       -> number
//   ffe.setCursorCaptured(bool) -> void
//
// All tests run in headless mode (no GLFW window).  Delta values are 0 and
// setCursorCaptured is a no-op, but the bindings must exist and not crash.

#include <catch2/catch_test_macros.hpp>

#include "scripting/script_engine.h"
#include "core/input.h"

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + headless input initialised.
// ---------------------------------------------------------------------------

struct MouseBindingFixture {
    ffe::ScriptEngine engine;

    MouseBindingFixture() {
        ffe::initInput(nullptr); // headless -- no GLFW window
        REQUIRE(engine.init());
    }
    ~MouseBindingFixture() {
        engine.shutdown();
        ffe::shutdownInput();
    }
};

// =============================================================================
// ffe.getMouseDeltaX
// =============================================================================

TEST_CASE("getMouseDeltaX binding exists and returns a number",
          "[scripting][mouse]") {
    MouseBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "local dx = ffe.getMouseDeltaX()\n"
        "assert(type(dx) == 'number', 'expected number, got ' .. type(dx))\n"
    ));
}

TEST_CASE("getMouseDeltaX returns 0 in headless mode",
          "[scripting][mouse]") {
    MouseBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "local dx = ffe.getMouseDeltaX()\n"
        "assert(dx == 0, 'expected 0, got ' .. tostring(dx))\n"
    ));
}

// =============================================================================
// ffe.getMouseDeltaY
// =============================================================================

TEST_CASE("getMouseDeltaY binding exists and returns a number",
          "[scripting][mouse]") {
    MouseBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "local dy = ffe.getMouseDeltaY()\n"
        "assert(type(dy) == 'number', 'expected number, got ' .. type(dy))\n"
    ));
}

TEST_CASE("getMouseDeltaY returns 0 in headless mode",
          "[scripting][mouse]") {
    MouseBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "local dy = ffe.getMouseDeltaY()\n"
        "assert(dy == 0, 'expected 0, got ' .. tostring(dy))\n"
    ));
}

// =============================================================================
// ffe.setCursorCaptured -- must not crash in headless mode
// =============================================================================

TEST_CASE("setCursorCaptured(true) does not crash in headless mode",
          "[scripting][mouse]") {
    MouseBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setCursorCaptured(true)"));
}

TEST_CASE("setCursorCaptured(false) does not crash in headless mode",
          "[scripting][mouse]") {
    MouseBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setCursorCaptured(false)"));
}
