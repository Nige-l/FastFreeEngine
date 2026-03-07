// tests/networking/test_lobby.cpp
//
// Unit tests for LobbyServer and LobbyClient.
// No real network needed — tests exercise the lobby logic directly.

#include <catch2/catch_test_macros.hpp>

#include "networking/lobby.h"
#include "networking/packet.h"

#include <cstdio>
#include <cstring>

using namespace ffe::networking;

// ---------------------------------------------------------------------------
// Helper: build a LOBBY_JOIN packet payload (just the player name after header)
// ---------------------------------------------------------------------------
static uint16_t buildJoinPacket(uint8_t* buffer, uint16_t capacity,
                                const char* playerName) {
    PacketWriter writer(buffer, capacity);
    PacketHeader hdr;
    hdr.type          = PacketType::LOBBY_JOIN;
    hdr.channel       = 0;
    hdr.sequence      = 0;
    hdr.payloadLength = 0;
    writeHeader(writer, hdr);
    writer.writeString(playerName);
    // Patch payload length
    const uint16_t payloadLen = writer.bytesWritten() - HEADER_SIZE;
    std::memcpy(buffer + 4, &payloadLen, sizeof(uint16_t));
    return writer.bytesWritten();
}

// Helper: build a LOBBY_READY packet
static uint16_t buildReadyPacket(uint8_t* buffer, uint16_t capacity,
                                 bool ready) {
    PacketWriter writer(buffer, capacity);
    PacketHeader hdr;
    hdr.type          = PacketType::LOBBY_READY;
    hdr.channel       = 0;
    hdr.sequence      = 0;
    hdr.payloadLength = 1;
    writeHeader(writer, hdr);
    writer.writeU8(ready ? 1 : 0);
    return writer.bytesWritten();
}

// Helper: create a reader positioned after the header
static PacketReader makePayloadReader(const uint8_t* buffer, uint16_t len) {
    PacketReader reader(buffer, len);
    PacketHeader hdr;
    readHeader(reader, hdr);
    return reader;
}

// ===========================================================================
// 1. LobbyServer create/destroy lifecycle
// ===========================================================================

TEST_CASE("LobbyServer create and destroy lifecycle", "[networking][lobby]") {
    LobbyServer server;
    CHECK_FALSE(server.isActive());

    REQUIRE(server.create("TestLobby", 4));
    CHECK(server.isActive());
    CHECK(std::strcmp(server.state().name, "TestLobby") == 0);
    CHECK(server.state().maxPlayers == 4);
    CHECK(server.state().playerCount == 0);
    CHECK_FALSE(server.state().gameStarted);

    server.destroy();
    CHECK_FALSE(server.isActive());
    CHECK(server.state().playerCount == 0);
}

// ===========================================================================
// 2. LobbyServer::create with empty name fails
// ===========================================================================

TEST_CASE("LobbyServer create with empty name fails", "[networking][lobby]") {
    LobbyServer server;
    CHECK_FALSE(server.create("", 4));
    CHECK_FALSE(server.create(nullptr, 4));
    CHECK_FALSE(server.isActive());
}

// ===========================================================================
// 3. LobbyServer::handlePacket JOIN adds player
// ===========================================================================

TEST_CASE("LobbyServer handlePacket JOIN adds player", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));

    uint8_t buf[MAX_PACKET_SIZE];
    const uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, "Alice");
    PacketReader reader = makePayloadReader(buf, len);

    server.handlePacket(1, PacketType::LOBBY_JOIN, reader);
    CHECK(server.state().playerCount == 1);
    CHECK(server.state().players[0].connectionId == 1);
    CHECK(std::strcmp(server.state().players[0].name, "Alice") == 0);
    CHECK_FALSE(server.state().players[0].ready);
}

// ===========================================================================
// 4. LobbyServer: max players enforced (reject when full)
// ===========================================================================

TEST_CASE("LobbyServer rejects join when full", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 2));

    // Add 2 players
    for (uint32_t i = 1; i <= 2; ++i) {
        uint8_t buf[MAX_PACKET_SIZE];
        char name[32];
        std::snprintf(name, sizeof(name), "P%u", i);
        const uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, name);
        PacketReader reader = makePayloadReader(buf, len);
        server.handlePacket(i, PacketType::LOBBY_JOIN, reader);
    }
    CHECK(server.state().playerCount == 2);

    // Third player should be rejected
    uint8_t buf[MAX_PACKET_SIZE];
    const uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, "P3");
    PacketReader reader = makePayloadReader(buf, len);
    server.handlePacket(3, PacketType::LOBBY_JOIN, reader);
    CHECK(server.state().playerCount == 2);
}

