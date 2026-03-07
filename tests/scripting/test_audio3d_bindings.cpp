// test_audio3d_bindings.cpp -- Catch2 unit tests for the 3D positional audio
// Lua bindings:
//   ffe.playSound3D, ffe.setListenerPosition,
//   ffe.setSound3DMinDistance, ffe.setSound3DMaxDistance
//
// All tests run in headless mode. Audio init/shutdown is managed per fixture.
// playSound3D is expected to be a no-op in headless mode (no device), but the
// binding itself must resolve and not error out. setListenerPosition and
// distance config work regardless of device state.

#include <catch2/catch_test_macros.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "audio/audio.h"

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World + audio init (headless)
// ---------------------------------------------------------------------------

struct Audio3DBindingFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    Audio3DBindingFixture() {
        REQUIRE(ffe::audio::init(true));
        REQUIRE(engine.init());
        engine.setWorld(&world);
    }
    ~Audio3DBindingFixture() {
        engine.shutdown();
        ffe::audio::shutdown();
    }
};

// =============================================================================
// ffe.playSound3D binding exists and does not crash
// =============================================================================

TEST_CASE("playSound3D binding exists and is callable",
          "[scripting][audio3d]") {
    Audio3DBindingFixture fix;
    // playSound3D with a non-existent file should not crash (will fail to load).
    // The binding should exist and not throw a Lua error.
    REQUIRE(fix.engine.doString(
        "ffe.playSound3D('nonexistent.wav', 1.0, 2.0, 3.0)\n"
    ));
}

TEST_CASE("playSound3D binding accepts optional volume",
          "[scripting][audio3d]") {
    Audio3DBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "ffe.playSound3D('nonexistent.wav', 0, 0, 0, 0.5)\n"
    ));
}

// =============================================================================
// ffe.setListenerPosition binding
// =============================================================================

TEST_CASE("setListenerPosition binding exists and accepts 9 floats",
          "[scripting][audio3d]") {
    Audio3DBindingFixture fix;
    REQUIRE(fix.engine.doString(
        "ffe.setListenerPosition(1, 2, 3, 0, 0, -1, 0, 1, 0)\n"
    ));
}

TEST_CASE("setListenerPosition handles NaN gracefully",
          "[scripting][audio3d]") {
    Audio3DBindingFixture fix;
    // math.huge / math.huge = NaN in Lua
    REQUIRE(fix.engine.doString(
        "local nan = 0/0\n"
        "ffe.setListenerPosition(nan, 0, 0, 0, 0, -1, 0, 1, 0)\n"
    ));
}

// =============================================================================
// ffe.setSound3DMinDistance / setSound3DMaxDistance bindings
// =============================================================================

TEST_CASE("setSound3DMinDistance binding exists and sets value",
          "[scripting][audio3d]") {
    Audio3DBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setSound3DMinDistance(2.0)"));
    CHECK(ffe::audio::getSound3DMinDistance() > 1.5f);
}

TEST_CASE("setSound3DMaxDistance binding exists and sets value",
          "[scripting][audio3d]") {
    Audio3DBindingFixture fix;
    REQUIRE(fix.engine.doString("ffe.setSound3DMaxDistance(50.0)"));
    CHECK(ffe::audio::getSound3DMaxDistance() < 60.0f);
}

TEST_CASE("setSound3DMinDistance rejects NaN from Lua",
          "[scripting][audio3d]") {
    Audio3DBindingFixture fix;
    const float before = ffe::audio::getSound3DMinDistance();
    REQUIRE(fix.engine.doString("ffe.setSound3DMinDistance(0/0)"));
    // Value should not change because NaN is rejected
    CHECK(ffe::audio::getSound3DMinDistance() == before);
}

TEST_CASE("setSound3DMaxDistance rejects Inf from Lua",
          "[scripting][audio3d]") {
    Audio3DBindingFixture fix;
    const float before = ffe::audio::getSound3DMaxDistance();
    REQUIRE(fix.engine.doString("ffe.setSound3DMaxDistance(math.huge)"));
    // Value should not change because Inf is rejected at the Lua binding level
    CHECK(ffe::audio::getSound3DMaxDistance() == before);
}
