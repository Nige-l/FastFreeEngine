// test_networking_bindings.cpp — Catch2 unit tests for the networking Lua bindings.
//
// Tests run in the ffe_tests_scripting executable which links ffe_scripting
// (and transitively ffe_networking with ENet).

#include <catch2/catch_test_macros.hpp>

#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "networking/network_system.h"

// ---------------------------------------------------------------------------
// Fixture: ScriptEngine + World + networking initialized
// ---------------------------------------------------------------------------
struct NetworkingScriptFixture {
    ffe::ScriptEngine engine;
    ffe::World        world;

    NetworkingScriptFixture() {
        REQUIRE(engine.init());
        ffe::networking::initNetworkSystem();
        engine.setWorld(&world);
    }
    ~NetworkingScriptFixture() {
        engine.shutdown();
        ffe::networking::shutdownNetworkSystem();
    }
};

// =============================================================================
// ffe.isServer() — returns false before starting
// =============================================================================

TEST_CASE("ffe.isServer returns false before starting",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(ffe.isServer() == false, 'should not be server')"));
}

// =============================================================================
// ffe.isConnected() — returns false before connecting
// =============================================================================

TEST_CASE("ffe.isConnected returns false before connecting",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("assert(ffe.isConnected() == false, 'should not be connected')"));
}

// =============================================================================
// ffe.startServer(port) and ffe.stopServer()
// =============================================================================

TEST_CASE("ffe.startServer and ffe.stopServer work",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;

    // Start a server on an ephemeral port
    REQUIRE(fix.engine.doString(
        "local ok = ffe.startServer(17771)\n"
        "assert(ok, 'startServer should succeed')\n"
        "assert(ffe.isServer(), 'should be server after start')"));

    // Stop the server
    REQUIRE(fix.engine.doString(
        "ffe.stopServer()\n"
        "assert(ffe.isServer() == false, 'should not be server after stop')"));
}

// =============================================================================
// ffe.startServer with invalid port
// =============================================================================

TEST_CASE("ffe.startServer rejects invalid port",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local ok = ffe.startServer(0)\n"
        "assert(ok == false, 'port 0 should fail')"));
}

// =============================================================================
// ffe.setNetworkTickRate(hz)
// =============================================================================

TEST_CASE("ffe.setNetworkTickRate does not error",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;

    // Start server first so tick rate setting takes effect
    REQUIRE(fix.engine.doString("ffe.startServer(17772)"));
    REQUIRE(fix.engine.doString("ffe.setNetworkTickRate(30)"));
    REQUIRE(fix.engine.doString("ffe.stopServer()"));
}

// =============================================================================
// Callback registration functions exist and accept nil
// =============================================================================

TEST_CASE("ffe.onNetworkMessage accepts nil without crashing",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.onNetworkMessage(nil)"));
}

TEST_CASE("ffe.onClientConnected accepts nil without crashing",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.onClientConnected(nil)"));
}

TEST_CASE("ffe.onClientDisconnected accepts nil without crashing",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.onClientDisconnected(nil)"));
}

TEST_CASE("ffe.onConnected accepts nil without crashing",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.onConnected(nil)"));
}

TEST_CASE("ffe.onDisconnected accepts nil without crashing",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.onDisconnected(nil)"));
}

// =============================================================================
// Callback registration with actual functions
// =============================================================================

TEST_CASE("ffe.onNetworkMessage accepts a function",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "ffe.onNetworkMessage(function(senderId, msgType, data) end)"));
}

TEST_CASE("ffe.onClientConnected accepts a function",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "ffe.onClientConnected(function(clientId) end)"));
}

// =============================================================================
// ffe.sendMessage does not crash when not connected
// =============================================================================

TEST_CASE("ffe.sendMessage returns false when not connected",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local ok = ffe.sendMessage(1, 'hello')\n"
        "assert(ok == false, 'sendMessage should fail when not connected')"));
}

TEST_CASE("ffe.sendMessage returns false with empty data when not connected",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local ok = ffe.sendMessage(0, '')\n"
        "assert(ok == false, 'sendMessage should fail when not connected')"));
}

// =============================================================================
// ffe.getClientId returns -1 when not connected
// =============================================================================

TEST_CASE("ffe.getClientId returns -1 when not connected",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local id = ffe.getClientId()\n"
        "assert(id == -1, 'getClientId should return -1 when not connected, got ' .. tostring(id))"));
}

// =============================================================================
// ffe.disconnect does not crash when not connected
// =============================================================================

TEST_CASE("ffe.disconnect does not crash when not connected",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.disconnect()"));
}

// =============================================================================
// ffe.startServer followed by startServer returns false (already active)
// =============================================================================

TEST_CASE("double startServer returns false",
          "[scripting][networking]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "assert(ffe.startServer(17773), 'first start should succeed')\n"
        "local ok = ffe.startServer(17774)\n"
        "assert(ok == false, 'second start should fail')\n"
        "ffe.stopServer()"));
}