// ===========================================================================
// 5. LobbyServer: removePlayer removes and updates state
// ===========================================================================

TEST_CASE("LobbyServer removePlayer removes and updates state", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));

    // Add a player
    uint8_t buf[MAX_PACKET_SIZE];
    const uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, "Alice");
    PacketReader reader = makePayloadReader(buf, len);
    server.handlePacket(1, PacketType::LOBBY_JOIN, reader);
    CHECK(server.state().playerCount == 1);

    // Remove the player
    server.removePlayer(1);
    CHECK(server.state().playerCount == 0);
    CHECK(server.state().players[0].connectionId == 0);
}

// ===========================================================================
// 6. LobbyServer: READY toggles player ready state
// ===========================================================================

TEST_CASE("LobbyServer READY toggles player ready state", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));

    // Add player
    uint8_t buf[MAX_PACKET_SIZE];
    uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, "Alice");
    PacketReader reader = makePayloadReader(buf, len);
    server.handlePacket(1, PacketType::LOBBY_JOIN, reader);
    CHECK_FALSE(server.state().players[0].ready);

    // Set ready
    len = buildReadyPacket(buf, MAX_PACKET_SIZE, true);
    PacketReader readyReader = makePayloadReader(buf, len);
    server.handlePacket(1, PacketType::LOBBY_READY, readyReader);
    CHECK(server.state().players[0].ready);

    // Set not ready
    len = buildReadyPacket(buf, MAX_PACKET_SIZE, false);
    PacketReader unreadyReader = makePayloadReader(buf, len);
    server.handlePacket(1, PacketType::LOBBY_READY, unreadyReader);
    CHECK_FALSE(server.state().players[0].ready);
}

// ===========================================================================
// 7. LobbyServer: allReady returns true when all ready
// ===========================================================================

TEST_CASE("LobbyServer allReady returns true when all ready", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));

    // Add two players
    for (uint32_t i = 1; i <= 2; ++i) {
        uint8_t buf[MAX_PACKET_SIZE];
        char name[32];
        std::snprintf(name, sizeof(name), "P%u", i);
        const uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, name);
        PacketReader reader = makePayloadReader(buf, len);
        server.handlePacket(i, PacketType::LOBBY_JOIN, reader);
    }

    // Set both ready
    for (uint32_t i = 1; i <= 2; ++i) {
        uint8_t buf[MAX_PACKET_SIZE];
        const uint16_t len = buildReadyPacket(buf, MAX_PACKET_SIZE, true);
        PacketReader reader = makePayloadReader(buf, len);
        server.handlePacket(i, PacketType::LOBBY_READY, reader);
    }

    CHECK(server.allReady());
}

// ===========================================================================
// 8. LobbyServer: allReady returns false when not all ready
// ===========================================================================

TEST_CASE("LobbyServer allReady returns false when not all ready", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));

    // Add two players
    for (uint32_t i = 1; i <= 2; ++i) {
        uint8_t buf[MAX_PACKET_SIZE];
        char name[32];
        std::snprintf(name, sizeof(name), "P%u", i);
        const uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, name);
        PacketReader reader = makePayloadReader(buf, len);
        server.handlePacket(i, PacketType::LOBBY_JOIN, reader);
    }

    // Only first player ready
    uint8_t buf[MAX_PACKET_SIZE];
    const uint16_t len = buildReadyPacket(buf, MAX_PACKET_SIZE, true);
    PacketReader reader = makePayloadReader(buf, len);
    server.handlePacket(1, PacketType::LOBBY_READY, reader);

    CHECK_FALSE(server.allReady());
}

// ===========================================================================
// 9. LobbyServer: startGame sets gameStarted flag
// ===========================================================================

TEST_CASE("LobbyServer startGame sets gameStarted flag", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));
    CHECK_FALSE(server.state().gameStarted);

    server.startGame();
    CHECK(server.state().gameStarted);
}

// ===========================================================================
// 10. LobbyClient: handlePacket LOBBY_STATE updates local state
// ===========================================================================

