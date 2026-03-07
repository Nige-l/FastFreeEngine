// tests/networking/test_transport.cpp
//
// Unit + integration tests for the networking transport layer.
// These tests use ENet on loopback (127.0.0.1) — no real network needed.

#include <catch2/catch_test_macros.hpp>

#include "networking/connection.h"
#include "networking/transport.h"

#include <array>
#include <cstring>
#include <thread>
#include <chrono>

using namespace ffe::networking;

// ---------------------------------------------------------------------------
// Helper: poll both server and client for up to N ms
// ---------------------------------------------------------------------------
static void pollBoth(ServerTransport& server, ClientTransport& client,
                     const int iterations = 50, const int delayMs = 10) {
    for (int i = 0; i < iterations; ++i) {
        server.poll(0);
        client.poll(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

// ===========================================================================
// Init / shutdown
// ===========================================================================

TEST_CASE("initNetworking and shutdownNetworking succeed", "[networking][transport]") {
    REQUIRE(initNetworking());
    shutdownNetworking();

    // Can re-init after shutdown
    REQUIRE(initNetworking());
    shutdownNetworking();
}

// ===========================================================================
// ServerTransport start / stop
// ===========================================================================

TEST_CASE("ServerTransport start and stop", "[networking][transport]") {
    REQUIRE(initNetworking());

    ServerTransport server;
    TransportConfig config;
    config.port       = 17770;
    config.maxClients = 4;

    REQUIRE(server.start(config));
    CHECK(server.isRunning());
    CHECK(server.clientCount() == 0);

    server.stop();
    CHECK_FALSE(server.isRunning());

    shutdownNetworking();
}

TEST_CASE("ServerTransport double start fails", "[networking][transport]") {
    REQUIRE(initNetworking());

    ServerTransport server;
    TransportConfig config;
    config.port = 17771;
    REQUIRE(server.start(config));
    CHECK_FALSE(server.start(config)); // already running

    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// ClientTransport connect / disconnect (loopback)
// ===========================================================================

TEST_CASE("Client connects to server on loopback", "[networking][transport]") {
    REQUIRE(initNetworking());

    // Track connection events
    bool serverSawConnect = false;
    bool clientConnected  = false;

    ServerTransport server;
    TransportConfig config;
    config.port       = 17772;
    config.maxClients = 4;

    server.setConnectCallback([](ConnectionId /*id*/, void* ud) {
        *static_cast<bool*>(ud) = true;
    }, &serverSawConnect);

    REQUIRE(server.start(config));

    ClientTransport client;
    client.setConnectCallback([](void* ud) {
        *static_cast<bool*>(ud) = true;
    }, &clientConnected);

    REQUIRE(client.connect("127.0.0.1", 17772));

    // Poll until connected (up to 500ms)
    pollBoth(server, client, 50, 10);

    CHECK(clientConnected);
    CHECK(serverSawConnect);
    CHECK(client.isConnected());
    CHECK(server.clientCount() == 1);

    // Disconnect
    bool clientDisconnected = false;
    client.setDisconnectCallback([](void* ud) {
        *static_cast<bool*>(ud) = true;
    }, &clientDisconnected);

    client.disconnect();
    CHECK_FALSE(client.isConnected());

    // Give server time to notice
    for (int i = 0; i < 20; ++i) {
        server.poll(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// Data round-trip on loopback
// ===========================================================================

TEST_CASE("Client and server exchange data on loopback", "[networking][transport]") {
    REQUIRE(initNetworking());

    bool clientConnected = false;
    ReceivedPacket lastServerPacket{};
    ReceivedPacket lastClientPacket{};
    std::array<uint8_t, 32> serverRecvBuf{};
    std::array<uint8_t, 32> clientRecvBuf{};

    ServerTransport server;
    TransportConfig config;
    config.port       = 17773;
    config.maxClients = 4;

    // Capture what the server receives
    struct ServerCtx {
        ReceivedPacket* pkt;
        uint8_t*        buf;
    };
    ServerCtx sctx{&lastServerPacket, serverRecvBuf.data()};

    server.setReceiveCallback([](const ReceivedPacket& pkt, void* ud) {
        auto* ctx = static_cast<ServerCtx*>(ud);
        ctx->pkt->sender     = pkt.sender;
        ctx->pkt->channel    = pkt.channel;
        ctx->pkt->dataLength = pkt.dataLength;
        if (pkt.dataLength <= 32) {
            std::memcpy(ctx->buf, pkt.data, pkt.dataLength);
            ctx->pkt->data = ctx->buf;
        }
    }, &sctx);

    REQUIRE(server.start(config));

    ClientTransport client;
    client.setConnectCallback([](void* ud) {
        *static_cast<bool*>(ud) = true;
    }, &clientConnected);

    // Capture what the client receives
    struct ClientCtx {
        ReceivedPacket* pkt;
        uint8_t*        buf;
    };
    ClientCtx cctx{&lastClientPacket, clientRecvBuf.data()};

    client.setReceiveCallback([](const ReceivedPacket& pkt, void* ud) {
        auto* ctx = static_cast<ClientCtx*>(ud);
        ctx->pkt->sender     = pkt.sender;
        ctx->pkt->channel    = pkt.channel;
        ctx->pkt->dataLength = pkt.dataLength;
        if (pkt.dataLength <= 32) {
            std::memcpy(ctx->buf, pkt.data, pkt.dataLength);
            ctx->pkt->data = ctx->buf;
        }
    }, &cctx);

    REQUIRE(client.connect("127.0.0.1", 17773));
    pollBoth(server, client, 50, 10);
    REQUIRE(clientConnected);

    // Client sends to server
    const uint8_t msg[] = {0xDE, 0xAD};
    REQUIRE(client.send(0, msg, 2, true));
    pollBoth(server, client, 20, 10);

    CHECK(lastServerPacket.dataLength == 2);
    if (lastServerPacket.data) {
        CHECK(lastServerPacket.data[0] == 0xDE);
        CHECK(lastServerPacket.data[1] == 0xAD);
    }

    // Server sends back to client
    const ConnectionId clientId = lastServerPacket.sender;
    const uint8_t reply[] = {0xBE, 0xEF};
    REQUIRE(server.send(clientId, 0, reply, 2, true));
    pollBoth(server, client, 20, 10);

    CHECK(lastClientPacket.dataLength == 2);
    if (lastClientPacket.data) {
        CHECK(lastClientPacket.data[0] == 0xBE);
        CHECK(lastClientPacket.data[1] == 0xEF);
    }

    client.disconnect();
    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// Rate limiting
// ===========================================================================

TEST_CASE("ConnectionRateLimit allows within budget", "[networking][transport]") {
    ConnectionRateLimit rl;
    for (uint32_t i = 0; i < ConnectionRateLimit::MAX_PACKETS_PER_SECOND; ++i) {
        CHECK(rl.allowPacket(100, 0.0f));
    }
    // Next packet should be rejected
    CHECK_FALSE(rl.allowPacket(100, 0.0f));
}

TEST_CASE("ConnectionRateLimit resets after time window", "[networking][transport]") {
    ConnectionRateLimit rl;
    for (uint32_t i = 0; i < ConnectionRateLimit::MAX_PACKETS_PER_SECOND; ++i) {
        REQUIRE(rl.allowPacket(100, 0.0f));
    }
    CHECK_FALSE(rl.allowPacket(100, 0.5f)); // still within same second

    // New second — window resets
    CHECK(rl.allowPacket(100, 1.0f));
}

TEST_CASE("ConnectionRateLimit enforces byte limit", "[networking][transport]") {
    ConnectionRateLimit rl;
    // Send large packets to hit byte limit before packet limit
    const uint32_t bigPacket = 16384; // 16 KB
    CHECK(rl.allowPacket(bigPacket, 0.0f));      // 16 KB
    CHECK(rl.allowPacket(bigPacket, 0.0f));      // 32 KB
    CHECK(rl.allowPacket(bigPacket, 0.0f));      // 48 KB
    CHECK(rl.allowPacket(bigPacket, 0.0f));      // 64 KB — at limit
    CHECK_FALSE(rl.allowPacket(bigPacket, 0.0f)); // 80 KB — exceeds 64 KB limit
}
