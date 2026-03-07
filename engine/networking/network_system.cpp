// engine/networking/network_system.cpp
//
// Module-level networking integration.  Follows the physics module pattern
// with module-level globals and init/shutdown functions.

#include "networking/network_system.h"
#include "networking/packet.h"
#include "networking/transport.h"

namespace ffe::networking {

// ---------------------------------------------------------------------------
// Module-level state (like physics module pattern)
// ---------------------------------------------------------------------------

static NetworkServer     s_server;
static NetworkClient     s_client;
static ReplicationRegistry s_registry;
static bool              s_initialized  = false;
static bool              s_isServer     = false;
static bool              s_isClient     = false;

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

bool startServer(const uint16_t port, const uint32_t maxClients) {
    if (!s_initialized) { return false; }
    if (s_isServer || s_isClient) { return false; } // already active

    ServerConfig config;
    config.port       = port;
    config.maxClients = maxClients;

    if (!s_server.start(config)) {
        return false;
    }
    s_isServer = true;
    return true;
}

bool connectToServer(const char* host, const uint16_t port) {
    if (!s_initialized) { return false; }
    if (s_isServer || s_isClient) { return false; }

    if (!s_client.connect(host, port)) {
        return false;
    }
    s_isClient = true;
    return true;
}

void disconnectNetwork() {
    if (s_isServer) {
        s_server.stop();
        s_isServer = false;
    }
    if (s_isClient) {
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

} // namespace ffe::networking
