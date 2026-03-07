#pragma once

// engine/networking/client.h
//
// High-level game client built on top of ClientTransport and the
// replication system.  Receives snapshot packets from the server and
// applies them to the local World.
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
// NetworkClient
// ---------------------------------------------------------------------------
class NetworkClient {
public:
    bool connect(const char* host, uint16_t port);
    void disconnect();
    bool isConnected() const;

    /// Call each frame -- polls transport for incoming packets.
    void update(float dt);

    /// Apply the latest received snapshot to the World.
    void applySnapshots(ffe::World& world, const ReplicationRegistry& registry);

    /// Interpolation alpha: fraction of time elapsed between the two most
    /// recent snapshots.  Useful for smooth rendering between network ticks.
    float getInterpolationAlpha() const;

    uint32_t clientId() const;

    /// Send raw data to the server (reliable).
    bool send(const uint8_t* data, uint16_t len);

    // -- Prediction --
    /// Set the locally predicted entity (client-side prediction).
    void setLocalEntity(uint32_t entityId);

    /// Set the movement function for client-side prediction.
    void setMovementFunction(MoveFn fn, void* userData);

    /// Record input and apply predicted movement locally.
    void recordAndPredict(ffe::World& world, const InputCommand& cmd);

    /// Send an InputCommand to the server.
    bool sendInput(const InputCommand& cmd);

    /// Get the last acknowledged server tick (from most recent snapshot).
    uint32_t getLastAcknowledgedTick() const;

    /// Get the prediction error from the last reconciliation.
    float getPredictionError() const;

    /// Get the current client prediction tick.
    uint32_t getCurrentPredictionTick() const;

    /// Access the prediction subsystem directly.
    ClientPrediction& prediction();
    const ClientPrediction& prediction() const;

    // -- Callbacks (function pointers + user data, no std::function) --
    using ConnectedCallback    = void(*)(void*);
    using DisconnectedCallback = void(*)(void*);
    using MessageCallback      = void(*)(const uint8_t* data, uint16_t len, void*);

    void setConnectedCallback(ConnectedCallback cb, void* userData);
    void setDisconnectedCallback(DisconnectedCallback cb, void* userData);
    void setMessageCallback(MessageCallback cb, void* userData);

    /// Callback for lobby packets (LOBBY_STATE, LOBBY_GAME_START).
    /// Receives full raw packet data (including header) so the caller
    /// can parse the header and dispatch.
    using LobbyPacketCallback = void(*)(const uint8_t* data, uint16_t len, void*);
    void setLobbyPacketCallback(LobbyPacketCallback cb, void* userData);

private:
    ClientTransport  m_transport;
    SnapshotBuffer   m_snapshots;
    ClientPrediction m_prediction;
    uint32_t         m_clientId             = 0xFFFFFFFF;
    uint32_t         m_lastAcknowledgedTick = 0;
    bool             m_connected            = false;
    float            m_interpolationAlpha   = 0.0f;
    float            m_timeSinceSnapshot    = 0.0f;
    float            m_snapshotInterval     = 0.05f; // 1/20 Hz default

    // Callback storage
    ConnectedCallback    m_connectedCb      = nullptr;
    void*                m_connectedData    = nullptr;
    DisconnectedCallback m_disconnectedCb   = nullptr;
    void*                m_disconnectedData = nullptr;
    MessageCallback      m_messageCb        = nullptr;
    void*                m_messageData      = nullptr;
    LobbyPacketCallback  m_lobbyCb          = nullptr;
    void*                m_lobbyData        = nullptr;

    // Internal transport-level callbacks
    static void onTransportConnect(void* userData);
    static void onTransportDisconnect(void* userData);
    static void onTransportReceive(const ReceivedPacket& pkt, void* userData);
};

} // namespace ffe::networking
