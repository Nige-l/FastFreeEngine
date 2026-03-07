#pragma once

// engine/networking/packet.h
//
// Binary packet format for FFE networking.  Bounds-checked reader/writer
// ensure no out-of-bounds access regardless of input.  All reads/writes
// return false on failure and set an internal error flag.
//
// Tiers: LEGACY, STANDARD, MODERN (no GPU involvement).

#include <cstdint>

namespace ffe::networking {

// ---------------------------------------------------------------------------
// Packet types (wire values — do NOT reorder)
// ---------------------------------------------------------------------------
enum class PacketType : uint8_t {
    CONNECT_REQUEST  = 0x01,
    CONNECT_ACCEPT   = 0x02,
    CONNECT_REJECT   = 0x03,
    DISCONNECT       = 0x04,

    INPUT            = 0x10,
    SNAPSHOT_FULL    = 0x11,
    SNAPSHOT_DELTA   = 0x12,

    EVENT            = 0x20,
    RPC              = 0x21,

    LOBBY_CREATE     = 0x30,
    LOBBY_JOIN       = 0x31,
    LOBBY_LEAVE      = 0x32,
    LOBBY_READY      = 0x33,
    LOBBY_STATE      = 0x34,
    LOBBY_GAME_START = 0x35,

    HIT_CHECK        = 0x40,

    HEARTBEAT        = 0xF0
};

/// Return true if \p t is a known PacketType value.
bool isValidPacketType(uint8_t t);

// ---------------------------------------------------------------------------
// Lobby shared types
// ---------------------------------------------------------------------------
constexpr uint32_t MAX_LOBBY_PLAYERS     = 16;
constexpr uint32_t MAX_LOBBY_NAME_LENGTH = 32;

/// Compact player info for lobby state broadcasts.
/// Fixed-size, no heap allocations — safe for direct serialization.
struct LobbyPlayerInfo {
    uint32_t connectionId{0};
    bool     ready{false};
    char     name[MAX_LOBBY_NAME_LENGTH]{}; // null-terminated
};

// ---------------------------------------------------------------------------
// Packet header — 6 bytes on the wire
// ---------------------------------------------------------------------------
struct PacketHeader {
    PacketType type{};
    uint8_t    channel{};
    uint16_t   sequence{};
    uint16_t   payloadLength{};
};

// ---------------------------------------------------------------------------
// Size constants
// ---------------------------------------------------------------------------
constexpr uint16_t MAX_PACKET_SIZE  = 1200; // MTU-safe
constexpr uint16_t HEADER_SIZE      = 6;    // sizeof on-wire header
constexpr uint16_t MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;

// ---------------------------------------------------------------------------
// PacketReader — bounds-checked deserialization
// ---------------------------------------------------------------------------
class PacketReader {
public:
    PacketReader(const uint8_t* data, uint16_t size);

    bool readU8(uint8_t& out);
    bool readU16(uint16_t& out);
    bool readU32(uint32_t& out);
    bool readF32(float& out);               // rejects NaN / Inf
    bool readBytes(uint8_t* out, uint16_t count);
    bool readString(char* out, uint16_t maxLen); // length-prefixed (u16)

    uint16_t remaining() const;
    bool     hasError() const;

private:
    const uint8_t* m_data;
    uint16_t       m_size;
    uint16_t       m_pos   = 0;
    bool           m_error = false;
};

// ---------------------------------------------------------------------------
// PacketWriter — bounds-checked serialization
// ---------------------------------------------------------------------------
class PacketWriter {
public:
    PacketWriter(uint8_t* buffer, uint16_t capacity);

    bool writeU8(uint8_t val);
    bool writeU16(uint16_t val);
    bool writeU32(uint32_t val);
    bool writeF32(float val);
    bool writeBytes(const uint8_t* data, uint16_t count);
    bool writeString(const char* str);       // length-prefixed (u16)

    uint16_t       bytesWritten() const;
    bool           hasError() const;
    const uint8_t* data() const;

private:
    uint8_t* m_buffer;
    uint16_t m_capacity;
    uint16_t m_pos   = 0;
    bool     m_error = false;
};

// ---------------------------------------------------------------------------
// Header helpers
// ---------------------------------------------------------------------------
bool writeHeader(PacketWriter& writer, const PacketHeader& header);
bool readHeader(PacketReader& reader, PacketHeader& header);

} // namespace ffe::networking
