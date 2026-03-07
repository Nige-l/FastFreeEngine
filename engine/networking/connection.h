#pragma once

// engine/networking/connection.h
//
// Connection state machine and per-connection types used by both
// ServerTransport and ClientTransport.

#include <cstdint>

namespace ffe::networking {

// ---------------------------------------------------------------------------
// Connection state
// ---------------------------------------------------------------------------
enum class ConnectionState : uint8_t {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};

// ---------------------------------------------------------------------------
// Connection handle — lightweight value type
// ---------------------------------------------------------------------------
struct ConnectionId {
    uint32_t id = 0xFFFFFFFF;
};

inline bool isValid(const ConnectionId c) { return c.id != 0xFFFFFFFF; }

// ---------------------------------------------------------------------------
// Per-connection rate limiter
// ---------------------------------------------------------------------------
struct ConnectionRateLimit {
    uint32_t m_packetsThisSecond = 0;
    uint32_t m_bytesThisSecond   = 0;
    float    m_windowStart       = 0.0f;

    static constexpr uint32_t MAX_PACKETS_PER_SECOND = 100;
    static constexpr uint32_t MAX_BYTES_PER_SECOND   = 65536; // 64 KB/s

    /// Return true if the packet should be allowed through.
    bool allowPacket(uint32_t packetSize, float currentTime) {
        // Roll over window every second
        if (currentTime - m_windowStart >= 1.0f) {
            m_packetsThisSecond = 0;
            m_bytesThisSecond   = 0;
            m_windowStart       = currentTime;
        }
        if (m_packetsThisSecond >= MAX_PACKETS_PER_SECOND) { return false; }
        if (m_bytesThisSecond + packetSize > MAX_BYTES_PER_SECOND) { return false; }
        ++m_packetsThisSecond;
        m_bytesThisSecond += packetSize;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Received packet data (non-owning view)
// ---------------------------------------------------------------------------
struct ReceivedPacket {
    ConnectionId    sender;
    uint8_t         channel    = 0;
    const uint8_t*  data       = nullptr;
    uint16_t        dataLength = 0;
};

} // namespace ffe::networking
