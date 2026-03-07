#pragma once

// engine/networking/transport.h
//
// Thin ENet wrapper providing server and client transport.
// No virtual functions, no std::function — callbacks are raw function pointers
// with a user-data void* (CLAUDE.md Section 3).
//
// Tiers: LEGACY, STANDARD, MODERN (ENet is pure C, no GPU involvement).

#include "networking/connection.h"
#include "networking/packet.h"

#include <cstdint>

// Forward-declare ENet types to avoid leaking the header.
struct _ENetHost;
struct _ENetPeer;

namespace ffe::networking {

// ---------------------------------------------------------------------------
// Global init / shutdown (call once at startup / shutdown)
// ---------------------------------------------------------------------------
bool initNetworking();
void shutdownNetworking();

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct TransportConfig {
    uint16_t port              = 7777;
    uint32_t maxClients        = 32;
    uint32_t channelCount      = 3;   // reliable, unreliable-seq, reliable-ordered
    uint32_t incomingBandwidth = 0;   // 0 = unlimited
    uint32_t outgoingBandwidth = 0;
};

// ---------------------------------------------------------------------------
// ServerTransport
// ---------------------------------------------------------------------------
class ServerTransport {
public:
    ServerTransport();
    ~ServerTransport();

    // Non-copyable, non-movable (owns ENet host)
    ServerTransport(const ServerTransport&)            = delete;
    ServerTransport& operator=(const ServerTransport&) = delete;
    ServerTransport(ServerTransport&&)                 = delete;
    ServerTransport& operator=(ServerTransport&&)      = delete;

    bool start(const TransportConfig& config);
    void stop();
    bool isRunning() const;

    /// Process incoming events.  Call once per frame.
    void poll(uint32_t timeoutMs = 0);

    bool send(ConnectionId client, uint8_t channel,
              const uint8_t* data, uint16_t len, bool reliable);
    void broadcast(uint8_t channel,
                   const uint8_t* data, uint16_t len, bool reliable);
    void disconnect(ConnectionId client);

    uint32_t clientCount() const;

    // -- Callbacks (set before start) --
    using ConnectCallback    = void(*)(ConnectionId, void*);
    using DisconnectCallback = void(*)(ConnectionId, void*);
    using ReceiveCallback    = void(*)(const ReceivedPacket&, void*);

    void setConnectCallback(ConnectCallback cb, void* userData);
    void setDisconnectCallback(DisconnectCallback cb, void* userData);
    void setReceiveCallback(ReceiveCallback cb, void* userData);

private:
    _ENetHost* m_host = nullptr;

    // Peer array — indexed by ConnectionId::id.
    // Fixed-size, allocated once in start().  MAX 32 peers (TransportConfig::maxClients).
    static constexpr uint32_t MAX_PEERS = 64;
    _ENetPeer* m_peers[MAX_PEERS]{};
    ConnectionRateLimit m_rateLimits[MAX_PEERS]{};
    uint32_t m_clientCount = 0;
    uint32_t m_maxClients  = 0;

    float m_currentTime = 0.0f; // incremented in poll()

    ConnectCallback    m_connectCb    = nullptr;
    void*              m_connectData  = nullptr;
    DisconnectCallback m_disconnectCb = nullptr;
    void*              m_disconnectData = nullptr;
    ReceiveCallback    m_receiveCb    = nullptr;
    void*              m_receiveData  = nullptr;

    ConnectionId peerToId(_ENetPeer* peer) const;
    _ENetPeer*   idToPeer(ConnectionId id) const;
};

// ---------------------------------------------------------------------------
// ClientTransport
// ---------------------------------------------------------------------------
class ClientTransport {
public:
    ClientTransport();
    ~ClientTransport();

    ClientTransport(const ClientTransport&)            = delete;
    ClientTransport& operator=(const ClientTransport&) = delete;
    ClientTransport(ClientTransport&&)                 = delete;
    ClientTransport& operator=(ClientTransport&&)      = delete;

    bool connect(const char* host, uint16_t port, uint32_t channelCount = 3);
    void disconnect();
    bool isConnected() const;

    void poll(uint32_t timeoutMs = 0);
    bool send(uint8_t channel, const uint8_t* data, uint16_t len, bool reliable);

    // -- Callbacks --
    using ConnectCallback    = void(*)(void*);
    using DisconnectCallback = void(*)(void*);
    using ReceiveCallback    = void(*)(const ReceivedPacket&, void*);

    void setConnectCallback(ConnectCallback cb, void* userData);
    void setDisconnectCallback(DisconnectCallback cb, void* userData);
    void setReceiveCallback(ReceiveCallback cb, void* userData);

private:
    _ENetHost* m_host = nullptr;
    _ENetPeer* m_peer = nullptr;
    ConnectionState m_state = ConnectionState::DISCONNECTED;

    ConnectCallback    m_connectCb      = nullptr;
    void*              m_connectData    = nullptr;
    DisconnectCallback m_disconnectCb   = nullptr;
    void*              m_disconnectData = nullptr;
    ReceiveCallback    m_receiveCb      = nullptr;
    void*              m_receiveData    = nullptr;
};

} // namespace ffe::networking
