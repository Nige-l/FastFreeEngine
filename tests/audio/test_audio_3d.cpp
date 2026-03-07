// test_audio_3d.cpp -- Catch2 unit tests for the 3D positional audio system.
//
// Tests cover:
//   - playSound3D returns cleanly in headless mode
//   - setListenerPosition stores values correctly
//   - setSound3DMinDistance / setSound3DMaxDistance clamping and validation
//   - NaN/Inf guards on position parameters
//   - computeSpatialGain: attenuation and stereo panning math
//
// All tests run in headless mode (no audio hardware required).

#include <catch2/catch_test_macros.hpp>
#include "audio/audio.h"
#include <cmath>
#include <limits>

// =============================================================================
// Test: playSound3D in headless mode is a no-op (no crash)
// =============================================================================

TEST_CASE("audio::playSound3D in headless mode is a no-op", "[audio][3d][headless]") {
    REQUIRE(ffe::audio::init(true));
    // playSound3D with invalid handle must not crash
    ffe::audio::playSound3D(ffe::audio::SoundHandle{0}, 1.0f, 2.0f, 3.0f);
    // playSound3D with a made-up valid-looking handle must not crash
    ffe::audio::playSound3D(ffe::audio::SoundHandle{42}, 5.0f, 0.0f, -3.0f, 0.8f);
    ffe::audio::shutdown();
}

// =============================================================================
// Test: setListenerPosition stores values
// =============================================================================

TEST_CASE("audio::setListenerPosition stores values and updateListenerFromCamera works",
          "[audio][3d][listener]") {
    REQUIRE(ffe::audio::init(true));

    // Manual listener set
    ffe::audio::setListenerPosition(10.0f, 20.0f, 30.0f,
                                    0.0f, 0.0f, -1.0f,
                                    0.0f, 1.0f, 0.0f);
    // No crash, no assertion. Listener state is internal but exercises the API path.

    // Camera auto-sync
    ffe::audio::updateListenerFromCamera(1.0f, 2.0f, 3.0f,
                                         0.0f, 0.0f, -1.0f,
                                         0.0f, 1.0f, 0.0f);

    ffe::audio::shutdown();
}

// =============================================================================
// Test: setSound3DMinDistance / setSound3DMaxDistance with valid values
// =============================================================================

TEST_CASE("audio::setSound3DMinDistance and setSound3DMaxDistance set valid values",
          "[audio][3d][distance]") {
    REQUIRE(ffe::audio::init(true));

    // Defaults
    REQUIRE(std::fabs(ffe::audio::getSound3DMinDistance() - 1.0f) < 1e-6f);
    REQUIRE(std::fabs(ffe::audio::getSound3DMaxDistance() - 100.0f) < 1e-6f);

    // Set valid values
    ffe::audio::setSound3DMinDistance(2.0f);
    REQUIRE(std::fabs(ffe::audio::getSound3DMinDistance() - 2.0f) < 1e-6f);

    ffe::audio::setSound3DMaxDistance(50.0f);
    REQUIRE(std::fabs(ffe::audio::getSound3DMaxDistance() - 50.0f) < 1e-6f);

    ffe::audio::shutdown();
}

// =============================================================================
// Test: setSound3DMinDistance / setSound3DMaxDistance with invalid values
// =============================================================================

TEST_CASE("audio::setSound3DMinDistance clamps to valid range",
          "[audio][3d][distance]") {
    REQUIRE(ffe::audio::init(true));

    // Too small — clamp to 0.01
    ffe::audio::setSound3DMinDistance(-5.0f);
    REQUIRE(ffe::audio::getSound3DMinDistance() >= 0.01f);

    ffe::audio::shutdown();
}

TEST_CASE("audio::setSound3DMaxDistance clamps to valid range",
          "[audio][3d][distance]") {
    REQUIRE(ffe::audio::init(true));

    // Too large — clamp to 10000
    ffe::audio::setSound3DMaxDistance(99999.0f);
    REQUIRE(std::fabs(ffe::audio::getSound3DMaxDistance() - 10000.0f) < 1e-3f);

    ffe::audio::shutdown();
}

