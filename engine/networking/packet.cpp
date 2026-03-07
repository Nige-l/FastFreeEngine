// engine/networking/packet.cpp

#include "networking/packet.h"

#include <cmath>    // std::isfinite
#include <cstring>  // std::memcpy, std::strlen

namespace ffe::networking {

// ---------------------------------------------------------------------------
// isValidPacketType
// ---------------------------------------------------------------------------
bool isValidPacketType(const uint8_t t) {
    switch (static_cast<PacketType>(t)) {
    case PacketType::CONNECT_REQUEST:
    case PacketType::CONNECT_ACCEPT:
    case PacketType::CONNECT_REJECT:
    case PacketType::DISCONNECT:
    case PacketType::INPUT:
    case PacketType::SNAPSHOT_FULL:
    case PacketType::SNAPSHOT_DELTA:
    case PacketType::EVENT:
    case PacketType::RPC:
    case PacketType::LOBBY_CREATE:
    case PacketType::LOBBY_JOIN:
    case PacketType::LOBBY_LEAVE:
    case PacketType::LOBBY_READY:
    case PacketType::LOBBY_STATE:
    case PacketType::LOBBY_GAME_START:
    case PacketType::HIT_CHECK:
    case PacketType::HEARTBEAT:
        return true;
    default:
        return false;
    }
}

// ===========================================================================
// PacketReader
// ===========================================================================

PacketReader::PacketReader(const uint8_t* data, const uint16_t size)
    : m_data(data), m_size(size) {}

bool PacketReader::readU8(uint8_t& out) {
    if (m_error || m_pos + 1u > m_size) {
        m_error = true;
        return false;
    }
    out = m_data[m_pos];
    m_pos += 1;
    return true;
}

bool PacketReader::readU16(uint16_t& out) {
    if (m_error || m_pos + 2u > m_size) {
        m_error = true;
        return false;
    }
    std::memcpy(&out, m_data + m_pos, 2);
    m_pos += 2;
    return true;
}

bool PacketReader::readU32(uint32_t& out) {
    if (m_error || m_pos + 4u > m_size) {
        m_error = true;
        return false;
    }
    std::memcpy(&out, m_data + m_pos, 4);
    m_pos += 4;
    return true;
}

bool PacketReader::readF32(float& out) {
    if (m_error || m_pos + 4u > m_size) {
        m_error = true;
        return false;
    }
    std::memcpy(&out, m_data + m_pos, 4);
    if (!std::isfinite(out)) {
        m_error = true;
        return false;
    }
    m_pos += 4;
    return true;
}

bool PacketReader::readBytes(uint8_t* out, const uint16_t count) {
    if (m_error || count == 0) {
        if (count != 0) { m_error = true; }
        return count == 0 && !m_error;
    }
    if (m_pos + count > m_size) {
        m_error = true;
        return false;
    }
    std::memcpy(out, m_data + m_pos, count);
    m_pos += count;
    return true;
}

bool PacketReader::readString(char* out, const uint16_t maxLen) {
    uint16_t len = 0;
    if (!readU16(len)) { return false; }

    if (len == 0) {
        if (maxLen > 0) { out[0] = '\0'; }
        return true;
    }

    // Need room for len chars + null terminator
    if (len >= maxLen) {
        m_error = true;
        return false;
    }
    if (m_pos + len > m_size) {
        m_error = true;
        return false;
    }

    std::memcpy(out, m_data + m_pos, len);
    out[len] = '\0';
    m_pos += len;
    return true;
}

uint16_t PacketReader::remaining() const {
    return (m_pos <= m_size) ? static_cast<uint16_t>(m_size - m_pos) : 0;
}

bool PacketReader::hasError() const { return m_error; }

// ===========================================================================
// PacketWriter
// ===========================================================================

PacketWriter::PacketWriter(uint8_t* buffer, const uint16_t capacity)
    : m_buffer(buffer), m_capacity(capacity) {}

bool PacketWriter::writeU8(const uint8_t val) {
    if (m_error || m_pos + 1u > m_capacity) {
        m_error = true;
        return false;
    }
    m_buffer[m_pos] = val;
    m_pos += 1;
    return true;
}

bool PacketWriter::writeU16(const uint16_t val) {
    if (m_error || m_pos + 2u > m_capacity) {
        m_error = true;
        return false;
    }
    std::memcpy(m_buffer + m_pos, &val, 2);
    m_pos += 2;
    return true;
}

bool PacketWriter::writeU32(const uint32_t val) {
    if (m_error || m_pos + 4u > m_capacity) {
        m_error = true;
        return false;
    }
    std::memcpy(m_buffer + m_pos, &val, 4);
    m_pos += 4;
    return true;
}

bool PacketWriter::writeF32(const float val) {
    if (m_error || m_pos + 4u > m_capacity) {
        m_error = true;
        return false;
    }
    std::memcpy(m_buffer + m_pos, &val, 4);
    m_pos += 4;
    return true;
}

bool PacketWriter::writeBytes(const uint8_t* data, const uint16_t count) {
    if (count == 0) { return !m_error; }
    if (m_error || m_pos + count > m_capacity) {
        m_error = true;
        return false;
    }
    std::memcpy(m_buffer + m_pos, data, count);
    m_pos += count;
    return true;
}

bool PacketWriter::writeString(const char* str) {
    const auto rawLen = std::strlen(str);
    if (rawLen > UINT16_MAX) {
        m_error = true;
        return false;
    }
    const auto len = static_cast<uint16_t>(rawLen);
    if (!writeU16(len)) { return false; }
    if (len == 0) { return true; }
    if (m_error || m_pos + len > m_capacity) {
        m_error = true;
        return false;
    }
    std::memcpy(m_buffer + m_pos, str, len);
    m_pos += len;
    return true;
}

uint16_t PacketWriter::bytesWritten() const { return m_pos; }
bool     PacketWriter::hasError() const { return m_error; }
const uint8_t* PacketWriter::data() const { return m_buffer; }

// ===========================================================================
// Header helpers
// ===========================================================================

bool writeHeader(PacketWriter& writer, const PacketHeader& header) {
    return writer.writeU8(static_cast<uint8_t>(header.type))
        && writer.writeU8(header.channel)
        && writer.writeU16(header.sequence)
        && writer.writeU16(header.payloadLength);
}

bool readHeader(PacketReader& reader, PacketHeader& header) {
    uint8_t rawType = 0;
    if (!reader.readU8(rawType))            { return false; }
    if (!isValidPacketType(rawType))        { return false; }
    header.type = static_cast<PacketType>(rawType);

    if (!reader.readU8(header.channel))     { return false; }
    if (!reader.readU16(header.sequence))   { return false; }
    if (!reader.readU16(header.payloadLength)) { return false; }
    return true;
}

} // namespace ffe::networking
