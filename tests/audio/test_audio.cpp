#include <catch2/catch_test_macros.hpp>
#include "audio/audio.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>

// =============================================================================
// Design notes
// =============================================================================
//
// Audio subsystem tests run in a separate executable (ffe_tests_audio) because:
//
// 1. init()/shutdown() global state: the audio module uses a static singleton.
//    Each test that calls init() must call shutdown() to reset state for
//    subsequent tests. A separate executable ensures no cross-test contamination.
//
// 2. loadSound() state: the write-once semantics and sound table persist across
//    test cases within a single process. Isolation prevents interference.
//
// 3. Thread safety: the audio callback runs on a background thread (non-headless
//    mode). Isolating audio tests avoids interference with other test teardown.
//
// All tests that call init() also call shutdown() at the end (or explicitly
// test double-init behaviour). Tests use headless=true for path validation
// tests so they do not require audio hardware.

// =============================================================================
// Helper: a guaranteed-existing absolute directory for path validation tests.
// /tmp is present on all POSIX systems and is always a valid absolute directory.
// =============================================================================
static constexpr const char* SAFE_ROOT = "/tmp";

// =============================================================================
// Minimal valid WAV file (48 bytes total).
// RIFF/PCM 44100 Hz, mono, 16-bit, 2 silent samples.
// Written to /tmp/ffe_test_audio.wav for the positive load test.
// =============================================================================
namespace {

// Minimal WAV: RIFF header (44 bytes) + 4 bytes of PCM data (2 mono 16-bit samples).
// RIFF chunk size = total_file_size - 8 = 48 - 8 = 40 = 0x28.
static const unsigned char k_minimalWav[] = {
    // RIFF chunk descriptor
    0x52, 0x49, 0x46, 0x46,  // "RIFF"
    0x28, 0x00, 0x00, 0x00,  // chunk size = 40 (total 48 - 8)
    0x57, 0x41, 0x56, 0x45,  // "WAVE"
    // fmt sub-chunk
    0x66, 0x6D, 0x74, 0x20,  // "fmt "
    0x10, 0x00, 0x00, 0x00,  // sub-chunk size = 16
    0x01, 0x00,              // AudioFormat = 1 (PCM)
    0x01, 0x00,              // NumChannels = 1 (mono)
    0x44, 0xAC, 0x00, 0x00,  // SampleRate = 44100
    0x88, 0x58, 0x01, 0x00,  // ByteRate = 44100 * 1 * 2 = 88200
    0x02, 0x00,              // BlockAlign = 1 * 16/8 = 2
    0x10, 0x00,              // BitsPerSample = 16
    // data sub-chunk
    0x64, 0x61, 0x74, 0x61,  // "data"
    0x04, 0x00, 0x00, 0x00,  // sub-chunk size = 4 bytes
    0x00, 0x00,              // sample 1: 0 (silence)
    0x00, 0x00               // sample 2: 0 (silence)
};
static constexpr std::size_t k_minimalWavSize = sizeof(k_minimalWav);

static constexpr const char* k_testWavAbsPath = "/tmp/ffe_test_audio.wav";
static constexpr const char* k_testWavRelPath  = "ffe_test_audio.wav";

bool writeTestWav() {
    FILE* const f = ::fopen(k_testWavAbsPath, "wb");
    if (!f) { return false; }
    const std::size_t written = ::fwrite(k_minimalWav, 1u, k_minimalWavSize, f);
    ::fclose(f);
    return written == k_minimalWavSize;
}

} // anonymous namespace

// =============================================================================
// Test 1: init() in headless mode returns true
// =============================================================================

TEST_CASE("audio::init in headless mode returns true", "[audio][init][headless]") {
    REQUIRE(ffe::audio::init(true));
    REQUIRE_FALSE(ffe::audio::isAudioAvailable()); // headless = no real device
    ffe::audio::shutdown();
}

// =============================================================================
// Test 2: init() twice logs a warning but does not crash
// =============================================================================

TEST_CASE("audio::init called twice returns false on second call", "[audio][init]") {
    REQUIRE(ffe::audio::init(true));
    // Second call must return false and not crash
    REQUIRE_FALSE(ffe::audio::init(true));
    ffe::audio::shutdown();
}

// =============================================================================
// Test 3: loadSound with null path returns invalid handle
// =============================================================================

