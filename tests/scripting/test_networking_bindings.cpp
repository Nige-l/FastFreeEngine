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

// =============================================================================
// Prediction bindings
// =============================================================================

TEST_CASE("ffe.setLocalPlayer exists and callable",
          "[scripting][networking][prediction]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.setLocalPlayer(1)"));
}

TEST_CASE("ffe.sendInput does not crash when not connected",
          "[scripting][networking][prediction]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local ok = ffe.sendInput({bits=1, aimX=0.5, aimY=-0.5})\n"
        "assert(ok == false, 'sendInput should fail when not connected')"));
}

TEST_CASE("ffe.getPredictionError returns 0 initially",
          "[scripting][networking][prediction]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local err = ffe.getPredictionError()\n"
        "assert(err == 0, 'prediction error should be 0 initially, got ' .. tostring(err))"));
}

TEST_CASE("ffe.getNetworkTick returns a number",
          "[scripting][networking][prediction]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local tick = ffe.getNetworkTick()\n"
        "assert(type(tick) == 'number', 'getNetworkTick should return number, got ' .. type(tick))"));
}

TEST_CASE("ffe.onServerInput callable with function",
          "[scripting][networking][prediction]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "ffe.onServerInput(function(clientId, inputTable) end)"));
}

// =============================================================================
// Lag compensation bindings
// =============================================================================

TEST_CASE("ffe.performHitCheck exists and returns nil when not server",
          "[scripting][networking][lag_compensation]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local result = ffe.performHitCheck(0, 0, 0, 1, 0, 0, 100, 0)\n"
        "assert(result == nil, 'should return nil when not server/client')"));
}

TEST_CASE("ffe.setLagCompensationWindow callable",
          "[scripting][networking][lag_compensation]") {
    NetworkingScriptFixture fix;
    // Should not error even when not a server
    REQUIRE(fix.engine.doString("ffe.setLagCompensationWindow(32)"));
}

TEST_CASE("ffe.onHitConfirm callable with function",
          "[scripting][networking][lag_compensation]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "ffe.onHitConfirm(function(shooterId, hitResult) end)"));
}

// =============================================================================
// Lobby / Matchmaking bindings
// =============================================================================

TEST_CASE("ffe.createLobby exists and callable",
          "[scripting][networking][lobby]") {
    NetworkingScriptFixture fix;
    // Not a server, so should return false
    REQUIRE(fix.engine.doString(
        "local ok = ffe.createLobby('TestLobby', 4)\n"
        "assert(ok == false, 'createLobby should fail when not a server')"));
}

TEST_CASE("ffe.isInLobby returns false initially",
          "[scripting][networking][lobby]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "assert(ffe.isInLobby() == false, 'should not be in lobby initially')"));
}

TEST_CASE("ffe.getLobbyPlayers returns empty table initially",
          "[scripting][networking][lobby]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "local players = ffe.getLobbyPlayers()\n"
        "assert(type(players) == 'table', 'should return table')\n"
        "assert(#players == 0, 'should be empty, got ' .. #players)"));
}

TEST_CASE("ffe.setReady does not crash when not in lobby",
          "[scripting][networking][lobby]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString("ffe.setReady(true)"));
    REQUIRE(fix.engine.doString("ffe.setReady(false)"));
}

TEST_CASE("ffe.onLobbyUpdate callable with function",
          "[scripting][networking][lobby]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "ffe.onLobbyUpdate(function(state) end)"));
}

TEST_CASE("ffe.onGameStart callable with function",
          "[scripting][networking][lobby]") {
    NetworkingScriptFixture fix;
    REQUIRE(fix.engine.doString(
        "ffe.onGameStart(function() end)"));
}
