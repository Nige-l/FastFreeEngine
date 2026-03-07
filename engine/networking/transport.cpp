// engine/networking/transport.cpp

#include "networking/transport.h"

#include <enet/enet.h>

#include <cstring> // memset

namespace ffe::networking {

// ===========================================================================
// Global init / shutdown
// ===========================================================================

bool initNetworking() {
    return enet_initialize() == 0;
}

void shutdownNetworking() {
    enet_deinitialize();
}

// ===========================================================================
// ServerTransport
// ===========================================================================

ServerTransport::ServerTransport() {
    std::memset(m_peers, 0, sizeof(m_peers));
}

ServerTransport::~ServerTransport() { stop(); }

bool ServerTransport::start(const TransportConfig& config) {
    if (m_host) { return false; } // already running

    const uint32_t maxClients = (config.maxClients > MAX_PEERS)
                                    ? MAX_PEERS
                                    : config.maxClients;
    m_maxClients = maxClients;

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = config.port;

    m_host = enet_host_create(&address,
                              maxClients,
                              config.channelCount,
                              config.incomingBandwidth,
                              config.outgoingBandwidth);
    return m_host != nullptr;
}

void ServerTransport::stop() {
    if (!m_host) { return; }

    // Disconnect all peers gracefully
    for (uint32_t i = 0; i < m_maxClients; ++i) {
        if (m_peers[i]) {
            enet_peer_disconnect_now(m_peers[i], 0);
            m_peers[i] = nullptr;
        }
    }
    m_clientCount = 0;

    enet_host_destroy(m_host);
    m_host = nullptr;
}

bool ServerTransport::isRunning() const { return m_host != nullptr; }

void ServerTransport::poll(const uint32_t timeoutMs) {
    if (!m_host) { return; }

    // Advance simple time counter (millisecond-resolution is fine for rate limiting)
    m_currentTime += 0.001f * static_cast<float>(timeoutMs > 0 ? timeoutMs : 1);

    ENetEvent event;
    while (enet_host_service(m_host, &event, timeoutMs) > 0) {
        // Only use the timeout on the first iteration
        // Subsequent calls are non-blocking to drain the queue.
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            const ConnectionId cid = peerToId(event.peer);
            if (!isValid(cid)) {
                // Find a free slot
                for (uint32_t i = 0; i < m_maxClients; ++i) {
                    if (!m_peers[i]) {
                        m_peers[i] = event.peer;
                        // Store i+1 so that 0 (NULL) means "unassigned"
                        event.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(i + 1));
                        m_rateLimits[i] = ConnectionRateLimit{};
                        ++m_clientCount;
                        const ConnectionId newId{i};
                        if (m_connectCb) { m_connectCb(newId, m_connectData); }
                        break;
                    }
                }
            }
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            const ConnectionId cid = peerToId(event.peer);
            if (isValid(cid) && cid.id < m_maxClients) {
                m_peers[cid.id] = nullptr;
                if (m_clientCount > 0) { --m_clientCount; }
                if (m_disconnectCb) { m_disconnectCb(cid, m_disconnectData); }
            }
            event.peer->data = nullptr;
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            const ConnectionId cid = peerToId(event.peer);
            if (isValid(cid) && cid.id < m_maxClients) {
                // Rate limit check
                const auto packetLen = static_cast<uint16_t>(
                    event.packet->dataLength > MAX_PACKET_SIZE
                        ? MAX_PACKET_SIZE
                        : event.packet->dataLength);

                if (m_rateLimits[cid.id].allowPacket(packetLen, m_currentTime)) {
                    if (event.packet->dataLength <= MAX_PACKET_SIZE && m_receiveCb) {
                        ReceivedPacket rp;
                        rp.sender     = cid;
                        rp.channel    = event.channelID;
                        rp.data       = event.packet->data;
                        rp.dataLength = packetLen;
                        m_receiveCb(rp, m_receiveData);
                    }
                }
                // else: silently dropped (no response — prevents amplification)
            }
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_NONE:
            break;
        }

    }
}

bool ServerTransport::send(const ConnectionId client, const uint8_t channel,
                           const uint8_t* data, const uint16_t len,
                           const bool reliable) {
    auto* peer = idToPeer(client);
    if (!peer || !m_host) { return false; }
    if (len > MAX_PACKET_SIZE) { return false; }

    const uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE
                                    : ENET_PACKET_FLAG_UNSEQUENCED;
    ENetPacket* packet = enet_packet_create(data, len, flags);
    if (!packet) { return false; }
    return enet_peer_send(peer, channel, packet) == 0;
}

void ServerTransport::broadcast(const uint8_t channel,
                                const uint8_t* data, const uint16_t len,
                                const bool reliable) {
    if (!m_host) { return; }
    if (len > MAX_PACKET_SIZE) { return; }

    const uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE
                                    : ENET_PACKET_FLAG_UNSEQUENCED;
    ENetPacket* packet = enet_packet_create(data, len, flags);
    if (!packet) { return; }
    enet_host_broadcast(m_host, channel, packet);
}

