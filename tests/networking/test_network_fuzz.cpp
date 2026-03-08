// tests/networking/test_network_fuzz.cpp
//
// Fuzz-style tests for the networking subsystem.  Exercises edge cases:
// malformed packets, oversized payloads, NaN injection, rapid connect/
// disconnect, duplicate and out-of-order packets, and connection limits.
//
// All tests are deterministic (no true randomness) and run headless.

#include <catch2/catch_test_macros.hpp>

#include "networking/connection.h"
#include "networking/lobby.h"
#include "networking/packet.h"
#include "networking/replication.h"
#include "networking/server.h"
#include "networking/client.h"
#include "networking/transport.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

using namespace ffe::networking;

// ===========================================================================
// Helper: poll server and client for a brief period
// ===========================================================================
static void pollBoth(NetworkServer& server, NetworkClient& client,
                     const int iterations = 30, const int delayMs = 10) {
    const float dt = static_cast<float>(delayMs) * 0.001f;
    for (int i = 0; i < iterations; ++i) {
        server.update(dt);
        client.update(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

// ===========================================================================
// 1. Malformed packets — invalid headers, wrong magic bytes, truncated data
// ===========================================================================

TEST_CASE("Fuzz: readHeader rejects all-zero buffer", "[networking][fuzz]") {
    std::array<uint8_t, HEADER_SIZE> buf{};
    PacketReader reader(buf.data(), HEADER_SIZE);
    PacketHeader header{};
    // Type byte 0x00 is not a valid PacketType
    CHECK_FALSE(readHeader(reader, header));
}

TEST_CASE("Fuzz: readHeader rejects every invalid type byte", "[networking][fuzz]") {
    // Walk all 256 values; only valid PacketType values should parse
    for (uint16_t rawType = 0; rawType < 256; ++rawType) {
        std::array<uint8_t, HEADER_SIZE> buf{};
        buf[0] = static_cast<uint8_t>(rawType);
        // Fill remaining header bytes with valid-ish data
        buf[1] = 0; // channel
        buf[2] = 0; buf[3] = 0; // sequence
        buf[4] = 0; buf[5] = 0; // payloadLength

        PacketReader reader(buf.data(), HEADER_SIZE);
        PacketHeader header{};
        const bool parsed = readHeader(reader, header);

        if (isValidPacketType(static_cast<uint8_t>(rawType))) {
            CHECK(parsed);
        } else {
            CHECK_FALSE(parsed);
        }
    }
}

TEST_CASE("Fuzz: truncated header — fewer than 6 bytes", "[networking][fuzz]") {
    // Try every possible truncation of the 6-byte header
    for (uint16_t len = 0; len < HEADER_SIZE; ++len) {
        std::array<uint8_t, HEADER_SIZE> buf{};
        buf[0] = static_cast<uint8_t>(PacketType::HEARTBEAT);
        PacketReader reader(buf.data(), len);
        PacketHeader header{};
        CHECK_FALSE(readHeader(reader, header));
    }
}

TEST_CASE("Fuzz: header claims payload but buffer is too short", "[networking][fuzz]") {
    // Write a valid header saying payloadLength=100, but only supply the header
    std::array<uint8_t, HEADER_SIZE> buf{};
    PacketWriter writer(buf.data(), HEADER_SIZE);
    PacketHeader hdr{};
    hdr.type          = PacketType::SNAPSHOT_FULL;
    hdr.channel       = 0;
    hdr.sequence      = 1;
    hdr.payloadLength = 100;
    REQUIRE(writeHeader(writer, hdr));

    // Read header succeeds (it just reads 6 bytes)
    PacketReader reader(buf.data(), HEADER_SIZE);
    PacketHeader outHdr{};
    REQUIRE(readHeader(reader, outHdr));
    CHECK(outHdr.payloadLength == 100);

    // But there are no payload bytes to read
    uint8_t dummy = 0;
    CHECK_FALSE(reader.readU8(dummy));
    CHECK(reader.hasError());
}

TEST_CASE("Fuzz: garbage bytes in header positions", "[networking][fuzz]") {
    // Fill with 0xFF — type byte 0xFF is not a valid PacketType
    std::array<uint8_t, HEADER_SIZE> buf{};
    std::memset(buf.data(), 0xFF, buf.size());

    PacketReader reader(buf.data(), HEADER_SIZE);
    PacketHeader header{};
    CHECK_FALSE(readHeader(reader, header));
}

// ===========================================================================
// 2. Oversized payloads — exceeding MAX_PACKET_SIZE
// ===========================================================================

TEST_CASE("Fuzz: PacketWriter rejects writes beyond MAX_PACKET_SIZE", "[networking][fuzz]") {
    std::array<uint8_t, MAX_PACKET_SIZE> buf{};
    PacketWriter writer(buf.data(), MAX_PACKET_SIZE);

    // Fill to capacity
    for (uint16_t i = 0; i < MAX_PACKET_SIZE; ++i) {
        REQUIRE(writer.writeU8(static_cast<uint8_t>(i & 0xFF)));
    }
    CHECK(writer.bytesWritten() == MAX_PACKET_SIZE);
    CHECK_FALSE(writer.hasError());

    // One more byte should fail
    CHECK_FALSE(writer.writeU8(0));
    CHECK(writer.hasError());
}

TEST_CASE("Fuzz: payloadLength field exceeds MAX_PAYLOAD_SIZE", "[networking][fuzz]") {
    // Construct a header with payloadLength = 0xFFFF (65535), far exceeding
    // MAX_PAYLOAD_SIZE (1194).  The header itself parses fine — it is the
    // reader's responsibility to bounds-check payload reads.
    std::array<uint8_t, HEADER_SIZE + 4> buf{};
    PacketWriter writer(buf.data(), static_cast<uint16_t>(buf.size()));
    PacketHeader hdr{};
    hdr.type          = PacketType::EVENT;
    hdr.channel       = 0;
    hdr.sequence      = 0;
    hdr.payloadLength = 0xFFFF;
    REQUIRE(writeHeader(writer, hdr));

    PacketReader reader(buf.data(), static_cast<uint16_t>(buf.size()));
    PacketHeader out{};
    REQUIRE(readHeader(reader, out));
    CHECK(out.payloadLength == 0xFFFF);

    // Attempting to read payloadLength bytes will fail — only 4 bytes remain
    uint8_t payload[MAX_PACKET_SIZE]{};
    CHECK_FALSE(reader.readBytes(payload, out.payloadLength));
    CHECK(reader.hasError());
}

// ===========================================================================
// 3. Zero-length packets — empty data
// ===========================================================================

TEST_CASE("Fuzz: PacketReader with nullptr and zero size", "[networking][fuzz]") {
    PacketReader reader(nullptr, 0);
    CHECK(reader.remaining() == 0);
    CHECK_FALSE(reader.hasError()); // No error until a read is attempted

    uint8_t v = 0;
    CHECK_FALSE(reader.readU8(v));
    CHECK(reader.hasError());

    // Subsequent reads also fail
    uint16_t v16 = 0;
    CHECK_FALSE(reader.readU16(v16));
    uint32_t v32 = 0;
    CHECK_FALSE(reader.readU32(v32));
    float f = 0.0f;
    CHECK_FALSE(reader.readF32(f));
}

TEST_CASE("Fuzz: PacketWriter with zero capacity", "[networking][fuzz]") {
    uint8_t dummy = 0;
    PacketWriter writer(&dummy, 0);

    CHECK_FALSE(writer.writeU8(0));
    CHECK(writer.hasError());
    CHECK(writer.bytesWritten() == 0);
}

TEST_CASE("Fuzz: readHeader on zero-length buffer", "[networking][fuzz]") {
    PacketReader reader(nullptr, 0);
    PacketHeader header{};
    CHECK_FALSE(readHeader(reader, header));
}

TEST_CASE("Fuzz: readBytes with zero count succeeds", "[networking][fuzz]") {
    // Zero-length read should be a no-op, not an error
    std::array<uint8_t, 4> buf{1, 2, 3, 4};
    PacketReader reader(buf.data(), static_cast<uint16_t>(buf.size()));
    CHECK(reader.readBytes(nullptr, 0));
    CHECK_FALSE(reader.hasError());
    CHECK(reader.remaining() == 4); // No bytes consumed
}

// ===========================================================================
// 4. Integer overflow in length fields — length claims huge size
// ===========================================================================

TEST_CASE("Fuzz: readString with length prefix claiming 65535 bytes", "[networking][fuzz]") {
    // Manually write a string length prefix of 0xFFFF but only 2 bytes of data
    std::array<uint8_t, 4> buf{};
    buf[0] = 0xFF; buf[1] = 0xFF; // length prefix = 65535 (little-endian)
    buf[2] = 'A';  buf[3] = 'B';

    PacketReader reader(buf.data(), static_cast<uint16_t>(buf.size()));
    char out[64]{};
    CHECK_FALSE(reader.readString(out, 64));
    CHECK(reader.hasError());
}

TEST_CASE("Fuzz: readString with length exceeding output buffer", "[networking][fuzz]") {
    // Write a valid 20-char string, try to read into a 10-char buffer
    std::array<uint8_t, 64> buf{};
    PacketWriter writer(buf.data(), static_cast<uint16_t>(buf.size()));
    REQUIRE(writer.writeString("01234567890123456789")); // 20 chars

    PacketReader reader(buf.data(), writer.bytesWritten());
    char out[10]{};
    CHECK_FALSE(reader.readString(out, 10));
    CHECK(reader.hasError());
}

TEST_CASE("Fuzz: readU32 on 3-byte buffer — boundary underflow", "[networking][fuzz]") {
    std::array<uint8_t, 3> buf{0xAA, 0xBB, 0xCC};
    PacketReader reader(buf.data(), 3);
    uint32_t val = 0;
    CHECK_FALSE(reader.readU32(val));
    CHECK(reader.hasError());
}

TEST_CASE("Fuzz: readU16 on 1-byte buffer", "[networking][fuzz]") {
    uint8_t buf = 0xAA;
    PacketReader reader(&buf, 1);
    uint16_t val = 0;
    CHECK_FALSE(reader.readU16(val));
    CHECK(reader.hasError());
}

// ===========================================================================
// 5. Rapid connect/disconnect — 100 cycles on loopback
// ===========================================================================

TEST_CASE("Fuzz: rapid connect/disconnect 100 times", "[networking][fuzz]") {
    REQUIRE(initNetworking());

    NetworkServer server;
    ServerConfig config;
    config.port       = 19900;
    config.maxClients = 4;
    REQUIRE(server.start(config));

    // Rapidly connect and disconnect 100 times.  The intent is to stress
    // the transport's connection lifecycle without crashing or leaking.
    for (int i = 0; i < 100; ++i) {
        NetworkClient client;
        // connect() may return false if ENet can't keep up — that is acceptable
        client.connect("127.0.0.1", 19900);
        // Tiny poll to let the transport process
        server.update(0.001f);
        client.update(0.001f);
        client.disconnect();
    }

    // Server should still be healthy
    CHECK(server.isRunning());

    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// 6. NaN injection — float fields containing NaN, Inf, -Inf
// ===========================================================================

TEST_CASE("Fuzz: readF32 rejects quiet NaN", "[networking][fuzz]") {
    const float val = std::numeric_limits<float>::quiet_NaN();
    std::array<uint8_t, 4> buf{};
    std::memcpy(buf.data(), &val, 4);

    PacketReader reader(buf.data(), 4);
    float out = 0.0f;
    CHECK_FALSE(reader.readF32(out));
    CHECK(reader.hasError());
}

TEST_CASE("Fuzz: readF32 rejects signaling NaN", "[networking][fuzz]") {
    const float val = std::numeric_limits<float>::signaling_NaN();
    std::array<uint8_t, 4> buf{};
    std::memcpy(buf.data(), &val, 4);

    PacketReader reader(buf.data(), 4);
    float out = 0.0f;
    CHECK_FALSE(reader.readF32(out));
    CHECK(reader.hasError());
}

TEST_CASE("Fuzz: readF32 rejects positive infinity", "[networking][fuzz]") {
    const float val = std::numeric_limits<float>::infinity();
    std::array<uint8_t, 4> buf{};
    std::memcpy(buf.data(), &val, 4);

    PacketReader reader(buf.data(), 4);
    float out = 0.0f;
    CHECK_FALSE(reader.readF32(out));
    CHECK(reader.hasError());
}

TEST_CASE("Fuzz: readF32 rejects negative infinity", "[networking][fuzz]") {
    const float val = -std::numeric_limits<float>::infinity();
    std::array<uint8_t, 4> buf{};
    std::memcpy(buf.data(), &val, 4);

    PacketReader reader(buf.data(), 4);
    float out = 0.0f;
    CHECK_FALSE(reader.readF32(out));
    CHECK(reader.hasError());
}

TEST_CASE("Fuzz: multiple NaN/Inf values in sequence poison the reader", "[networking][fuzz]") {
    std::array<uint8_t, 16> buf{};
    const float nan  = std::numeric_limits<float>::quiet_NaN();
    const float inf  = std::numeric_limits<float>::infinity();
    const float ninf = -std::numeric_limits<float>::infinity();
    const float ok   = 1.0f;

    std::memcpy(buf.data() + 0,  &nan, 4);
    std::memcpy(buf.data() + 4,  &inf, 4);
    std::memcpy(buf.data() + 8,  &ninf, 4);
    std::memcpy(buf.data() + 12, &ok, 4);

    PacketReader reader(buf.data(), 16);
    float out = 0.0f;

    // First NaN poisons the reader — all subsequent reads fail too
    CHECK_FALSE(reader.readF32(out));
    CHECK(reader.hasError());
    CHECK_FALSE(reader.readF32(out));
    CHECK_FALSE(reader.readF32(out));
    CHECK_FALSE(reader.readF32(out)); // Even the valid 1.0f cannot be read
}

TEST_CASE("Fuzz: NaN injection in writeF32 then readF32 round-trip", "[networking][fuzz]") {
    // writeF32 does NOT reject NaN (it writes raw bytes), but readF32 does
    std::array<uint8_t, 4> buf{};
    PacketWriter writer(buf.data(), 4);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(writer.writeF32(nan)); // write succeeds

    PacketReader reader(buf.data(), 4);
    float out = 0.0f;
    CHECK_FALSE(reader.readF32(out)); // read rejects NaN
    CHECK(reader.hasError());
}

// ===========================================================================
// 7. Negative / extreme sequence numbers
// ===========================================================================
// The protocol uses uint16_t for sequence numbers (0..65535).
// There are no "negative" sequence numbers per se, but we test boundary
// values and wraparound behavior.

TEST_CASE("Fuzz: header with sequence number 0 round-trips", "[networking][fuzz]") {
    std::array<uint8_t, HEADER_SIZE> buf{};
    PacketWriter writer(buf.data(), HEADER_SIZE);
    PacketHeader hdr{};
    hdr.type     = PacketType::HEARTBEAT;
    hdr.sequence = 0;
    REQUIRE(writeHeader(writer, hdr));

    PacketReader reader(buf.data(), HEADER_SIZE);
    PacketHeader out{};
    REQUIRE(readHeader(reader, out));
    CHECK(out.sequence == 0);
}

TEST_CASE("Fuzz: header with sequence number 0xFFFF round-trips", "[networking][fuzz]") {
    std::array<uint8_t, HEADER_SIZE> buf{};
    PacketWriter writer(buf.data(), HEADER_SIZE);
    PacketHeader hdr{};
    hdr.type     = PacketType::HEARTBEAT;
    hdr.sequence = 0xFFFF;
    REQUIRE(writeHeader(writer, hdr));

    PacketReader reader(buf.data(), HEADER_SIZE);
    PacketHeader out{};
    REQUIRE(readHeader(reader, out));
    CHECK(out.sequence == 0xFFFF);
}

TEST_CASE("Fuzz: sequence number wraps around 0xFFFF to 0x0000", "[networking][fuzz]") {
    // Simulate server tick wrapping: the protocol truncates tick to uint16_t
    uint32_t serverTick = 0xFFFF;
    const auto seq = static_cast<uint16_t>(serverTick & 0xFFFF);
    CHECK(seq == 0xFFFF);

    ++serverTick;
    const auto nextSeq = static_cast<uint16_t>(serverTick & 0xFFFF);
    CHECK(nextSeq == 0x0000);
}

// ===========================================================================
// 8. Duplicate packets — same packet parsed twice
// ===========================================================================

TEST_CASE("Fuzz: reading the same buffer twice produces identical results", "[networking][fuzz]") {
    std::array<uint8_t, 64> buf{};
    PacketWriter writer(buf.data(), static_cast<uint16_t>(buf.size()));

    PacketHeader hdr{};
    hdr.type          = PacketType::INPUT;
    hdr.channel       = 0;
    hdr.sequence      = 42;
    hdr.payloadLength = 4;
    REQUIRE(writeHeader(writer, hdr));
    REQUIRE(writer.writeU32(0xDEADBEEF));

    // First read
    PacketReader r1(buf.data(), writer.bytesWritten());
    PacketHeader h1{};
    REQUIRE(readHeader(r1, h1));
    uint32_t v1 = 0;
    REQUIRE(r1.readU32(v1));

    // Second read — same buffer, fresh reader
    PacketReader r2(buf.data(), writer.bytesWritten());
    PacketHeader h2{};
    REQUIRE(readHeader(r2, h2));
    uint32_t v2 = 0;
    REQUIRE(r2.readU32(v2));

    CHECK(h1.type     == h2.type);
    CHECK(h1.sequence == h2.sequence);
    CHECK(v1 == v2);
    CHECK(v1 == 0xDEADBEEF);
}

TEST_CASE("Fuzz: snapshot buffer handles duplicate ticks", "[networking][fuzz]") {
    SnapshotBuffer snapBuf;

    Snapshot snap{};
    snap.tick        = 10;
    snap.entityCount = 0;

    // Push the same snapshot twice
    snapBuf.push(snap);
    snapBuf.push(snap);

    CHECK(snapBuf.count() == 2);
    const Snapshot* latest = snapBuf.latest();
    REQUIRE(latest != nullptr);
    CHECK(latest->tick == 10);
}

// ===========================================================================
// 9. Out-of-order packets — sequence 3 before sequence 2
// ===========================================================================

TEST_CASE("Fuzz: snapshot buffer accepts out-of-order ticks", "[networking][fuzz]") {
    SnapshotBuffer snapBuf;

    // Push ticks in wrong order: 1, 3, 2
    Snapshot snap1{};
    snap1.tick = 1;
    snapBuf.push(snap1);

    Snapshot snap3{};
    snap3.tick = 3;
    snapBuf.push(snap3);

    Snapshot snap2{};
    snap2.tick = 2;
    snapBuf.push(snap2);

    CHECK(snapBuf.count() == 3);
    // Latest is the most recently pushed, which is tick 2
    const Snapshot* latest = snapBuf.latest();
    REQUIRE(latest != nullptr);
    CHECK(latest->tick == 2);
}

TEST_CASE("Fuzz: out-of-order headers parse correctly regardless of sequence", "[networking][fuzz]") {
    // Parse packet with seq=100, then seq=1 — both should succeed
    for (const uint16_t seq : {100, 1, 50000, 0, 65535}) {
        std::array<uint8_t, HEADER_SIZE> buf{};
        PacketWriter writer(buf.data(), HEADER_SIZE);
        PacketHeader hdr{};
        hdr.type     = PacketType::SNAPSHOT_FULL;
        hdr.sequence = seq;
        REQUIRE(writeHeader(writer, hdr));

        PacketReader reader(buf.data(), HEADER_SIZE);
        PacketHeader out{};
        REQUIRE(readHeader(reader, out));
        CHECK(out.sequence == seq);
    }
}

// ===========================================================================
// 10. Maximum clients — exceed MAX_CLIENTS
// ===========================================================================

TEST_CASE("Fuzz: server handles max client connections", "[networking][fuzz]") {
    REQUIRE(initNetworking());

    NetworkServer server;
    ServerConfig config;
    config.port       = 19901;
    config.maxClients = 2; // Deliberately small limit

    uint32_t connectCount = 0;
    server.setClientConnectCallback([](uint32_t /*id*/, void* ud) {
        ++(*static_cast<uint32_t*>(ud));
    }, &connectCount);

    REQUIRE(server.start(config));

    // Create 4 clients, but only 2 should be accepted
    NetworkClient clients[4];
    for (auto& client : clients) {
        client.connect("127.0.0.1", 19901);
    }

    // Poll to let connections establish
    const float dt = 0.01f;
    for (int i = 0; i < 60; ++i) {
        server.update(dt);
        for (auto& client : clients) {
            client.update(dt);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Server should not exceed its max
    CHECK(server.clientCount() <= 2);

    for (auto& client : clients) {
        client.disconnect();
    }
    server.stop();
    shutdownNetworking();
}

// ===========================================================================
// Additional edge cases — rate limiter, connection state, reader exhaustion
// ===========================================================================

TEST_CASE("Fuzz: ConnectionRateLimit blocks after MAX_PACKETS_PER_SECOND", "[networking][fuzz]") {
    ConnectionRateLimit limiter{};

    // Send MAX_PACKETS_PER_SECOND packets — all should be allowed
    for (uint32_t i = 0; i < ConnectionRateLimit::MAX_PACKETS_PER_SECOND; ++i) {
        CHECK(limiter.allowPacket(10, 0.0f));
    }
    // Next packet should be blocked
    CHECK_FALSE(limiter.allowPacket(10, 0.0f));

    // After 1 second, window resets
    CHECK(limiter.allowPacket(10, 1.0f));
}

TEST_CASE("Fuzz: ConnectionRateLimit blocks after MAX_BYTES_PER_SECOND", "[networking][fuzz]") {
    ConnectionRateLimit limiter{};

    // Send one giant packet that uses all the byte budget
    CHECK(limiter.allowPacket(ConnectionRateLimit::MAX_BYTES_PER_SECOND, 0.0f));

    // Even a 1-byte packet should now be blocked (bytes exhausted)
    CHECK_FALSE(limiter.allowPacket(1, 0.0f));

    // After window rolls over
    CHECK(limiter.allowPacket(1, 1.0f));
}

TEST_CASE("Fuzz: reader error state is sticky — no recovery", "[networking][fuzz]") {
    std::array<uint8_t, 2> buf{0xAA, 0xBB};
    PacketReader reader(buf.data(), 2);

    // Force an error by reading more than available
    uint32_t val = 0;
    CHECK_FALSE(reader.readU32(val));
    CHECK(reader.hasError());

    // Even a valid 1-byte read now fails
    uint8_t v8 = 0;
    CHECK_FALSE(reader.readU8(v8));
    CHECK(reader.hasError());
}

TEST_CASE("Fuzz: writer error state is sticky — no recovery", "[networking][fuzz]") {
    std::array<uint8_t, 1> buf{};
    PacketWriter writer(buf.data(), 1);

    REQUIRE(writer.writeU8(0xFF)); // fills capacity
    CHECK_FALSE(writer.writeU8(0));
    CHECK(writer.hasError());

    // Even writeBytes(nullptr, 0) fails once error flag is set
    // (because the writer checks m_error first)
    // Note: writeBytes with count=0 returns !m_error, so it returns false
    CHECK_FALSE(writer.writeBytes(nullptr, 0));
}

TEST_CASE("Fuzz: isValidPacketType rejects gaps between valid ranges", "[networking][fuzz]") {
    // Valid types are: 0x01-0x04, 0x10-0x12, 0x20-0x21, 0x30-0x35, 0x40, 0xF0
    // Check some values in the gaps
    const uint8_t gapValues[] = {
        0x00, 0x05, 0x06, 0x0F,
        0x13, 0x14, 0x1F,
        0x22, 0x23, 0x2F,
        0x36, 0x37, 0x3F,
        0x41, 0x42, 0x80,
        0xEF, 0xF1, 0xFE, 0xFF
    };
    for (const uint8_t v : gapValues) {
        CHECK_FALSE(isValidPacketType(v));
    }
}

TEST_CASE("Fuzz: ConnectionId validity check", "[networking][fuzz]") {
    ConnectionId invalid{};
    CHECK_FALSE(isValid(invalid)); // default is 0xFFFFFFFF

    ConnectionId zero{0};
    CHECK(isValid(zero));

    ConnectionId max{0xFFFFFFFE};
    CHECK(isValid(max));

    ConnectionId sentinel{0xFFFFFFFF};
    CHECK_FALSE(isValid(sentinel));
}

TEST_CASE("Fuzz: writeHeader then readHeader with all valid PacketTypes", "[networking][fuzz]") {
    const PacketType validTypes[] = {
        PacketType::CONNECT_REQUEST,  PacketType::CONNECT_ACCEPT,
        PacketType::CONNECT_REJECT,   PacketType::DISCONNECT,
        PacketType::INPUT,            PacketType::SNAPSHOT_FULL,
        PacketType::SNAPSHOT_DELTA,   PacketType::EVENT,
        PacketType::RPC,              PacketType::LOBBY_CREATE,
        PacketType::LOBBY_JOIN,       PacketType::LOBBY_LEAVE,
        PacketType::LOBBY_READY,      PacketType::LOBBY_STATE,
        PacketType::LOBBY_GAME_START, PacketType::HIT_CHECK,
        PacketType::HEARTBEAT
    };

    for (const PacketType type : validTypes) {
        std::array<uint8_t, HEADER_SIZE> buf{};
        PacketWriter writer(buf.data(), HEADER_SIZE);
        PacketHeader hdr{};
        hdr.type          = type;
        hdr.channel       = 2;
        hdr.sequence      = 12345;
        hdr.payloadLength = 100;
        REQUIRE(writeHeader(writer, hdr));

        PacketReader reader(buf.data(), HEADER_SIZE);
        PacketHeader out{};
        REQUIRE(readHeader(reader, out));
        CHECK(out.type          == type);
        CHECK(out.channel       == 2);
        CHECK(out.sequence      == 12345);
        CHECK(out.payloadLength == 100);
    }
}

TEST_CASE("Fuzz: snapshot buffer fills to capacity without crash", "[networking][fuzz]") {
    SnapshotBuffer snapBuf;

    // Push more snapshots than the ring buffer holds (SNAPSHOT_BUFFER_SIZE = 16)
    for (uint32_t i = 0; i < SNAPSHOT_BUFFER_SIZE * 3; ++i) {
        Snapshot snap{};
        snap.tick        = i;
        snap.entityCount = 0;
        snapBuf.push(snap);
    }

    // Count should be capped at SNAPSHOT_BUFFER_SIZE
    CHECK(snapBuf.count() == SNAPSHOT_BUFFER_SIZE);

    // Latest should be the most recently pushed
    const Snapshot* latest = snapBuf.latest();
    REQUIRE(latest != nullptr);
    CHECK(latest->tick == SNAPSHOT_BUFFER_SIZE * 3 - 1);
}
