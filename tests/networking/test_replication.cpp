// tests/networking/test_replication.cpp
//
// Unit tests for the replication system: registry, serialisation round-trips,
// snapshot buffer, and interpolation correctness.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "networking/replication.h"
#include "renderer/render_system.h"

#include <array>
#include <cmath>
#include <cstring>

using namespace ffe::networking;
using Catch::Matchers::WithinAbs;

// ===========================================================================
// ReplicationRegistry tests
// ===========================================================================

TEST_CASE("ReplicationRegistry register and find", "[networking][replication]") {
    ReplicationRegistry reg;
    REQUIRE(reg.count() == 0);

    const bool ok = reg.registerComponent(
        42, 20, serializeTransform, deserializeTransform, interpolateTransform);
    REQUIRE(ok);
    REQUIRE(reg.count() == 1);

    const ReplicatedComponent* comp = reg.find(42);
    REQUIRE(comp != nullptr);
    REQUIRE(comp->componentId == 42);
    REQUIRE(comp->maxSerializedSize == 20);
    REQUIRE(comp->serialize == serializeTransform);
    REQUIRE(comp->deserialize == deserializeTransform);
    REQUIRE(comp->interpolate == interpolateTransform);

    // Not-found returns nullptr
    REQUIRE(reg.find(99) == nullptr);
}

TEST_CASE("ReplicationRegistry rejects duplicate componentId", "[networking][replication]") {
    ReplicationRegistry reg;
    REQUIRE(reg.registerComponent(1, 28, serializeTransform, deserializeTransform));
    REQUIRE_FALSE(reg.registerComponent(1, 40, serializeTransform3D, deserializeTransform3D));
    REQUIRE(reg.count() == 1);
}

TEST_CASE("ReplicationRegistry rejects when full", "[networking][replication]") {
    ReplicationRegistry reg;
    for (uint16_t i = 0; i < MAX_REPLICATED_COMPONENTS; ++i) {
        REQUIRE(reg.registerComponent(i, 10, serializeTransform, deserializeTransform));
    }
    REQUIRE(reg.count() == MAX_REPLICATED_COMPONENTS);
    REQUIRE_FALSE(reg.registerComponent(999, 10, serializeTransform, deserializeTransform));
}

TEST_CASE("ReplicationRegistry array access", "[networking][replication]") {
    ReplicationRegistry reg;
    reg.registerComponent(10, 28, serializeTransform, deserializeTransform);
    reg.registerComponent(20, 40, serializeTransform3D, deserializeTransform3D);

    const ReplicatedComponent* arr = reg.components();
    REQUIRE(arr[0].componentId == 10);
    REQUIRE(arr[1].componentId == 20);
}

TEST_CASE("registerDefaultComponents registers Transform and Transform3D",
          "[networking][replication]") {
    ReplicationRegistry reg;
    registerDefaultComponents(reg);
    REQUIRE(reg.count() == 2);
    REQUIRE(reg.find(COMPONENT_ID_TRANSFORM) != nullptr);
    REQUIRE(reg.find(COMPONENT_ID_TRANSFORM3D) != nullptr);
}

// ===========================================================================
// Transform serialization round-trip
// ===========================================================================