void ServerTransport::disconnect(const ConnectionId client) {
    auto* peer = idToPeer(client);
    if (!peer) { return; }
    enet_peer_disconnect(peer, 0);
}

uint32_t ServerTransport::clientCount() const { return m_clientCount; }

void ServerTransport::setConnectCallback(ConnectCallback cb, void* userData) {
    m_connectCb   = cb;
    m_connectData = userData;
}

void ServerTransport::setDisconnectCallback(DisconnectCallback cb, void* userData) {
    m_disconnectCb   = cb;
    m_disconnectData = userData;
}

void ServerTransport::setReceiveCallback(ReceiveCallback cb, void* userData) {
    m_receiveCb   = cb;
    m_receiveData = userData;
}

ConnectionId ServerTransport::peerToId(_ENetPeer* peer) const {
    if (!peer || !peer->data) { return ConnectionId{}; }
    // peer->data stores index+1 (0 means unassigned)
    const auto raw = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(peer->data));
    if (raw == 0 || raw > MAX_PEERS) { return ConnectionId{}; }
    return ConnectionId{raw - 1};
}

_ENetPeer* ServerTransport::idToPeer(const ConnectionId id) const {
    if (!isValid(id) || id.id >= MAX_PEERS) { return nullptr; }
    return m_peers[id.id];
}

// ===========================================================================
// ClientTransport
// ===========================================================================

ClientTransport::ClientTransport()  = default;

ClientTransport::~ClientTransport() { disconnect(); }

bool ClientTransport::connect(const char* host, const uint16_t port,
                              const uint32_t channelCount) {
    if (m_host) { return false; } // already connected or connecting

    m_host = enet_host_create(nullptr, // client — no incoming connections
                              1,       // one outgoing connection
                              channelCount,
                              0, 0);   // unlimited bandwidth
    if (!m_host) { return false; }

    ENetAddress address;
    enet_address_set_host(&address, host);
    address.port = port;

    m_peer = enet_host_connect(m_host, &address, channelCount, 0);
    if (!m_peer) {
        enet_host_destroy(m_host);
        m_host = nullptr;
        return false;
    }

    m_state = ConnectionState::CONNECTING;
    return true;
}

void ClientTransport::disconnect() {
    if (m_peer && m_state == ConnectionState::CONNECTED) {
        enet_peer_disconnect(m_peer, 0);
        m_state = ConnectionState::DISCONNECTING;

        // Wait briefly for the disconnect to complete
        ENetEvent event;
        bool disconnected = false;
        for (int i = 0; i < 10 && !disconnected; ++i) {
            if (enet_host_service(m_host, &event, 100) > 0) {
                if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                    disconnected = true;
                }
                if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    enet_packet_destroy(event.packet);
                }
            }
        }

        if (!disconnected && m_peer) {
            enet_peer_reset(m_peer);
        }
    } else if (m_peer) {
        enet_peer_reset(m_peer);
    }

    m_peer  = nullptr;
    m_state = ConnectionState::DISCONNECTED;

    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
}

bool ClientTransport::isConnected() const {
    return m_state == ConnectionState::CONNECTED;
}

void ClientTransport::poll(const uint32_t timeoutMs) {
    if (!m_host) { return; }

    ENetEvent event;
    while (enet_host_service(m_host, &event, timeoutMs) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            m_state = ConnectionState::CONNECTED;
            if (m_connectCb) { m_connectCb(m_connectData); }
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            m_state = ConnectionState::DISCONNECTED;
            m_peer  = nullptr;
            if (m_disconnectCb) { m_disconnectCb(m_disconnectData); }
            break;

        case ENET_EVENT_TYPE_RECEIVE: {
            if (event.packet->dataLength <= MAX_PACKET_SIZE && m_receiveCb) {
                ReceivedPacket rp;
                rp.sender     = ConnectionId{0}; // server is always 0 from client perspective
                rp.channel    = event.channelID;
                rp.data       = event.packet->data;
                rp.dataLength = static_cast<uint16_t>(event.packet->dataLength);
                m_receiveCb(rp, m_receiveData);
            }
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }
}

bool ClientTransport::send(const uint8_t channel, const uint8_t* data,
                           const uint16_t len, const bool reliable) {
    if (!m_peer || m_state != ConnectionState::CONNECTED) { return false; }
    if (len > MAX_PACKET_SIZE) { return false; }

    const uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE
                                    : ENET_PACKET_FLAG_UNSEQUENCED;
    ENetPacket* packet = enet_packet_create(data, len, flags);
    if (!packet) { return false; }
    return enet_peer_send(m_peer, channel, packet) == 0;
}

void ClientTransport::setConnectCallback(ConnectCallback cb, void* userData) {
    m_connectCb   = cb;
    m_connectData = userData;
}

void ClientTransport::setDisconnectCallback(DisconnectCallback cb, void* userData) {
    m_disconnectCb   = cb;
    m_disconnectData = userData;
}

void ClientTransport::setReceiveCallback(ReceiveCallback cb, void* userData) {
    m_receiveCb   = cb;
    m_receiveData = userData;
}

} // namespace ffe::networking
