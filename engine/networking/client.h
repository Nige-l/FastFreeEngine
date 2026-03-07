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

    // -- Callbacks (function pointers + user data, no std::function) --
    using ConnectedCallback    = void(*)(void*);
    using DisconnectedCallback = void(*)(void*);
    using MessageCallback      = void(*)(const uint8_t* data, uint16_t len, void*);

    void setConnectedCallback(ConnectedCallback cb, void* userData);
    void setDisconnectedCallback(DisconnectedCallback cb, void* userData);
    void setMessageCallback(MessageCallback cb, void* userData);

private:
    ClientTransport m_transport;
    SnapshotBuffer  m_snapshots;
    uint32_t        m_clientId          = 0xFFFFFFFF;
    bool            m_connected         = false;
    float           m_interpolationAlpha = 0.0f;
    float           m_timeSinceSnapshot  = 0.0f;
    float           m_snapshotInterval   = 0.05f; // 1/20 Hz default

    // Callback storage
    ConnectedCallback    m_connectedCb      = nullptr;
    void*                m_connectedData    = nullptr;
    DisconnectedCallback m_disconnectedCb   = nullptr;
    void*                m_disconnectedData = nullptr;
    MessageCallback      m_messageCb        = nullptr;
    void*                m_messageData      = nullptr;

    // Internal transport-level callbacks
    static void onTransportConnect(void* userData);
    static void onTransportDisconnect(void* userData);
    static void onTransportReceive(const ReceivedPacket& pkt, void* userData);
};

} // namespace ffe::networking