TEST_CASE("Transform serialize/deserialize round-trip", "[networking][replication]") {
    ffe::Transform original{};
    original.position = {1.5f, -2.3f, 0.0f};
    original.rotation = 1.57f;
    original.scale    = {2.0f, 3.0f, 1.0f};

    std::array<uint8_t, 64> buffer{};
    const uint16_t written = serializeTransform(&original, buffer.data(),
                                                 static_cast<uint16_t>(buffer.size()));
    REQUIRE(written == TRANSFORM_SERIALIZED_SIZE);

    ffe::Transform restored{};
    const bool ok = deserializeTransform(&restored, buffer.data(), written);
    REQUIRE(ok);

    REQUIRE_THAT(static_cast<double>(restored.position.x), WithinAbs(1.5, 1e-6));
    REQUIRE_THAT(static_cast<double>(restored.position.y), WithinAbs(-2.3, 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.position.z), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(static_cast<double>(restored.rotation), WithinAbs(1.57, 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.scale.x), WithinAbs(2.0, 1e-6));
    REQUIRE_THAT(static_cast<double>(restored.scale.y), WithinAbs(3.0, 1e-6));
    REQUIRE_THAT(static_cast<double>(restored.scale.z), WithinAbs(1.0, 1e-6));
}

TEST_CASE("Transform serialize fails with insufficient buffer", "[networking][replication]") {
    ffe::Transform tx{};
    std::array<uint8_t, 10> small{};
    const uint16_t written = serializeTransform(&tx, small.data(),
                                                 static_cast<uint16_t>(small.size()));
    REQUIRE(written == 0);
}

TEST_CASE("Transform deserialize fails with insufficient data", "[networking][replication]") {
    ffe::Transform tx{};
    std::array<uint8_t, 10> small{};
    REQUIRE_FALSE(deserializeTransform(&tx, small.data(),
                                        static_cast<uint16_t>(small.size())));
}

// ===========================================================================
// Transform3D serialization round-trip
// ===========================================================================

TEST_CASE("Transform3D serialize/deserialize round-trip", "[networking][replication]") {
    ffe::Transform3D original{};
    original.position = {10.0f, -5.0f, 3.0f};
    // 90-degree rotation around Y axis
    const float halfAngle = 3.14159265f / 4.0f; // pi/4
    original.rotation = glm::quat(std::cos(halfAngle), 0.0f, std::sin(halfAngle), 0.0f);
    original.scale = {1.5f, 2.0f, 0.5f};

    std::array<uint8_t, 64> buffer{};
    const uint16_t written = serializeTransform3D(&original, buffer.data(),
                                                    static_cast<uint16_t>(buffer.size()));
    REQUIRE(written == TRANSFORM3D_SERIALIZED_SIZE);

    ffe::Transform3D restored{};
    const bool ok = deserializeTransform3D(&restored, buffer.data(), written);
    REQUIRE(ok);

    REQUIRE_THAT(static_cast<double>(restored.position.x), WithinAbs(10.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.position.y), WithinAbs(-5.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.position.z), WithinAbs(3.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.rotation.w), WithinAbs(static_cast<double>(original.rotation.w), 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.rotation.x), WithinAbs(static_cast<double>(original.rotation.x), 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.rotation.y), WithinAbs(static_cast<double>(original.rotation.y), 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.rotation.z), WithinAbs(static_cast<double>(original.rotation.z), 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.scale.x), WithinAbs(1.5, 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.scale.y), WithinAbs(2.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(restored.scale.z), WithinAbs(0.5, 1e-5));
}

TEST_CASE("Transform3D serialize fails with insufficient buffer", "[networking][replication]") {
    ffe::Transform3D tx{};
    std::array<uint8_t, 20> small{};
    REQUIRE(serializeTransform3D(&tx, small.data(),
                                  static_cast<uint16_t>(small.size())) == 0);
}

TEST_CASE("Transform3D deserialize fails with insufficient data", "[networking][replication]") {
    ffe::Transform3D tx{};
    std::array<uint8_t, 20> small{};
    REQUIRE_FALSE(deserializeTransform3D(&tx, small.data(),
                                          static_cast<uint16_t>(small.size())));
}

// ===========================================================================
// SnapshotBuffer tests
// ===========================================================================

TEST_CASE("SnapshotBuffer starts empty", "[networking][replication]") {
    SnapshotBuffer buf;
    REQUIRE(buf.count() == 0);
    REQUIRE(buf.latest() == nullptr);
}

TEST_CASE("SnapshotBuffer push and latest", "[networking][replication]") {
    SnapshotBuffer buf;

    Snapshot s1{};
    s1.tick = 100;
    s1.entityCount = 1;
    buf.push(s1);

    REQUIRE(buf.count() == 1);
    const Snapshot* latest = buf.latest();
    REQUIRE(latest != nullptr);
    REQUIRE(latest->tick == 100);

    Snapshot s2{};
    s2.tick = 200;
    s2.entityCount = 2;
    buf.push(s2);

    REQUIRE(buf.count() == 2);
    REQUIRE(buf.latest()->tick == 200);
}

TEST_CASE("SnapshotBuffer ring buffer overflow overwrites oldest", "[networking][replication]") {
    SnapshotBuffer buf;

    // Fill the buffer completely
    for (uint32_t i = 0; i < SNAPSHOT_BUFFER_SIZE; ++i) {
        Snapshot s{};
        s.tick = i + 1;
        buf.push(s);
    }
    REQUIRE(buf.count() == SNAPSHOT_BUFFER_SIZE);
    REQUIRE(buf.latest()->tick == SNAPSHOT_BUFFER_SIZE);

    // Push one more — should overwrite the oldest (tick=1)
    Snapshot extra{};
    extra.tick = SNAPSHOT_BUFFER_SIZE + 1;
    buf.push(extra);

    REQUIRE(buf.count() == SNAPSHOT_BUFFER_SIZE); // Count stays capped
    REQUIRE(buf.latest()->tick == SNAPSHOT_BUFFER_SIZE + 1);

    // Verify the oldest remaining is tick=2 by checking interpolation pair
    const Snapshot* a = nullptr;
    const Snapshot* b = nullptr;
    float t = 0.0f;
    // renderTick=2 should find a=tick2, b=tick3
    const bool ok = buf.getInterpolationPair(2, a, b, t);
    REQUIRE(ok);
    REQUIRE(a->tick == 2);
    REQUIRE(b->tick == 3);
}

TEST_CASE("SnapshotBuffer getInterpolationPair with two snapshots", "[networking][replication]") {
    SnapshotBuffer buf;

    Snapshot s1{};
    s1.tick = 10;
    buf.push(s1);

    Snapshot s2{};
    s2.tick = 20;
    buf.push(s2);

    const Snapshot* a = nullptr;
    const Snapshot* b = nullptr;
    float t = 0.0f;

    // renderTick=15 => midpoint between 10 and 20
    REQUIRE(buf.getInterpolationPair(15, a, b, t));
    REQUIRE(a->tick == 10);
    REQUIRE(b->tick == 20);
    REQUIRE_THAT(static_cast<double>(t), WithinAbs(0.5, 1e-5));

    // renderTick=10 => at the boundary
    REQUIRE(buf.getInterpolationPair(10, a, b, t));
    REQUIRE(a->tick == 10);
    REQUIRE(b->tick == 20);
    REQUIRE_THAT(static_cast<double>(t), WithinAbs(0.0, 1e-5));
}

TEST_CASE("SnapshotBuffer getInterpolationPair fails with one snapshot",
          "[networking][replication]") {
    SnapshotBuffer buf;
    Snapshot s{};
    s.tick = 5;
    buf.push(s);

    const Snapshot* a = nullptr;
    const Snapshot* b = nullptr;
    float t = 0.0f;
    REQUIRE_FALSE(buf.getInterpolationPair(5, a, b, t));
}

TEST_CASE("SnapshotBuffer getInterpolationPair fails when renderTick out of range",
          "[networking][replication]") {
    SnapshotBuffer buf;

    Snapshot s1{};
    s1.tick = 10;
    buf.push(s1);

    Snapshot s2{};
    s2.tick = 20;
    buf.push(s2);

    const Snapshot* a = nullptr;
    const Snapshot* b = nullptr;
    float t = 0.0f;

    // renderTick=25 => beyond the newest snapshot
    REQUIRE_FALSE(buf.getInterpolationPair(25, a, b, t));

    // renderTick=5 => before the oldest snapshot
    REQUIRE_FALSE(buf.getInterpolationPair(5, a, b, t));
}

// ===========================================================================
// Transform interpolation correctness
// ===========================================================================

TEST_CASE("Transform interpolation at t=0", "[networking][replication]") {
    ffe::Transform a{};
    a.position = {0.0f, 0.0f, 0.0f};
    a.rotation = 0.0f;
    a.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Transform b{};
    b.position = {10.0f, 20.0f, 0.0f};
    b.rotation = 3.14f;
    b.scale    = {2.0f, 2.0f, 1.0f};

    ffe::Transform result{};
    interpolateTransform(&result, &a, &b, 0.0f);

    REQUIRE_THAT(static_cast<double>(result.position.x), WithinAbs(0.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.position.y), WithinAbs(0.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.rotation), WithinAbs(0.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.scale.x), WithinAbs(1.0, 1e-5));
}

TEST_CASE("Transform interpolation at t=1", "[networking][replication]") {
    ffe::Transform a{};
    a.position = {0.0f, 0.0f, 0.0f};
    a.rotation = 0.0f;
    a.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Transform b{};
    b.position = {10.0f, 20.0f, 0.0f};
    b.rotation = 3.14f;
    b.scale    = {2.0f, 2.0f, 1.0f};

    ffe::Transform result{};
    interpolateTransform(&result, &a, &b, 1.0f);

    REQUIRE_THAT(static_cast<double>(result.position.x), WithinAbs(10.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.position.y), WithinAbs(20.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.rotation), WithinAbs(3.14, 1e-4));
    REQUIRE_THAT(static_cast<double>(result.scale.x), WithinAbs(2.0, 1e-5));
}

TEST_CASE("Transform interpolation at t=0.5", "[networking][replication]") {
    ffe::Transform a{};
    a.position = {0.0f, 0.0f, 0.0f};
    a.rotation = 0.0f;
    a.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Transform b{};
    b.position = {10.0f, 20.0f, 0.0f};
    b.rotation = 2.0f;
    b.scale    = {3.0f, 3.0f, 1.0f};

    ffe::Transform result{};
    interpolateTransform(&result, &a, &b, 0.5f);

    REQUIRE_THAT(static_cast<double>(result.position.x), WithinAbs(5.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.position.y), WithinAbs(10.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.rotation), WithinAbs(1.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.scale.x), WithinAbs(2.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.scale.y), WithinAbs(2.0, 1e-5));
}

// ===========================================================================
// Transform3D slerp interpolation
// ===========================================================================

TEST_CASE("Transform3D interpolation at t=0 returns first state", "[networking][replication]") {
    ffe::Transform3D a{};
    a.position = {1.0f, 2.0f, 3.0f};
    a.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity
    a.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Transform3D b{};
    b.position = {10.0f, 20.0f, 30.0f};
    b.rotation = glm::quat(0.0f, 0.0f, 1.0f, 0.0f); // 180 deg around Y
    b.scale    = {2.0f, 2.0f, 2.0f};

    ffe::Transform3D result{};
    interpolateTransform3D(&result, &a, &b, 0.0f);

    REQUIRE_THAT(static_cast<double>(result.position.x), WithinAbs(1.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.position.y), WithinAbs(2.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.position.z), WithinAbs(3.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.rotation.w), WithinAbs(1.0, 1e-4));
    REQUIRE_THAT(static_cast<double>(result.scale.x), WithinAbs(1.0, 1e-5));
}

TEST_CASE("Transform3D interpolation at t=1 returns second state", "[networking][replication]") {
    ffe::Transform3D a{};
    a.position = {0.0f, 0.0f, 0.0f};
    a.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity
    a.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Transform3D b{};
    b.position = {10.0f, 20.0f, 30.0f};
    b.rotation = glm::quat(0.0f, 0.0f, 1.0f, 0.0f); // 180 deg around Y
    b.scale    = {2.0f, 2.0f, 2.0f};

    ffe::Transform3D result{};
    interpolateTransform3D(&result, &a, &b, 1.0f);

    REQUIRE_THAT(static_cast<double>(result.position.x), WithinAbs(10.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.position.y), WithinAbs(20.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.position.z), WithinAbs(30.0, 1e-5));
    REQUIRE_THAT(static_cast<double>(result.scale.x), WithinAbs(2.0, 1e-5));
}

TEST_CASE("Transform3D slerp at t=0.5 gives midpoint rotation", "[networking][replication]") {
    // Identity to 90-degree rotation around Y axis
    ffe::Transform3D a{};
    a.position = {0.0f, 0.0f, 0.0f};
    a.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity
    a.scale    = {1.0f, 1.0f, 1.0f};

    // 90 degrees around Y: quat = (cos(45deg), 0, sin(45deg), 0)
    const float c = std::cos(3.14159265f / 4.0f);
    const float s = std::sin(3.14159265f / 4.0f);
    ffe::Transform3D b{};
    b.position = {10.0f, 0.0f, 0.0f};
    b.rotation = glm::quat(c, 0.0f, s, 0.0f);
    b.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Transform3D result{};
    interpolateTransform3D(&result, &a, &b, 0.5f);

    // Position should be midpoint
    REQUIRE_THAT(static_cast<double>(result.position.x), WithinAbs(5.0, 1e-5));

    // Rotation at t=0.5 between identity and 90-deg-Y should be 45-deg-Y
    // quat for 45 deg around Y: (cos(22.5deg), 0, sin(22.5deg), 0)
    const float c45 = std::cos(3.14159265f / 8.0f);
    const float s45 = std::sin(3.14159265f / 8.0f);
    REQUIRE_THAT(static_cast<double>(result.rotation.w), WithinAbs(static_cast<double>(c45), 1e-3));
    REQUIRE_THAT(static_cast<double>(result.rotation.x), WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(static_cast<double>(result.rotation.y), WithinAbs(static_cast<double>(s45), 1e-3));
    REQUIRE_THAT(static_cast<double>(result.rotation.z), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Transform3D slerp handles nearly identical quaternions (lerp fallback)",
          "[networking][replication]") {
    ffe::Transform3D a{};
    a.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    a.scale    = {1.0f, 1.0f, 1.0f};

    // Very slightly rotated
    ffe::Transform3D b{};
    b.rotation = glm::quat(0.99999f, 0.0f, 0.001f, 0.0f);
    b.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Transform3D result{};
    // Should not crash or produce NaN
    interpolateTransform3D(&result, &a, &b, 0.5f);

    // Result should be roughly halfway and still a valid quaternion
    const float len = std::sqrt(result.rotation.w * result.rotation.w +
                                result.rotation.x * result.rotation.x +
                                result.rotation.y * result.rotation.y +
                                result.rotation.z * result.rotation.z);
    REQUIRE_THAT(static_cast<double>(len), WithinAbs(1.0, 1e-3));
}
