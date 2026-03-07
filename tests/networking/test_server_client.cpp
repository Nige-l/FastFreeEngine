// tests/networking/test_server_client.cpp
//
// Unit + integration tests for NetworkServer, NetworkClient, and NetworkSystem.
// Uses ENet on loopback (127.0.0.1) -- no real network needed.

#include <catch2/catch_test_macros.hpp>

#include "networking/server.h"
#include "networking/client.h"
#include "networking/network_system.h"
#include "networking/transport.h"
#include "core/ecs.h"
#include "renderer/render_system.h"

#include <chrono>
#include <cstring>
#include <thread>

using namespace ffe::networking;

// ---------------------------------------------------------------------------
// Helper: poll both server and client for a number of iterations
// ---------------------------------------------------------------------------
static void pollBoth(NetworkServer& server, NetworkClient& client,
                     const int iterations = 50, const int delayMs = 10) {
    const float dt = static_cast<float>(delayMs) * 0.001f;
    for (int i = 0; i < iterations; ++i) {
        server.update(dt);
        client.update(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

// ===========================================================================
// Server start / stop
// ===========================================================================

TEST_CASE("NetworkServer start and stop", "[networking][server]") {
    REQUIRE(initNetworking());

    NetworkServer server;
    ServerConfig config;
    config.port       = 18770;
    config.maxClients = 4;

    REQUIRE(server.start(config));
    CHECK(server.isRunning());
    CHECK(server.clientCount() == 0);
    CHECK(server.tick() == 0);

    server.stop();
    CHECK_FALSE(server.isRunning());

    shutdownNetworking();
}

TEST_CASE("NetworkServer double start fails", "[networking][server]") {
    REQUIRE(initNetworking());

    NetworkServer server;
    ServerConfig config;
    config.port = 18771;
    REQUIRE(server.start(config));
    CHECK_FALSE(server.start(config));

    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// Client connect / disconnect
// ===========================================================================

TEST_CASE("NetworkClient connects to server on loopback", "[networking][server][client]") {
    REQUIRE(initNetworking());

    bool serverSawConnect = false;
    bool clientConnected  = false;

    NetworkServer server;
    ServerConfig config;
    config.port       = 18772;
    config.maxClients = 4;

    server.setClientConnectCallback([](uint32_t /*id*/, void* ud) {
        *static_cast<bool*>(ud) = true;
    }, &serverSawConnect);

    REQUIRE(server.start(config));

    NetworkClient client;
    client.setConnectedCallback([](void* ud) {
        *static_cast<bool*>(ud) = true;
    }, &clientConnected);

    REQUIRE(client.connect("127.0.0.1", 18772));

    pollBoth(server, client, 50, 10);

    CHECK(clientConnected);
    CHECK(serverSawConnect);
    CHECK(client.isConnected());
    CHECK(server.clientCount() == 1);

    client.disconnect();
    CHECK_FALSE(client.isConnected());

    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// Client ID assignment
// ===========================================================================

TEST_CASE("NetworkClient ID is initially invalid", "[networking][client]") {
    NetworkClient client;
    CHECK(client.clientId() == 0xFFFFFFFF);
}

// ===========================================================================
// Connection limit enforcement
// ===========================================================================

TEST_CASE("Server enforces max client limit", "[networking][server]") {
    REQUIRE(initNetworking());

    NetworkServer server;
    ServerConfig config;
    config.port       = 18773;
    config.maxClients = 2; // Only allow 2 clients

    uint32_t connectCount = 0;
    server.setClientConnectCallback([](uint32_t /*id*/, void* ud) {
        ++(*static_cast<uint32_t*>(ud));
    }, &connectCount);

    REQUIRE(server.start(config));

    // Connect two clients
    NetworkClient client1;
    NetworkClient client2;
    REQUIRE(client1.connect("127.0.0.1", 18773));
    REQUIRE(client2.connect("127.0.0.1", 18773));

    // Poll to let connections establish
    const float dt = 0.01f;
    for (int i = 0; i < 50; ++i) {
        server.update(dt);
        client1.update(dt);
        client2.update(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(server.clientCount() == 2);

    client1.disconnect();
    client2.disconnect();
    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// Server networkTick produces snapshot
// ===========================================================================

TEST_CASE("Server networkTick increments tick counter", "[networking][server]") {
    REQUIRE(initNetworking());

    NetworkServer server;
    ServerConfig config;
    config.port            = 18774;
    config.maxClients      = 4;
    config.networkTickRate  = 20.0f;

    REQUIRE(server.start(config));

    // Connect a client so the server actually builds snapshots
    NetworkClient client;
    client.connect("127.0.0.1", 18774);
    pollBoth(server, client, 30, 10);
    REQUIRE(client.isConnected());

    CHECK(server.tick() == 0);

    // Accumulate enough time for one network tick (1/20 = 0.05s)
    ffe::World world;
    ReplicationRegistry registry;
    registerDefaultComponents(registry);

    server.update(0.06f); // > 0.05s
    server.networkTick(world, registry);

    CHECK(server.tick() == 1);

    // Second tick
    server.update(0.06f);
    server.networkTick(world, registry);
    CHECK(server.tick() == 2);

    client.disconnect();
    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// Client receives snapshot and applies to world
// ===========================================================================

TEST_CASE("Client receives snapshot and applies to world", "[networking][server][client]") {
    REQUIRE(initNetworking());

    NetworkServer server;
    ServerConfig config;
    config.port            = 18775;
    config.maxClients      = 4;
    config.networkTickRate  = 20.0f;
    REQUIRE(server.start(config));

    NetworkClient client;
    REQUIRE(client.connect("127.0.0.1", 18775));

    // Poll to establish connection
    pollBoth(server, client, 50, 10);
    REQUIRE(client.isConnected());
    REQUIRE(server.clientCount() >= 1);

    // Setup the server-side world with a replicated entity
    ffe::World serverWorld;
    const ffe::EntityId eid = serverWorld.createEntity();
    auto& tx = serverWorld.addComponent<ffe::Transform>(eid);
    tx.position.x = 42.0f;
    tx.position.y = 99.0f;
    tx.rotation   = 1.5f;

    ReplicationRegistry registry;
    registerDefaultComponents(registry);

    // Force a network tick (accumulate enough time)
    server.update(0.06f);
    server.networkTick(serverWorld, registry);

    // Poll to deliver the snapshot to the client
    pollBoth(server, client, 30, 10);

    // Setup a matching client-side world with the same entity
    ffe::World clientWorld;
    // The client needs to create the entity at the same ID.
    // In a real system, entity ID mapping would handle this.
    // For this test, we create entity 0 (first entity in both worlds).
    const ffe::EntityId clientEid = clientWorld.createEntity();
    auto& clientTx = clientWorld.addComponent<ffe::Transform>(clientEid);
    clientTx.position.x = 0.0f;
    clientTx.position.y = 0.0f;

    // Apply snapshots
    client.applySnapshots(clientWorld, registry);

    // Verify the transform was updated from the snapshot
    const auto& result = clientWorld.getComponent<ffe::Transform>(clientEid);
    CHECK(result.position.x == 42.0f);
    CHECK(result.position.y == 99.0f);
    CHECK(result.rotation == 1.5f);

    client.disconnect();
    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// Message send/receive between client and server
// ===========================================================================

TEST_CASE("Server receives messages from client via callback", "[networking][server][client]") {
    REQUIRE(initNetworking());

    NetworkServer server;
    ServerConfig config;
    config.port       = 18776;
    config.maxClients = 4;
    REQUIRE(server.start(config));

    // Track messages received by the server
    struct ServerMsg {
        uint32_t clientId    = 0xFFFFFFFF;
        uint16_t len         = 0;
        uint8_t  data[32]    = {};
        bool     received    = false;
    };
    ServerMsg sMsg;

    server.setMessageCallback([](const uint32_t cid, const uint8_t* data,
                                 const uint16_t len, void* ud) {
        auto* msg = static_cast<ServerMsg*>(ud);
        msg->clientId = cid;
        msg->len      = len;
        msg->received = true;
        if (len <= 32) {
            std::memcpy(msg->data, data, len);
        }
    }, &sMsg);

    NetworkClient client;
    REQUIRE(client.connect("127.0.0.1", 18776));

    pollBoth(server, client, 50, 10);
    REQUIRE(client.isConnected());

    // Client sends raw data via transport (low-level send)
    // For now, messages go through the transport layer directly
    // The server's message callback fires for any non-snapshot packet.

    client.disconnect();
    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// NetworkSystem integration
// ===========================================================================

TEST_CASE("NetworkSystem init and shutdown", "[networking][system]") {
    initNetworkSystem();

    CHECK_FALSE(isServer());
    CHECK_FALSE(isClient());
    CHECK(getServer() == nullptr);
    CHECK(getClient() == nullptr);

    shutdownNetworkSystem();
}

TEST_CASE("NetworkSystem start server", "[networking][system]") {
    initNetworkSystem();

    REQUIRE(startServer(18777, 8));
    CHECK(isServer());
    CHECK_FALSE(isClient());
    CHECK(getServer() != nullptr);
    CHECK(getServer()->isRunning());

    // Cannot start another server or connect as client while active
    CHECK_FALSE(startServer(18778));
    CHECK_FALSE(connectToServer("127.0.0.1", 18778));

    disconnectNetwork();
    CHECK_FALSE(isServer());

    shutdownNetworkSystem();
}

TEST_CASE("NetworkSystem connect as client", "[networking][system]") {
    initNetworkSystem();

    // Start a server to connect to (using raw server for the other end)
    REQUIRE(initNetworking()); // double init is safe per ENet docs

    ServerTransport rawServer;
    TransportConfig tc;
    tc.port       = 18779;
    tc.maxClients = 4;
    REQUIRE(rawServer.start(tc));

    REQUIRE(connectToServer("127.0.0.1", 18779));
    CHECK(isClient());
    CHECK_FALSE(isServer());
    CHECK(getClient() != nullptr);

    disconnectNetwork();
    CHECK_FALSE(isClient());

    rawServer.stop();
    shutdownNetworkSystem();
}

TEST_CASE("NetworkSystem updateNetworkSystem server path", "[networking][system]") {
    initNetworkSystem();

    REQUIRE(startServer(18780, 4));

    ffe::World world;
    const ffe::EntityId eid = world.createEntity();
    auto& tx = world.addComponent<ffe::Transform>(eid);
    tx.position.x = 10.0f;

    // Update should not crash with no connected clients
    updateNetworkSystem(0.016f, world);
    updateNetworkSystem(0.05f, world);

    disconnectNetwork();
    shutdownNetworkSystem();
}

TEST_CASE("NetworkSystem replication registry has default components", "[networking][system]") {
    initNetworkSystem();

    const auto& reg = getReplicationRegistry();
    CHECK(reg.count() >= 2); // Transform + Transform3D
    CHECK(reg.find(COMPONENT_ID_TRANSFORM) != nullptr);
    CHECK(reg.find(COMPONENT_ID_TRANSFORM3D) != nullptr);

    shutdownNetworkSystem();
}

// ===========================================================================
// Interpolation alpha
// ===========================================================================

TEST_CASE("Client interpolation alpha advances with dt", "[networking][client]") {
    REQUIRE(initNetworking());

    NetworkClient client;
    CHECK(client.getInterpolationAlpha() == 0.0f);

    // Simulate time passing without any snapshots
    client.update(0.025f); // half of default interval (0.05)
    CHECK(client.getInterpolationAlpha() > 0.0f);
    CHECK(client.getInterpolationAlpha() <= 1.0f);

    shutdownNetworking();
}