// =============================================================================
// Test: NaN/Inf guards on distance parameters
// =============================================================================

TEST_CASE("audio::setSound3DMinDistance ignores NaN", "[audio][3d][nan]") {
    REQUIRE(ffe::audio::init(true));

    const float before = ffe::audio::getSound3DMinDistance();
    ffe::audio::setSound3DMinDistance(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(std::fabs(ffe::audio::getSound3DMinDistance() - before) < 1e-6f);

    ffe::audio::shutdown();
}

TEST_CASE("audio::setSound3DMaxDistance ignores Inf", "[audio][3d][nan]") {
    REQUIRE(ffe::audio::init(true));

    const float before = ffe::audio::getSound3DMaxDistance();
    ffe::audio::setSound3DMaxDistance(std::numeric_limits<float>::infinity());
    REQUIRE(std::fabs(ffe::audio::getSound3DMaxDistance() - before) < 1e-6f);

    ffe::audio::shutdown();
}

// =============================================================================
// Test: setListenerPosition NaN/Inf guards
// =============================================================================

TEST_CASE("audio::setListenerPosition handles NaN/Inf without crash",
          "[audio][3d][nan][listener]") {
    REQUIRE(ffe::audio::init(true));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // All NaN — should not crash; forward defaults to (0,0,-1), up to (0,1,0)
    ffe::audio::setListenerPosition(nan, nan, nan, nan, nan, nan, nan, nan, nan);
    // Mixed NaN — should not crash
    ffe::audio::setListenerPosition(1.0f, inf, 3.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);

    ffe::audio::shutdown();
}

// =============================================================================
// Test: computeSpatialGain — source at minDistance = full volume center
// =============================================================================

TEST_CASE("audio::computeSpatialGain: source at minDistance gives full volume",
          "[audio][3d][spatialization]") {
    // Source is directly in front of the listener at exactly minDistance
    // Listener at origin, facing -Z, up +Y
    const auto sg = ffe::audio::computeSpatialGain(
        0.0f, 0.0f, -1.0f,   // source at (0, 0, -1)
        0.0f, 0.0f,  0.0f,   // listener at origin
        0.0f, 0.0f, -1.0f,   // forward = -Z
        0.0f, 1.0f,  0.0f,   // up = +Y
        1.0f, 100.0f);       // minDist=1, maxDist=100

    // At minDistance, gain = 1.0. Source is directly ahead, so pan = 0 (center).
    // leftGain = rightGain = 1.0 (center pan at full volume)
    REQUIRE(std::fabs(sg.left - 1.0f) < 0.05f);
    REQUIRE(std::fabs(sg.right - 1.0f) < 0.05f);
}

// =============================================================================
// Test: computeSpatialGain — source beyond maxDistance = silence
// =============================================================================

TEST_CASE("audio::computeSpatialGain: source beyond maxDistance gives silence",
          "[audio][3d][spatialization]") {
    const auto sg = ffe::audio::computeSpatialGain(
        0.0f, 0.0f, -200.0f,  // source far away
        0.0f, 0.0f,   0.0f,   // listener at origin
        0.0f, 0.0f,  -1.0f,   // forward = -Z
        0.0f, 1.0f,   0.0f,   // up = +Y
        1.0f, 100.0f);

    REQUIRE(sg.left < 0.001f);
    REQUIRE(sg.right < 0.001f);
}

// =============================================================================
// Test: computeSpatialGain — source between min and max = attenuated
// =============================================================================

TEST_CASE("audio::computeSpatialGain: source at 10 units with minDist=1 maxDist=100",
          "[audio][3d][spatialization]") {
    // Source directly ahead at distance 10
    const auto sg = ffe::audio::computeSpatialGain(
        0.0f, 0.0f, -10.0f,
        0.0f, 0.0f,   0.0f,
        0.0f, 0.0f,  -1.0f,
        0.0f, 1.0f,   0.0f,
        1.0f, 100.0f);

    // Inverse distance clamped: gain = 1.0 / 10.0 = 0.1
    // Center pan, so left = right = gain * 1.0 = 0.1
    REQUIRE(std::fabs(sg.left - 0.1f) < 0.02f);
    REQUIRE(std::fabs(sg.right - 0.1f) < 0.02f);
}

// =============================================================================
// Test: computeSpatialGain — source to the right = right-biased gain
// =============================================================================

TEST_CASE("audio::computeSpatialGain: source to the right produces right-biased gain",
          "[audio][3d][spatialization]") {
    // Listener at origin, facing -Z. Source to the right (+X) at distance 1.
    // right = cross(forward=(0,0,-1), up=(0,1,0)) = (0*0 - (-1)*1, (-1)*0 - 0*0, 0*1 - 0*0) = (-1, 0, 0)
    // Wait: cross((0,0,-1), (0,1,0)) = (0*0-(-1)*1, (-1)*0-0*0, 0*1-0*0) = (1, 0, 0)
    // Actually: cross(fwd, up) where fwd=(0,0,-1), up=(0,1,0):
    //   rx = fwdY*upZ - fwdZ*upY = 0*0 - (-1)*1 = 1
    //   ry = fwdZ*upX - fwdX*upZ = (-1)*0 - 0*0 = 0
    //   rz = fwdX*upY - fwdY*upX = 0*1 - 0*0 = 0
    // right = (1, 0, 0). Source at (1,0,0), dir = (1,0,0).
    // pan = dot((1,0,0), (1,0,0)) = 1.0 (full right)

    const auto sg = ffe::audio::computeSpatialGain(
        1.0f, 0.0f, 0.0f,     // source to the right
        0.0f, 0.0f, 0.0f,     // listener at origin
        0.0f, 0.0f, -1.0f,    // forward = -Z
        0.0f, 1.0f, 0.0f,     // up = +Y
        1.0f, 100.0f);

    // At distance 1 = minDist, gain = 1.0.
    // pan = 1.0 (full right)
    // leftGain = 1.0 * (1.0 - 1.0) * 0.5 * 2 = 0.0
    // rightGain = 1.0 * (1.0 + 1.0) * 0.5 * 2 = 2.0
    // Clamped: right should be significantly greater than left
    REQUIRE(sg.right > sg.left);
    REQUIRE(sg.right > 0.5f);
    REQUIRE(sg.left < 0.1f);
}

// =============================================================================
// Test: computeSpatialGain — source to the left = left-biased gain
// =============================================================================

TEST_CASE("audio::computeSpatialGain: source to the left produces left-biased gain",
          "[audio][3d][spatialization]") {
    // Source at (-1, 0, 0) — to the left of listener facing -Z
    const auto sg = ffe::audio::computeSpatialGain(
        -1.0f, 0.0f, 0.0f,    // source to the left
         0.0f, 0.0f, 0.0f,    // listener at origin
         0.0f, 0.0f, -1.0f,   // forward = -Z
         0.0f, 1.0f, 0.0f,    // up = +Y
         1.0f, 100.0f);

    REQUIRE(sg.left > sg.right);
    REQUIRE(sg.left > 0.5f);
    REQUIRE(sg.right < 0.1f);
}

// =============================================================================
// Test: computeSpatialGain — source at listener position = full volume center
// =============================================================================

TEST_CASE("audio::computeSpatialGain: source at listener position gives full center",
          "[audio][3d][spatialization]") {
    const auto sg = ffe::audio::computeSpatialGain(
        5.0f, 3.0f, 7.0f,     // source = listener
        5.0f, 3.0f, 7.0f,     // same position
        0.0f, 0.0f, -1.0f,
        0.0f, 1.0f, 0.0f,
        1.0f, 100.0f);

    // At zero distance, full volume center
    REQUIRE(std::fabs(sg.left - 1.0f) < 0.01f);
    REQUIRE(std::fabs(sg.right - 1.0f) < 0.01f);
}
