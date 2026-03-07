// tests/networking/test_packet.cpp
//
// Unit tests for PacketReader, PacketWriter, header helpers, and validation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "networking/packet.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

using namespace ffe::networking;

// ===========================================================================
// Round-trip tests
// ===========================================================================

TEST_CASE("PacketWriter/Reader round-trip u8", "[networking][packet]") {
    std::array<uint8_t, 64> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(w.writeU8(0x00));
    REQUIRE(w.writeU8(0xFF));
    REQUIRE(w.writeU8(42));
    REQUIRE(w.bytesWritten() == 3);

    PacketReader r(buf.data(), w.bytesWritten());
    uint8_t a = 0, b = 0, c = 0;
    REQUIRE(r.readU8(a));
    REQUIRE(r.readU8(b));
    REQUIRE(r.readU8(c));
    CHECK(a == 0x00);
    CHECK(b == 0xFF);
    CHECK(c == 42);
    CHECK(r.remaining() == 0);
}

TEST_CASE("PacketWriter/Reader round-trip u16", "[networking][packet]") {
    std::array<uint8_t, 64> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(w.writeU16(0));
    REQUIRE(w.writeU16(0xFFFF));
    REQUIRE(w.writeU16(1234));

    PacketReader r(buf.data(), w.bytesWritten());
    uint16_t a = 0, b = 0, c = 0;
    REQUIRE(r.readU16(a));
    REQUIRE(r.readU16(b));
    REQUIRE(r.readU16(c));
    CHECK(a == 0);
    CHECK(b == 0xFFFF);
    CHECK(c == 1234);
}

TEST_CASE("PacketWriter/Reader round-trip u32", "[networking][packet]") {
    std::array<uint8_t, 64> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(w.writeU32(0));
    REQUIRE(w.writeU32(0xDEADBEEF));

    PacketReader r(buf.data(), w.bytesWritten());
    uint32_t a = 0, b = 0;
    REQUIRE(r.readU32(a));
    REQUIRE(r.readU32(b));
    CHECK(a == 0);
    CHECK(b == 0xDEADBEEF);
}

TEST_CASE("PacketWriter/Reader round-trip f32", "[networking][packet]") {
    std::array<uint8_t, 64> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(w.writeF32(0.0f));
    REQUIRE(w.writeF32(-3.14f));
    REQUIRE(w.writeF32(1.0e10f));

    PacketReader r(buf.data(), w.bytesWritten());
    float a = 0, b = 0, c = 0;
    REQUIRE(r.readF32(a));
    REQUIRE(r.readF32(b));
    REQUIRE(r.readF32(c));
    CHECK(a == 0.0f);
    CHECK_THAT(b, Catch::Matchers::WithinRel(-3.14f, 1.0e-5f));
    CHECK(c == 1.0e10f);
}

TEST_CASE("PacketWriter/Reader round-trip bytes", "[networking][packet]") {
    std::array<uint8_t, 64> buf{};
    const uint8_t payload[] = {1, 2, 3, 4, 5};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(w.writeBytes(payload, 5));

    PacketReader r(buf.data(), w.bytesWritten());
    uint8_t out[5] = {};
    REQUIRE(r.readBytes(out, 5));
    CHECK(std::memcmp(out, payload, 5) == 0);
}

TEST_CASE("PacketWriter/Reader round-trip string", "[networking][packet]") {
    std::array<uint8_t, 64> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(w.writeString("hello"));

    PacketReader r(buf.data(), w.bytesWritten());
    char out[32] = {};
    REQUIRE(r.readString(out, 32));
    CHECK(std::strcmp(out, "hello") == 0);
}

TEST_CASE("PacketWriter/Reader round-trip empty string", "[networking][packet]") {
    std::array<uint8_t, 64> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(w.writeString(""));

    PacketReader r(buf.data(), w.bytesWritten());
    char out[8] = {'x'};
    REQUIRE(r.readString(out, 8));
    CHECK(out[0] == '\0');
}

// ===========================================================================
// Bounds checking — reader
// ===========================================================================