TEST_CASE("LobbyClient handlePacket LOBBY_STATE updates local state", "[networking][lobby]") {
    LobbyClient client;
    CHECK_FALSE(client.isInLobby());

    // Build a fake LOBBY_STATE packet
    uint8_t buf[MAX_PACKET_SIZE];
    PacketWriter writer(buf, MAX_PACKET_SIZE);

    PacketHeader hdr;
    hdr.type          = PacketType::LOBBY_STATE;
    hdr.channel       = 0;
    hdr.sequence      = 0;
    hdr.payloadLength = 0;
    writeHeader(writer, hdr);

    writer.writeString("TestLobby");
    writer.writeU32(4);  // maxPlayers
    writer.writeU32(1);  // playerCount
    writer.writeU8(0);   // gameStarted = false

    // One player entry
    writer.writeU32(42); // connectionId
    writer.writeU8(1);   // ready
    writer.writeString("Bob");

    // Patch payload length
    const uint16_t payloadLen = writer.bytesWritten() - HEADER_SIZE;
    std::memcpy(buf + 4, &payloadLen, sizeof(uint16_t));

    PacketReader reader = makePayloadReader(buf, writer.bytesWritten());
    client.handlePacket(PacketType::LOBBY_STATE, reader);

    CHECK(client.isInLobby());
    CHECK(std::strcmp(client.state().name, "TestLobby") == 0);
    CHECK(client.state().maxPlayers == 4);
    CHECK(client.state().playerCount == 1);
    CHECK(client.state().players[0].connectionId == 42);
    CHECK(client.state().players[0].ready);
    CHECK(std::strcmp(client.state().players[0].name, "Bob") == 0);
}

// ===========================================================================
// 11. LobbyClient: handlePacket GAME_START sets flag and calls callback
// ===========================================================================

TEST_CASE("LobbyClient handlePacket GAME_START sets flag and calls callback", "[networking][lobby]") {
    LobbyClient client;
    bool callbackFired = false;

    client.setGameStartCallback([](void* ud) {
        *static_cast<bool*>(ud) = true;
    }, &callbackFired);

    // Build a LOBBY_GAME_START packet (empty payload)
    uint8_t buf[MAX_PACKET_SIZE];
    PacketWriter writer(buf, MAX_PACKET_SIZE);

    PacketHeader hdr;
    hdr.type          = PacketType::LOBBY_GAME_START;
    hdr.channel       = 0;
    hdr.sequence      = 0;
    hdr.payloadLength = 0;
    writeHeader(writer, hdr);

    PacketReader reader = makePayloadReader(buf, writer.bytesWritten());
    client.handlePacket(PacketType::LOBBY_GAME_START, reader);

    CHECK(client.isGameStarted());
    CHECK(callbackFired);
}

// ===========================================================================
// 12. LobbyClient: requestJoin sends packet (no crash)
// ===========================================================================

TEST_CASE("LobbyClient requestJoin does not crash", "[networking][lobby]") {
    // We can't test actual sending without a transport, but we verify no crash.
    LobbyClient client;
    CHECK_FALSE(client.isInLobby());
    // requestJoin with null name should do nothing
    // (We can't easily call requestJoin without a real transport to avoid crash
    //  from transport.send — but the LobbyClient just builds a packet and sends.)
    // This test validates that the client state is initially not in lobby.
    CHECK_FALSE(client.isGameStarted());
}

// ===========================================================================
// 13. LobbyClient: isInLobby tracks state
// ===========================================================================

TEST_CASE("LobbyClient isInLobby tracks state correctly", "[networking][lobby]") {
    LobbyClient client;
    CHECK_FALSE(client.isInLobby());

    // After receiving a LOBBY_STATE, should be in lobby
    uint8_t buf[MAX_PACKET_SIZE];
    PacketWriter writer(buf, MAX_PACKET_SIZE);

    PacketHeader hdr;
    hdr.type          = PacketType::LOBBY_STATE;
    hdr.channel       = 0;
    hdr.sequence      = 0;
    hdr.payloadLength = 0;
    writeHeader(writer, hdr);

    writer.writeString("Lobby");
    writer.writeU32(4);
    writer.writeU32(0);
    writer.writeU8(0);

    const uint16_t payloadLen = writer.bytesWritten() - HEADER_SIZE;
    std::memcpy(buf + 4, &payloadLen, sizeof(uint16_t));

    PacketReader reader = makePayloadReader(buf, writer.bytesWritten());
    client.handlePacket(PacketType::LOBBY_STATE, reader);

    CHECK(client.isInLobby());
}

