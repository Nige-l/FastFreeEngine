#pragma once

// engine/networking/server.h
//
// High-level game server built on top of ServerTransport and the
// replication system.  Manages the network tick loop, snapshot
// serialisation, and client lifecycle callbacks.
//
// No virtual functions, no std::function, no per-frame heap allocations.
//
// Tiers: LEGACY (primary), STANDARD, MODERN.

#include "networking/prediction.h"
#include "networking/replication.h"
#include "networking/transport.h"

#include <cstdint>

namespace ffe {
class World;
} // namespace ffe

namespace ffe::networking {

// ---------------------------------------------------------------------------
// Server configuration
// ---------------------------------------------------------------------------
struct ServerConfig {
    uint16_t port            = 7777;
    uint32_t maxClients      = 32;
    float    networkTickRate  = 20.0f; // Hz
};

// ---------------------------------------------------------------------------
// NetworkServer
// ---------------------------------------------------------------------------
class NetworkServer {
public:
    bool start(const ServerConfig& config);
    void stop();
    bool isRunning() const;

    /// Call each frame -- polls transport, processes incoming packets.
    void update(float dt);

    /// Network tick -- called at networkTickRate Hz.
    /// Gathers dirty components, builds a full snapshot, broadcasts to all
    /// connected clients.
    void networkTick(ffe::World& world, const ReplicationRegistry& registry);

    uint32_t clientCount() const;
    uint32_t tick() const; ///< Current network tick number

    /// Send raw data to all connected clients (reliable).
    bool broadcast(const uint8_t* data, uint16_t len);

    /// Send raw data to a specific client (reliable).
    bool sendTo(uint32_t clientId, const uint8_t* data, uint16_t len);

    /// Set the network tick rate (Hz). Clamped to [1, 120].
    void setTickRate(float hz);

    // -- Input handling --
    /// Set a callback that fires when a client sends an InputCommand.
    /// Signature: void(clientId, InputCommand, userData)
    void setInputCallback(InputCallbackFn cb, void* userData);

    /// Apply queued inputs from all clients. Called internally during networkTick.
    void applyQueuedInputs(ffe::World& world);

    // -- Callbacks (function pointers + user data, no std::function) --
    using ClientConnectCallback    = void(*)(uint32_t clientId, void*);
    using ClientDisconnectCallback = void(*)(uint32_t clientId, void*);
    using MessageCallback          = void(*)(uint32_t clientId,
                                             const uint8_t* data,
                                             uint16_t len, void*);

    void setClientConnectCallback(ClientConnectCallback cb, void* userData);
    void setClientDisconnectCallback(ClientDisconnectCallback cb, void* userData);
    void setMessageCallback(MessageCallback cb, void* userData);

private:
    ServerTransport m_transport;
    ServerConfig    m_config;
    uint32_t        m_tick            = 0;
    float           m_tickAccumulator = 0.0f;
    bool            m_running         = false;

    // Callback storage
    ClientConnectCallback    m_connectCb      = nullptr;
    void*                    m_connectData    = nullptr;
    ClientDisconnectCallback m_disconnectCb   = nullptr;
    void*                    m_disconnectData = nullptr;
    MessageCallback          m_messageCb      = nullptr;
    void*                    m_messageData    = nullptr;

    // Input callback storage
    InputCallbackFn m_inputCb       = nullptr;
    void*           m_inputCbData   = nullptr;

    // Per-connection input queue (fixed-size, no heap allocations)
    static constexpr uint32_t MAX_INPUT_QUEUE_PER_CLIENT = 8;
    static constexpr uint32_t MAX_SERVER_PEERS = 64; // matches ServerTransport::MAX_PEERS

    struct ClientInputQueue {
        InputCommand commands[MAX_INPUT_QUEUE_PER_CLIENT]{};
        uint32_t     count{0};
    };
    ClientInputQueue m_inputQueues[MAX_SERVER_PEERS]{};

    // Internal transport-level callbacks (static, forward to member state)
    static void onTransportConnect(ConnectionId id, void* userData);
    static void onTransportDisconnect(ConnectionId id, void* userData);
    static void onTransportReceive(const ReceivedPacket& pkt, void* userData);
};

} // namespace ffe::networking