TEST_CASE("audio::loadSound with null path returns invalid handle", "[audio][path][security]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound(nullptr, SAFE_ROOT);
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

// =============================================================================
// Test 4: loadSound with path traversal returns invalid handle (SEC-1)
// =============================================================================

TEST_CASE("audio::loadSound with path traversal ../ returns invalid handle",
          "[audio][path][security]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound("../../../etc/passwd", SAFE_ROOT);
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

TEST_CASE("audio::loadSound with embedded path traversal returns invalid handle",
          "[audio][path][security]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound("sfx/../../etc/passwd", SAFE_ROOT);
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

TEST_CASE("audio::loadSound with trailing /.. returns invalid handle",
          "[audio][path][security]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound("sfx/..", SAFE_ROOT);
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

// =============================================================================
// Test 5: loadSound with absolute path returns invalid handle (SEC-1)
// =============================================================================

TEST_CASE("audio::loadSound with absolute Unix path returns invalid handle",
          "[audio][path][security]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound("/etc/passwd", SAFE_ROOT);
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

TEST_CASE("audio::loadSound with Windows drive letter path returns invalid handle",
          "[audio][path][security]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound("C:\\sfx\\jump.wav", SAFE_ROOT);
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

// =============================================================================
// Test 6: loadSound with nonexistent file returns invalid handle
// =============================================================================

TEST_CASE("audio::loadSound with nonexistent file returns invalid handle",
          "[audio][path]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound(
        "definitely_does_not_exist_ffe_audio_test.wav", SAFE_ROOT);
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

// =============================================================================
// Test 7: loadSound with nonexistent assetRoot returns invalid handle
// =============================================================================

TEST_CASE("audio::loadSound with nonexistent assetRoot returns invalid handle",
          "[audio][path]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound(
        "test.wav", "/nonexistent_ffe_audio_test_root_99999");
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

TEST_CASE("audio::loadSound with null assetRoot returns invalid handle",
          "[audio][path]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound("test.wav", nullptr);
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

TEST_CASE("audio::loadSound with relative assetRoot returns invalid handle",
          "[audio][path]") {
    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound("test.wav", "relative/path");
    REQUIRE_FALSE(ffe::audio::isValid(h));
    ffe::audio::shutdown();
}

// =============================================================================
// Test 8: unloadSound with invalid handle is a no-op
// =============================================================================

TEST_CASE("audio::unloadSound with invalid handle is a no-op", "[audio]") {
    REQUIRE(ffe::audio::init(true));
    // Must not crash or assert
    ffe::audio::unloadSound(ffe::audio::SoundHandle{0});
    ffe::audio::shutdown();
}

// =============================================================================
// Test 9: playSound in headless mode is a no-op (no crash)
// =============================================================================

TEST_CASE("audio::playSound in headless mode is a no-op", "[audio][headless]") {
    REQUIRE(ffe::audio::init(true));
    // playSound with invalid handle must not crash
    ffe::audio::playSound(ffe::audio::SoundHandle{0});
    // playSound with a made-up valid-looking handle must not crash
    ffe::audio::playSound(ffe::audio::SoundHandle{42}, 0.5f);
    ffe::audio::shutdown();
}

// =============================================================================
// Test 10: setMasterVolume clamps correctly; NaN treated as 0.0
// =============================================================================

TEST_CASE("audio::setMasterVolume clamps to [0.0, 1.0]", "[audio][volume]") {
    REQUIRE(ffe::audio::init(true));

    ffe::audio::setMasterVolume(0.5f);
    REQUIRE(std::fabs(ffe::audio::getMasterVolume() - 0.5f) < 1e-6f);

    ffe::audio::setMasterVolume(-1.0f);
    REQUIRE(std::fabs(ffe::audio::getMasterVolume() - 0.0f) < 1e-6f);

    ffe::audio::setMasterVolume(2.0f);
    REQUIRE(std::fabs(ffe::audio::getMasterVolume() - 1.0f) < 1e-6f);

    // NaN must clamp to 0.0 (SEC-6)
    ffe::audio::setMasterVolume(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(std::fabs(ffe::audio::getMasterVolume() - 0.0f) < 1e-6f);

    // Infinity must clamp to 0.0 (SEC-6)
    ffe::audio::setMasterVolume(std::numeric_limits<float>::infinity());
    REQUIRE(std::fabs(ffe::audio::getMasterVolume() - 0.0f) < 1e-6f);

    ffe::audio::shutdown();
}

// =============================================================================
// Test 11: loadSound with valid WAV file returns valid handle (positive test)
// =============================================================================

TEST_CASE("audio::loadSound with valid WAV file returns valid handle",
          "[audio][wav][requires_file]") {
    if (!writeTestWav()) {
        WARN("Could not write test WAV to " << k_testWavAbsPath << " — skipping");
        return;
    }

    REQUIRE(ffe::audio::init(true));
    const ffe::audio::SoundHandle h = ffe::audio::loadSound(k_testWavRelPath, SAFE_ROOT);
    REQUIRE(ffe::audio::isValid(h));
    ffe::audio::unloadSound(h);
    ffe::audio::shutdown();
}

// =============================================================================
// Test 12: getActiveVoiceCount returns 0 after init with no sounds playing
// =============================================================================

TEST_CASE("audio::getActiveVoiceCount returns 0 after init", "[audio][headless]") {
    REQUIRE(ffe::audio::init(true));
    REQUIRE(ffe::audio::getActiveVoiceCount() == 0u);
    ffe::audio::shutdown();
}

// =============================================================================
// Test 13: isAudioAvailable returns false in headless mode
// =============================================================================

TEST_CASE("audio::isAudioAvailable returns false in headless mode", "[audio][headless]") {
    REQUIRE(ffe::audio::init(true));
    REQUIRE_FALSE(ffe::audio::isAudioAvailable());
    ffe::audio::shutdown();
}

// =============================================================================
// Test 14: shutdown without init is a no-op (does not crash)
// =============================================================================

TEST_CASE("audio::shutdown without init is a no-op", "[audio]") {
    // Do NOT call init — shutdown must be safe to call at any time
    ffe::audio::shutdown();
}