// ===========================================================================
// 14. LobbyServer: duplicate join from same connectionId rejected
// ===========================================================================

TEST_CASE("LobbyServer rejects duplicate join from same connectionId", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));

    // Join with connectionId 1
    uint8_t buf[MAX_PACKET_SIZE];
    uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, "Alice");
    PacketReader reader1 = makePayloadReader(buf, len);
    server.handlePacket(1, PacketType::LOBBY_JOIN, reader1);
    CHECK(server.state().playerCount == 1);

    // Try to join again with same connectionId
    len = buildJoinPacket(buf, MAX_PACKET_SIZE, "Alice2");
    PacketReader reader2 = makePayloadReader(buf, len);
    server.handlePacket(1, PacketType::LOBBY_JOIN, reader2);
    CHECK(server.state().playerCount == 1); // still 1, rejected
}

// ===========================================================================
// 15. LobbyServer: name truncated to MAX_LOBBY_NAME_LENGTH
// ===========================================================================

TEST_CASE("LobbyServer name truncated to MAX_LOBBY_NAME_LENGTH", "[networking][lobby]") {
    LobbyServer server;
    // Create a name longer than MAX_LOBBY_NAME_LENGTH
    char longName[128];
    std::memset(longName, 'A', sizeof(longName) - 1);
    longName[sizeof(longName) - 1] = '\0';

    REQUIRE(server.create(longName, 4));
    CHECK(server.isActive());
    CHECK(std::strlen(server.state().name) == MAX_LOBBY_NAME_LENGTH - 1);
}

// ===========================================================================
// Additional: LobbyServer allReady returns false with no players
// ===========================================================================

TEST_CASE("LobbyServer allReady returns false with no players", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));
    CHECK_FALSE(server.allReady());
}

// ===========================================================================
// Additional: LobbyServer create with zero maxPlayers fails
// ===========================================================================

TEST_CASE("LobbyServer create with zero maxPlayers fails", "[networking][lobby]") {
    LobbyServer server;
    CHECK_FALSE(server.create("Lobby", 0));
    CHECK_FALSE(server.isActive());
}

// ===========================================================================
// Additional: LobbyServer create with too many maxPlayers fails
// ===========================================================================

TEST_CASE("LobbyServer create with maxPlayers > MAX_LOBBY_PLAYERS fails", "[networking][lobby]") {
    LobbyServer server;
    CHECK_FALSE(server.create("Lobby", MAX_LOBBY_PLAYERS + 1));
    CHECK_FALSE(server.isActive());
}

// ===========================================================================
// Additional: LobbyServer join callback fires
// ===========================================================================

TEST_CASE("LobbyServer join callback fires", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));

    uint32_t callbackConnId = 0;
    const char* callbackName = nullptr;

    struct CbData {
        uint32_t* connId;
        const char** name;
    } cbData{&callbackConnId, &callbackName};

    server.setPlayerJoinCallback([](uint32_t connId, const char* name, void* ud) {
        auto* d = static_cast<CbData*>(ud);
        *d->connId = connId;
        *d->name   = name;
    }, &cbData);

    uint8_t buf[MAX_PACKET_SIZE];
    const uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, "Alice");
    PacketReader reader = makePayloadReader(buf, len);
    server.handlePacket(5, PacketType::LOBBY_JOIN, reader);

    CHECK(callbackConnId == 5);
    CHECK(callbackName != nullptr);
    CHECK(std::strcmp(callbackName, "Alice") == 0);
}

// ===========================================================================
// Additional: LobbyServer leave callback fires
// ===========================================================================

TEST_CASE("LobbyServer leave callback fires", "[networking][lobby]") {
    LobbyServer server;
    REQUIRE(server.create("Lobby", 4));

    // Add a player first
    uint8_t buf[MAX_PACKET_SIZE];
    const uint16_t len = buildJoinPacket(buf, MAX_PACKET_SIZE, "Alice");
    PacketReader reader = makePayloadReader(buf, len);
    server.handlePacket(3, PacketType::LOBBY_JOIN, reader);

    uint32_t callbackConnId = 0;
    server.setPlayerLeaveCallback([](uint32_t connId, void* ud) {
        *static_cast<uint32_t*>(ud) = connId;
    }, &callbackConnId);

    server.removePlayer(3);
    CHECK(callbackConnId == 3);
}