TEST_CASE("PacketReader rejects read beyond end", "[networking][packet]") {
    const uint8_t data[] = {0xAA, 0xBB};
    PacketReader r(data, 2);

    uint8_t v8 = 0;
    REQUIRE(r.readU8(v8));  // consume 1 byte
    CHECK(v8 == 0xAA);

    uint16_t v16 = 0;
    CHECK_FALSE(r.readU16(v16)); // need 2 bytes, only 1 remaining
    CHECK(r.hasError());

    // Once in error, all further reads fail
    CHECK_FALSE(r.readU8(v8));
}

TEST_CASE("PacketReader handles zero-length data", "[networking][packet]") {
    PacketReader r(nullptr, 0);
    CHECK(r.remaining() == 0);

    uint8_t v = 0;
    CHECK_FALSE(r.readU8(v));
    CHECK(r.hasError());
}

TEST_CASE("PacketReader readString rejects when buffer too small", "[networking][packet]") {
    // Write a 10-char string
    std::array<uint8_t, 64> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(w.writeString("0123456789")); // 10 chars

    // Try to read into a 5-char buffer (too small: need 10+1 for null)
    PacketReader r(buf.data(), w.bytesWritten());
    char out[5] = {};
    CHECK_FALSE(r.readString(out, 5));
    CHECK(r.hasError());
}

// ===========================================================================
// Bounds checking — writer
// ===========================================================================

TEST_CASE("PacketWriter rejects write beyond capacity", "[networking][packet]") {
    std::array<uint8_t, 3> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));

    REQUIRE(w.writeU8(1));   // 1 byte written
    REQUIRE(w.writeU16(2));  // 3 bytes total — at capacity

    CHECK_FALSE(w.writeU8(3)); // 4th byte — exceeds capacity
    CHECK(w.hasError());
    CHECK(w.bytesWritten() == 3);
}

TEST_CASE("PacketWriter writeString respects capacity", "[networking][packet]") {
    std::array<uint8_t, 4> buf{}; // 2 (length prefix) + 2 chars max
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));

    CHECK_FALSE(w.writeString("abc")); // needs 2 + 3 = 5 bytes
    CHECK(w.hasError());
}

// ===========================================================================
// NaN / Inf rejection
// ===========================================================================

TEST_CASE("PacketReader rejects NaN in readF32", "[networking][packet]") {
    std::array<uint8_t, 4> buf{};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(buf.data(), &nan, 4);

    PacketReader r(buf.data(), 4);
    float out = 0.0f;
    CHECK_FALSE(r.readF32(out));
    CHECK(r.hasError());
}

TEST_CASE("PacketReader rejects positive Inf in readF32", "[networking][packet]") {
    std::array<uint8_t, 4> buf{};
    const float inf = std::numeric_limits<float>::infinity();
    std::memcpy(buf.data(), &inf, 4);

    PacketReader r(buf.data(), 4);
    float out = 0.0f;
    CHECK_FALSE(r.readF32(out));
    CHECK(r.hasError());
}

TEST_CASE("PacketReader rejects negative Inf in readF32", "[networking][packet]") {
    std::array<uint8_t, 4> buf{};
    const float negInf = -std::numeric_limits<float>::infinity();
    std::memcpy(buf.data(), &negInf, 4);

    PacketReader r(buf.data(), 4);
    float out = 0.0f;
    CHECK_FALSE(r.readF32(out));
    CHECK(r.hasError());
}

// ===========================================================================
// Header round-trip
// ===========================================================================

TEST_CASE("Header write/read round-trip", "[networking][packet]") {
    std::array<uint8_t, 64> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));

    PacketHeader hdr;
    hdr.type          = PacketType::SNAPSHOT_DELTA;
    hdr.channel       = 1;
    hdr.sequence      = 9999;
    hdr.payloadLength = 42;

    REQUIRE(writeHeader(w, hdr));
    CHECK(w.bytesWritten() == HEADER_SIZE);

    PacketReader r(buf.data(), w.bytesWritten());
    PacketHeader out{};
    REQUIRE(readHeader(r, out));
    CHECK(out.type          == PacketType::SNAPSHOT_DELTA);
    CHECK(out.channel       == 1);
    CHECK(out.sequence      == 9999);
    CHECK(out.payloadLength == 42);
}

