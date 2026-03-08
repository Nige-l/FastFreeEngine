// engine/networking/network_system.cpp
//
// Module-level networking integration.  Follows the physics module pattern
// with module-level globals and init/shutdown functions.

#include "networking/network_system.h"
#include "networking/lobby.h"
#include "networking/packet.h"
#include "networking/transport.h"

#include <cstring>

namespace ffe::networking {

// ---------------------------------------------------------------------------
// Module-level state (like physics module pattern)
// ---------------------------------------------------------------------------

static NetworkServer       s_server;
static NetworkClient       s_client;
static ReplicationRegistry s_registry;
static LobbyServer         s_lobbyServer;
static LobbyClient         s_lobbyClient;
static LobbyState          s_emptyLobbyState{}; // returned when no lobby active
static bool                s_initialized  = false;
static bool                s_isServer     = false;
static bool                s_isClient     = false;

// ===========================================================================
// Internal: broadcast lobby state via the server's broadcast method
// ===========================================================================

static void broadcastLobbyStateViaServer() {
    if (!s_isServer || !s_lobbyServer.isActive()) { return; }

    // Serialize the lobby state into a packet and use s_server.broadcast()
    const LobbyState& ls = s_lobbyServer.state();

    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::LOBBY_STATE;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = 0; // patched below
    writeHeader(writer, header);

    writer.writeString(ls.name);
    writer.writeU32(ls.maxPlayers);
    writer.writeU32(ls.playerCount);
    writer.writeU8(ls.gameStarted ? 1 : 0);

    for (uint32_t i = 0; i < MAX_LOBBY_PLAYERS; ++i) {
        const LobbyPlayerInfo& info = ls.players[i];
        if (info.connectionId == 0xFFFFFFFF) { continue; }
        writer.writeU32(info.connectionId);
        writer.writeU8(info.ready ? 1 : 0);
        writer.writeString(info.name);
    }

    if (writer.hasError()) { return; }

    // Patch payload length
    const uint16_t payloadLen = writer.bytesWritten() - HEADER_SIZE;
    std::memcpy(buffer + 4, &payloadLen, sizeof(uint16_t));

    s_server.broadcast(buffer, writer.bytesWritten());
}

// ===========================================================================
// Lobby packet routing callbacks (internal)
// ===========================================================================

// Server-side: intercept lobby packets from clients, forward rest to user cb.
static NetworkServer::MessageCallback s_userServerMsgCb   = nullptr;
static void*                          s_userServerMsgData = nullptr;

static void lobbyServerMessageCb(const uint32_t clientId,
                                 const uint8_t* data, const uint16_t len,
                                 void* /*userData*/) {
    // Try to parse header to detect lobby packets
    if (len >= HEADER_SIZE) {
        PacketReader reader(data, len);
        PacketHeader header;
        if (readHeader(reader, header)) {
            switch (header.type) {
            case PacketType::LOBBY_JOIN:
            case PacketType::LOBBY_LEAVE:
            case PacketType::LOBBY_READY:
                s_lobbyServer.handlePacket(clientId, header.type, reader);
                broadcastLobbyStateViaServer();
                return;
            default:
                break;
            }
        }
    }

    // Not a lobby packet — forward to user callback
    if (s_userServerMsgCb != nullptr) {
        s_userServerMsgCb(clientId, data, len, s_userServerMsgData);
    }
}

// Client-side: lobby packet callback (receives full packet incl. header)
static void lobbyClientPacketCb(const uint8_t* data, const uint16_t len,
                                void* /*userData*/) {
    if (len < HEADER_SIZE) { return; }

    PacketReader reader(data, len);
    PacketHeader header;
    if (!readHeader(reader, header)) { return; }

    s_lobbyClient.handlePacket(header.type, reader);
}

// ===========================================================================
// Init / Shutdown
// ===========================================================================

void initNetworkSystem() {
    if (s_initialized) { return; }
    initNetworking();
    registerDefaultComponents(s_registry);
    s_initialized = true;
}

void shutdownNetworkSystem() {
    if (!s_initialized) { return; }
    disconnectNetwork();
    shutdownNetworking();
    s_initialized = false;
    s_isServer    = false;
    s_isClient    = false;
    s_registry    = ReplicationRegistry{}; // reset
}

// ===========================================================================
// Per-frame update
// ===========================================================================

void updateNetworkSystem(const float dt, ffe::World& world) {
    if (!s_initialized) { return; }

    if (s_isServer) {
        s_server.update(dt);
        s_server.networkTick(world, s_registry);
    } else if (s_isClient) {
        s_client.update(dt);
        s_client.applySnapshots(world, s_registry);
    }
}

// ===========================================================================
// Accessors
// ===========================================================================

NetworkServer* getServer() {
    return s_isServer ? &s_server : nullptr;
}

NetworkClient* getClient() {
    return s_isClient ? &s_client : nullptr;
}

bool isServer() { return s_isServer; }
bool isClient() { return s_isClient; }

// ===========================================================================
// Control
// ===========================================================================

// Server disconnect handler: remove player from lobby on disconnect
static NetworkServer::ClientDisconnectCallback s_userDisconnectCb   = nullptr;
static void*                                   s_userDisconnectData = nullptr;

static void lobbyDisconnectCb(const uint32_t clientId, void* /*userData*/) {
    if (s_lobbyServer.isActive()) {
        s_lobbyServer.removePlayer(clientId);
        broadcastLobbyStateViaServer();
    }
    // Forward to user callback
    if (s_userDisconnectCb != nullptr) {
        s_userDisconnectCb(clientId, s_userDisconnectData);
    }
}

bool startServer(const uint16_t port, const uint32_t maxClients) {
    if (!s_initialized) { return false; }
    if (s_isServer || s_isClient) { return false; } // already active

    ServerConfig config;
    config.port       = port;
    config.maxClients = maxClients;

    if (!s_server.start(config)) {
        return false;
    }

    // Wire up lobby packet interception on the server's message callback
    s_server.setMessageCallback(lobbyServerMessageCb, nullptr);

    // Wire up disconnect callback for lobby player removal
    s_server.setClientDisconnectCallback(lobbyDisconnectCb, nullptr);

    s_isServer = true;
    return true;
}

bool connectToServer(const char* host, const uint16_t port) {
    if (!s_initialized) { return false; }
    if (s_isServer || s_isClient) { return false; }

    if (!s_client.connect(host, port)) {
        return false;
    }

    // Wire up lobby packet callback on the client
    s_client.setLobbyPacketCallback(lobbyClientPacketCb, nullptr);

    s_isClient = true;
    return true;
}

void disconnectNetwork() {
    if (s_isServer) {
        s_lobbyServer.destroy();
        s_server.stop();
        s_isServer = false;
    }
    if (s_isClient) {
        s_lobbyClient = LobbyClient{};
        s_client.disconnect();
        s_isClient = false;
    }
}

// ===========================================================================
// Messaging
// ===========================================================================

bool sendGameMessage(const uint8_t msgType, const uint8_t* data, const uint16_t len) {
    if (!s_initialized) { return false; }

    // Build an EVENT packet: header + msgType(u8) + payload
    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::EVENT;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = static_cast<uint16_t>(1u + len);
    writeHeader(writer, header);
    writer.writeU8(msgType);
    if (len > 0 && data != nullptr) {
        writer.writeBytes(data, len);
    }

    if (writer.hasError()) { return false; }

    if (s_isServer) {
        return s_server.broadcast(buffer, writer.bytesWritten());
    }
    if (s_isClient) {
        return s_client.send(buffer, writer.bytesWritten());
    }
    return false;
}

bool sendGameMessageTo(const uint32_t clientId, const uint8_t msgType,
                       const uint8_t* data, const uint16_t len) {
    if (!s_initialized || !s_isServer) { return false; }

    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::EVENT;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = static_cast<uint16_t>(1u + len);
    writeHeader(writer, header);
    writer.writeU8(msgType);
    if (len > 0 && data != nullptr) {
        writer.writeBytes(data, len);
    }

    if (writer.hasError()) { return false; }

    return s_server.sendTo(clientId, buffer, writer.bytesWritten());
}

// ===========================================================================
// Tick rate
// ===========================================================================

void setNetworkTickRate(const float hz) {
    if (s_isServer) {
        s_server.setTickRate(hz);
    }
}

ReplicationRegistry& getReplicationRegistry() {
    return s_registry;
}

// ===========================================================================
// Prediction (client-side)
// ===========================================================================

void setLocalPlayer(const uint32_t entityId) {
    if (s_isClient) {
        s_client.setLocalEntity(entityId);
    }
}

bool sendInput(const InputCommand& cmd) {
    if (!s_isClient) { return false; }
    return s_client.sendInput(cmd);
}

void setMovementFunction(const MoveFn fn, void* userData) {
    if (s_isClient) {
        s_client.setMovementFunction(fn, userData);
    }
}

float getPredictionError() {
    if (s_isClient) {
        return s_client.getPredictionError();
    }
    return 0.0f;
}

uint32_t getCurrentNetworkTick() {
    if (s_isServer) {
        return s_server.tick();
    }
    if (s_isClient) {
        return s_client.getCurrentPredictionTick();
    }
    return 0;
}

// ===========================================================================
// Input handling (server-side)
// ===========================================================================

void setInputCallback(const InputCallbackFn cb, void* userData) {
    if (s_isServer) {
        s_server.setInputCallback(cb, userData);
    }
}

// ===========================================================================
// Lag compensation (server-side rewind)
// ===========================================================================

HitCheckResult performHitCheck(const float originX, const float originY, const float originZ,
                               const float dirX, const float dirY, const float dirZ,
                               const float maxDist, const uint32_t ignoreEntity) {
    if (s_isServer) {
        // Server: rewind to current tick (caller may pass specific tick via processHitCheck)
        return s_server.processHitCheck(
            0, // shooterConnectionId unknown at module level; use 0
            s_server.tick(), originX, originY, originZ,
            dirX, dirY, dirZ, maxDist, ignoreEntity);
    }
    if (s_isClient) {
        // Client: send HIT_CHECK packet to server
        uint8_t buffer[MAX_PACKET_SIZE];
        PacketWriter writer(buffer, MAX_PACKET_SIZE);

        PacketHeader header;
        header.type          = PacketType::HIT_CHECK;
        header.channel       = 0;
        header.sequence      = 0;
        header.payloadLength = 0; // patched below
        writeHeader(writer, header);

        writer.writeF32(originX);
        writer.writeF32(originY);
        writer.writeF32(originZ);
        writer.writeF32(dirX);
        writer.writeF32(dirY);
        writer.writeF32(dirZ);
        writer.writeF32(maxDist);
        writer.writeU32(ignoreEntity);

        if (!writer.hasError()) {
            const uint16_t payloadLen = writer.bytesWritten() - HEADER_SIZE;
            std::memcpy(buffer + 4, &payloadLen, sizeof(uint16_t));
            s_client.send(buffer, writer.bytesWritten());
        }
    }
    return HitCheckResult{};
}

void setLagCompensationWindow(const uint32_t ticks) {
    if (s_isServer) {
        s_server.lagCompensator().setMaxRewindTicks(ticks);
    }
}

void onHitConfirm(const HitConfirmFn fn, void* userData) {
    if (s_isServer) {
        s_server.setHitConfirmCallback(fn, userData);
    }
}

// ===========================================================================
// Lobby / Matchmaking
// ===========================================================================

bool createLobby(const char* name, const uint32_t maxPlayers) {
    if (!s_isServer) { return false; }
    return s_lobbyServer.create(name, maxPlayers);
}

void destroyLobby() {
    if (!s_isServer) { return; }
    s_lobbyServer.destroy();
}

bool isLobbyActive() {
    if (s_isServer) { return s_lobbyServer.isActive(); }
    return false;
}

void startLobbyGame() {
    if (!s_isServer || !s_lobbyServer.isActive()) { return; }

    // Mark game as started on the server-side lobby
    s_lobbyServer.startGame();

    // Build and broadcast LOBBY_GAME_START packet to all clients
    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::LOBBY_GAME_START;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = 0;
    writeHeader(writer, header);

    if (!writer.hasError()) {
        s_server.broadcast(buffer, writer.bytesWritten());
    }
}

bool joinLobby(const char* playerName) {
    if (!s_isClient) { return false; }
    // The client transport is internal to NetworkClient — we need to send
    // a lobby join packet via the client's send method.
    if (playerName == nullptr || playerName[0] == '\0') { return false; }

    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::LOBBY_JOIN;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = 0; // patched
    writeHeader(writer, header);
    writer.writeString(playerName);

    if (writer.hasError()) { return false; }

    const uint16_t payloadLen = writer.bytesWritten() - HEADER_SIZE;
    std::memcpy(buffer + 4, &payloadLen, sizeof(uint16_t));

    return s_client.send(buffer, writer.bytesWritten());
}

void leaveLobby() {
    if (!s_isClient) { return; }

    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::LOBBY_LEAVE;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = 0;
    writeHeader(writer, header);

    if (!writer.hasError()) {
        s_client.send(buffer, writer.bytesWritten());
    }
}

void setReady(const bool ready) {
    if (!s_isClient) { return; }

    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::LOBBY_READY;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = 1;
    writeHeader(writer, header);
    writer.writeU8(ready ? 1 : 0);

    if (!writer.hasError()) {
        s_client.send(buffer, writer.bytesWritten());
    }
}

bool isInLobby() {
    if (s_isClient) { return s_lobbyClient.isInLobby(); }
    return false;
}

const LobbyState& getLobbyState() {
    if (s_isServer && s_lobbyServer.isActive()) {
        return s_lobbyServer.state();
    }
    if (s_isClient) {
        return s_lobbyClient.state();
    }
    return s_emptyLobbyState;
}

void setLobbyUpdateCallback(const LobbyUpdateFn fn, void* userData) {
    s_lobbyClient.setLobbyUpdateCallback(fn, userData);
}

void setGameStartCallback(const GameStartFn fn, void* userData) {
    s_lobbyClient.setGameStartCallback(fn, userData);
}

} // namespace ffe::networking