TEST_CASE("readHeader rejects unknown packet type", "[networking][packet]") {
    // Manually write an invalid type byte
    std::array<uint8_t, 6> buf{};
    buf[0] = 0x99; // invalid type
    buf[1] = 0;
    buf[2] = 0; buf[3] = 0;
    buf[4] = 0; buf[5] = 0;

    PacketReader r(buf.data(), 6);
    PacketHeader hdr{};
    CHECK_FALSE(readHeader(r, hdr));
}

// ===========================================================================
// isValidPacketType
// ===========================================================================

TEST_CASE("isValidPacketType accepts all known types", "[networking][packet]") {
    CHECK(isValidPacketType(0x01)); // CONNECT_REQUEST
    CHECK(isValidPacketType(0x02)); // CONNECT_ACCEPT
    CHECK(isValidPacketType(0x03)); // CONNECT_REJECT
    CHECK(isValidPacketType(0x04)); // DISCONNECT
    CHECK(isValidPacketType(0x10)); // INPUT
    CHECK(isValidPacketType(0x11)); // SNAPSHOT_FULL
    CHECK(isValidPacketType(0x12)); // SNAPSHOT_DELTA
    CHECK(isValidPacketType(0x20)); // EVENT
    CHECK(isValidPacketType(0x21)); // RPC
    CHECK(isValidPacketType(0xF0)); // HEARTBEAT
}

TEST_CASE("isValidPacketType rejects unknown values", "[networking][packet]") {
    CHECK_FALSE(isValidPacketType(0x00));
    CHECK_FALSE(isValidPacketType(0x05));
    CHECK_FALSE(isValidPacketType(0x99));
    CHECK_FALSE(isValidPacketType(0xFF));
}

// ===========================================================================
// Empty and max-size packets
// ===========================================================================

TEST_CASE("Empty packet — zero-length reader", "[networking][packet]") {
    PacketReader r(nullptr, 0);
    uint8_t v = 0;
    CHECK_FALSE(r.readU8(v));
    CHECK(r.remaining() == 0);
}

TEST_CASE("Max-size packet round-trip", "[networking][packet]") {
    std::array<uint8_t, MAX_PACKET_SIZE> buf{};
    PacketWriter w(buf.data(), MAX_PACKET_SIZE);

    // Write header
    PacketHeader hdr;
    hdr.type          = PacketType::SNAPSHOT_FULL;
    hdr.channel       = 0;
    hdr.sequence      = 1;
    hdr.payloadLength = MAX_PAYLOAD_SIZE;
    REQUIRE(writeHeader(w, hdr));

    // Fill the rest with payload bytes
    for (uint16_t i = 0; i < MAX_PAYLOAD_SIZE; ++i) {
        REQUIRE(w.writeU8(static_cast<uint8_t>(i & 0xFF)));
    }
    CHECK(w.bytesWritten() == MAX_PACKET_SIZE);

    // One more byte should fail
    CHECK_FALSE(w.writeU8(0));
    CHECK(w.hasError());

    // Read it all back
    PacketReader r(buf.data(), MAX_PACKET_SIZE);
    PacketHeader outHdr{};
    REQUIRE(readHeader(r, outHdr));
    CHECK(outHdr.payloadLength == MAX_PAYLOAD_SIZE);

    for (uint16_t i = 0; i < MAX_PAYLOAD_SIZE; ++i) {
        uint8_t v = 0;
        REQUIRE(r.readU8(v));
        CHECK(v == static_cast<uint8_t>(i & 0xFF));
    }
    CHECK(r.remaining() == 0);
}

// ===========================================================================
// Zero-length bytes read/write
// ===========================================================================

TEST_CASE("PacketWriter/Reader zero-length bytes is no-op", "[networking][packet]") {
    std::array<uint8_t, 8> buf{};
    PacketWriter w(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(w.writeBytes(nullptr, 0));
    CHECK(w.bytesWritten() == 0);

    PacketReader r(buf.data(), 0);
    CHECK(r.readBytes(nullptr, 0));
}
