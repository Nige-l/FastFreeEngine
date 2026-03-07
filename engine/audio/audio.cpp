// audio.cpp — FFE audio subsystem implementation.
//
// miniaudio v0.11.25 — https://github.com/mackron/miniaudio (LOW-3: version pinned)
// stb_vorbis v1.22   — https://github.com/nothings/stb (LOW-3: version pinned)
//
// Both are public-domain single-header libraries embedded in third_party/.
// MINIAUDIO_IMPLEMENTATION and STB_VORBIS_IMPLEMENTATION are defined in this
// file only. Including either header again without the define gives declarations.
//
// Security conditions implemented (from ADR-006-security-review.md):
//   HIGH-1:   Path concat overflow check before strncat/realpath — same as ADR-005 HIGH-2.
//   HIGH-2:   SEC-5 decoder output validation runs BEFORE u64 multiplication (SEC-4).
//             Prevents negative int cast to u64 producing 0xFFFFFFFFFFFFFFFF.
//   MEDIUM-1: isAudioPathSafe() uses strnlen(path, MAX_AUDIO_PATH) — not strlen.
//   MEDIUM-2: Code comment at stb_vorbis decode site documenting codebook heap risk.
//   SEC-1:    isAudioPathSafe() is the FIRST operation in loadSound() — before any syscall.
//   SEC-2:    stat() file-size check before fopen().
//   SEC-3:    Format allowlist via magic bytes (RIFF/OggS) before decoding.
//   SEC-4:    Decoded size computed with u64 arithmetic, checked vs AUDIO_MAX_DECODED_BYTES.
//   SEC-5:    Decoder outputs validated (frameCount > 0, channels in {1,2}, sampleRate > 0).
//   SEC-6:    Volume clamped via clampVolume() — NaN/Inf treated as 0.0.
//   LOW-1:    UNC path rejection added to isAudioPathSafe().
//   LOW-2:    Ring buffer uses memory_order_release (write) / memory_order_acquire (read).
//   LOW-4:    unloadSound() deactivates voice AND frees PCM buffer under m_mutex,
//             within the same critical section. No use-after-free window.
//   LOW-5:    playSound() per-instance volume passes through clampVolume().

// ---------------------------------------------------------------------------
// miniaudio include — suppress third-party warnings
// ---------------------------------------------------------------------------
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunused-function"
    #pragma clang diagnostic ignored "-Wsign-conversion"
    #pragma clang diagnostic ignored "-Wcast-qual"
    #pragma clang diagnostic ignored "-Wimplicit-fallthrough"
    #pragma clang diagnostic ignored "-Wdouble-promotion"
    #pragma clang diagnostic ignored "-Wpadded"
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
    #pragma clang diagnostic ignored "-Wreserved-identifier"
    #pragma clang diagnostic ignored "-Watomic-implicit-seq-cst"
    #pragma clang diagnostic ignored "-Wcast-align"
    #pragma clang diagnostic ignored "-Wconditional-uninitialized"
    #pragma clang diagnostic ignored "-Wmissing-noreturn"
    #pragma clang diagnostic ignored "-Wextra-semi-stmt"
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    #pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
    #pragma clang diagnostic ignored "-Wbad-function-cast"
    #pragma clang diagnostic ignored "-Wnullability-extension"
    #pragma clang diagnostic ignored "-Wimplicit-int-conversion"
    #pragma clang diagnostic ignored "-Wshorten-64-to-32"
    #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    #pragma clang diagnostic ignored "-Wswitch-default"
    #pragma clang diagnostic ignored "-Wformat-nonliteral"
    #pragma clang diagnostic ignored "-Wreserved-macro-identifier"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wdouble-promotion"
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    #pragma GCC diagnostic ignored "-Wcast-qual"
    #pragma GCC diagnostic ignored "-Wformat-nonliteral"
    #pragma GCC diagnostic ignored "-Wpedantic"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wunused-variable"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #pragma GCC diagnostic ignored "-Wcast-align"
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio.h"

#ifdef __clang__
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

// ---------------------------------------------------------------------------
// stb_vorbis include — suppress third-party warnings
// ---------------------------------------------------------------------------
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunused-function"
    #pragma clang diagnostic ignored "-Wsign-conversion"
    #pragma clang diagnostic ignored "-Wimplicit-fallthrough"
    #pragma clang diagnostic ignored "-Wdouble-promotion"
    #pragma clang diagnostic ignored "-Wcast-qual"
    #pragma clang diagnostic ignored "-Wshadow"
    #pragma clang diagnostic ignored "-Wpadded"
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
    #pragma clang diagnostic ignored "-Wcast-align"
    #pragma clang diagnostic ignored "-Wconditional-uninitialized"
    #pragma clang diagnostic ignored "-Wnull-dereference"
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    #pragma clang diagnostic ignored "-Wimplicit-int-conversion"
    #pragma clang diagnostic ignored "-Wshorten-64-to-32"
    #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    #pragma clang diagnostic ignored "-Wextra-semi-stmt"
    #pragma clang diagnostic ignored "-Wswitch-default"
    #pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
    #pragma clang diagnostic ignored "-Wold-style-cast"
    #pragma clang diagnostic ignored "-Wreserved-macro-identifier"
    #pragma clang diagnostic ignored "-Wreserved-identifier"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wdouble-promotion"
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    #pragma GCC diagnostic ignored "-Wcast-qual"
    #pragma GCC diagnostic ignored "-Wshadow"
    #pragma GCC diagnostic ignored "-Wpedantic"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wunused-variable"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #pragma GCC diagnostic ignored "-Wcast-align"
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    #pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

#define STB_VORBIS_IMPLEMENTATION
#include "../../third_party/stb_vorbis.c"

#ifdef __clang__
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

// ---------------------------------------------------------------------------
// FFE headers
// ---------------------------------------------------------------------------
#include "audio/audio.h"
#include "core/logging.h"

#include <atomic>
#include <climits>      // PATH_MAX
#include <cmath>        // std::isfinite
#include <cstdio>       // FILE, fopen, fread, fclose
#include <cstring>      // strnlen, strstr, memcpy, memset, strncmp
#include <mutex>
#include <sys/stat.h>   // stat(), S_ISDIR
#include <stdlib.h>     // free(), malloc()
#include "core/platform.h"
#include <cinttypes>    // PRIu64

namespace ffe::audio {

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

// A loaded sound: PCM data decoded into float32, mono or stereo.
//
// canonPath stores the realpath()-verified absolute path used at load time.
// This allows playMusic() to open the file for streaming without re-running
// path validation — the path was already hardened by loadSound().
struct SoundBuffer {
    float*  data        = nullptr;  // heap-allocated decoded PCM (float32)
    u64     frameCount  = 0u;       // number of sample frames
    u32     channels    = 0u;       // 1 = mono, 2 = stereo
    u32     sampleRate  = 0u;       // samples per second (e.g. 44100)
    bool    inUse       = false;    // slot occupied?
    char    canonPath[PATH_MAX + 1] = {};  // validated canonical path (for music streaming)
};

// An active voice: a sound currently being mixed into the output.
struct Voice {
    const SoundBuffer* buffer   = nullptr;  // points into s_sounds[]
    u64                cursor   = 0u;       // current frame position
    float              volume   = 1.0f;     // per-instance volume [0,1]
    bool               active   = false;
};

// SPSC command ring buffer: main thread writes, audio callback drains.
// Capacity 64 entries — full = drop + warn (acceptable for game audio).
// LOW-2: head uses memory_order_release on write; tail uses memory_order_acquire
//        on read. This ensures command payload fields are visible to the consumer
//        when the index update is observed. memory_order_relaxed on the indices
//        would be a data race on the struct fields.
struct AudioCommand {
    enum class Type : u8 {
        PLAY_SOUND,
        SET_MASTER_VOLUME,
        PLAY_MUSIC,         // start/restart music streaming (soundId + flags)
        STOP_MUSIC,         // stop current music track
        SET_MUSIC_VOLUME,   // update music volume atomic
    };
    Type  type    = Type::PLAY_SOUND;
    u8    flags   = 0u;     // PLAY_MUSIC: bit 0 = loop (1=loop, 0=once)
    u8    _pad0   = 0u;     // reserved
    u8    _pad1   = 0u;     // reserved
    u32   soundId = 0u;     // for PLAY_SOUND / PLAY_MUSIC: 1-based sound slot index
    float volume  = 1.0f;   // for PLAY_SOUND: per-instance volume
                            // for SET_MASTER_VOLUME / SET_MUSIC_VOLUME: new volume
};

static constexpr u32 CMD_RING_CAPACITY = 64u;

// ---------------------------------------------------------------------------
// Module constants
// ---------------------------------------------------------------------------
static constexpr u32 MAX_SOUNDS = 256u;

// ---------------------------------------------------------------------------
// Module state — zero-initialised static struct (no heap allocation at init)
// ---------------------------------------------------------------------------
static struct {
    // Lifecycle
    bool            initialised = false;
    bool            headless    = false;
    bool            deviceOpen  = false;

    // miniaudio device
    ma_device       device;
    ma_device_config deviceConfig;

    // Sound buffer table — fixed array, no per-loadSound allocation
    SoundBuffer     sounds[MAX_SOUNDS];
    u32             nextId = 1u;    // 0 is reserved for invalid

    // Voice pool — pre-allocated at init time, no per-playSound allocation
    Voice           voices[MAX_AUDIO_VOICES];

    // SPSC command ring buffer (main -> audio thread)
    AudioCommand    cmdRing[CMD_RING_CAPACITY];
    std::atomic<u32> cmdHead{0u};  // written by main thread
    std::atomic<u32> cmdTail{0u};  // written by audio callback

    // Volume — stored as atomic for lock-free reads in the callback
    std::atomic<float> masterVolume{1.0f};

    // Active voice counter — maintained atomically alongside voice activation and
    // deactivation. Allows getActiveVoiceCount() to read without acquiring the mutex.
    // All writes happen under m_mutex (or after ma_device_stop in shutdown), so there
    // is no data race; memory_order_relaxed suffices for this diagnostic counter.
    // M-2 fix: replaces the previous mutex + linear scan in getActiveVoiceCount().
    std::atomic<u32> activeVoiceCount{0u};

    // Music streaming state — only accessed from the audio callback thread,
    // except for musicVolume and musicPlaying which are atomics for main-thread reads.
    //
    // musicStream is opened/closed by the callback when processing PLAY_MUSIC /
    // STOP_MUSIC commands. It is always null when the device is stopped (shutdown).
    stb_vorbis*        musicStream   = nullptr;  // non-null when music is playing
    bool               musicLoop     = false;    // current track looping?
    std::atomic<float> musicVolume{1.0f};        // music volume [0,1], read by main thread
    std::atomic<bool>  musicPlaying{false};      // true while track is active

    // Mutex protecting voice pool and sound buffer deactivation
    std::mutex      mutex;
} s_state;

// ---------------------------------------------------------------------------
// Volume clamping (SEC-6)
// ---------------------------------------------------------------------------
static float clampVolume(const float v) {
    if (!std::isfinite(v)) { return 0.0f; }
    if (v < 0.0f) { return 0.0f; }
    if (v > 1.0f) { return 1.0f; }
    return v;
}

// ---------------------------------------------------------------------------
// Path validation (SEC-1, MEDIUM-1, LOW-1)
// ---------------------------------------------------------------------------

// isAudioPathSafe — path traversal prevention.
// MUST be the first operation called on any caller-supplied path.
// MEDIUM-1: uses strnlen, not strlen, to handle non-null-terminated buffers.
static bool isAudioPathSafe(const char* const path) {
    if (!path) {
        return false;
    }
    // MEDIUM-1: bound path length with strnlen — do not rely on strlen
    // (protects against non-null-terminated or oversized buffers).
    const size_t pathLen = strnlen(path, static_cast<size_t>(MAX_AUDIO_PATH));
    if (pathLen >= static_cast<size_t>(MAX_AUDIO_PATH)) {
        return false;
    }
    if (path[0] == '\0') {
        return false;
    }
    // Reject absolute Unix paths
    if (path[0] == '/') {
        return false;
    }
    // Reject absolute Windows paths (backslash root)
    if (path[0] == '\\') {
        return false;
    }
    // Reject drive letters (e.g., "C:")
    if (pathLen >= 2u && path[1] == ':') {
        return false;
    }
    // LOW-1: reject UNC paths ("\\server\share\...") for Windows portability.
    // realpath() is POSIX-only; a porting note for Windows (_fullpath()) will be
    // needed in the future. UNC rejection is defence-in-depth on POSIX.
    if (path[0] == '\\' && pathLen >= 2u && path[1] == '\\') {
        return false;
    }
    // Reject traversal sequences
    if (strstr(path, "../") != nullptr)  { return false; }
    if (strstr(path, "..\\") != nullptr) { return false; }
    if (strstr(path, "/..") != nullptr)  { return false; }
    if (strstr(path, "\\..") != nullptr) { return false; }
    // Reject Windows Alternate Data Streams (e.g. "audio/music.ogg:stream").
    // Legitimate relative asset paths never contain ':'.
    // Drive-letter absolute paths ("C:\...") are already rejected above by the
    // path[1]==':' check, so any remaining ':' is always an ADS or device path.
    if (strchr(path, ':') != nullptr) { return false; }
    return true;
}

// validateAssetRoot — check an absolute directory path.
static bool validateAssetRoot(const char* const absPath) {
    if (!absPath || absPath[0] == '\0') {
        return false;
    }
    // Must be absolute (start with '/')
    if (absPath[0] != '/') {
        return false;
    }
    struct stat st{};
    if (::stat(absPath, &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Audio callback (runs on miniaudio's audio thread)
//
// M-1 fix: Previously the mutex was acquired once per PLAY_SOUND command in
// the ring-drain loop, then once more for the mixing loop (N+1 acquisitions
// per callback where N = number of PLAY_SOUND commands). This refactor takes
// the mutex exactly once per callback regardless of command count.
//
// Structure:
//   Phase 1: Drain the SPSC ring buffer WITHOUT holding the mutex.
//     - PLAY_SOUND: stage in pendingPlay[]
//     - SET_MASTER_VOLUME / SET_MUSIC_VOLUME: atomic stores, no mutex
//     - PLAY_MUSIC: record last command (last write wins)
//     - STOP_MUSIC: set pendingStop flag
//   Phase 2: Acquire mutex ONCE for:
//     - Music command dispatch (stop / start stream)
//     - PLAY_SOUND voice activation
//     - SFX mixing
//     - Music decode and mixing
// ---------------------------------------------------------------------------
static void audioCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/,
                           ma_uint32 frameCount) {
    // We output float32 stereo PCM.
    float* const out = static_cast<float*>(pOutput);
    const u32 outFrames = static_cast<u32>(frameCount);

    // Zero the output buffer first.
    memset(out, 0, static_cast<size_t>(outFrames) * 2u * sizeof(float));

    // -----------------------------------------------------------------------
    // Phase 1: Drain the SPSC command ring buffer (no mutex required).
    //
    // LOW-2: tail read uses memory_order_acquire to see payload fields written
    // by the main thread with memory_order_release on cmdHead.
    // -----------------------------------------------------------------------
    const u32 head = s_state.cmdHead.load(std::memory_order_acquire);
    u32 tail = s_state.cmdTail.load(std::memory_order_relaxed);

    // Staging for PLAY_SOUND commands (activated in Phase 2 under mutex).
    AudioCommand pendingPlay[CMD_RING_CAPACITY];
    u32 pendingCount = 0u;

    // Staging for music commands. PLAY_MUSIC uses last-write-wins — only
    // the most recent PLAY_MUSIC in a single callback period is acted on.
    bool hasPendingPlayMusic = false;
    AudioCommand pendingPlayMusic;          // last PLAY_MUSIC command seen
    bool pendingStopMusic = false;

    while (tail != head) {
        const AudioCommand& cmd = s_state.cmdRing[tail % CMD_RING_CAPACITY];
        switch (cmd.type) {
            case AudioCommand::Type::PLAY_SOUND:
                if (pendingCount < CMD_RING_CAPACITY) {
                    pendingPlay[pendingCount++] = cmd;
                }
                break;
            case AudioCommand::Type::SET_MASTER_VOLUME:
                s_state.masterVolume.store(cmd.volume, std::memory_order_relaxed);
                break;
            case AudioCommand::Type::SET_MUSIC_VOLUME:
                s_state.musicVolume.store(cmd.volume, std::memory_order_relaxed);
                break;
            case AudioCommand::Type::PLAY_MUSIC:
                hasPendingPlayMusic = true;
                pendingPlayMusic = cmd;
                pendingStopMusic = false;   // PLAY supersedes a pending STOP
                break;
            case AudioCommand::Type::STOP_MUSIC:
                pendingStopMusic = true;
                hasPendingPlayMusic = false; // STOP supersedes a pending PLAY
                break;
        }
        tail = (tail + 1u) % CMD_RING_CAPACITY;
        s_state.cmdTail.store(tail, std::memory_order_release);
    }

    // -----------------------------------------------------------------------
    // Phase 2: Single mutex acquisition — music dispatch + SFX + music mixing.
    // -----------------------------------------------------------------------
    const float master   = s_state.masterVolume.load(std::memory_order_relaxed);
    const float musicVol = s_state.musicVolume.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(s_state.mutex);

        // --- Music command dispatch ---

        // Process STOP_MUSIC
        if (pendingStopMusic && s_state.musicStream != nullptr) {
            stb_vorbis_close(s_state.musicStream);
            s_state.musicStream = nullptr;
            s_state.musicPlaying.store(false, std::memory_order_relaxed);
        }

        // Process PLAY_MUSIC (last-write-wins for rapid track switches)
        if (hasPendingPlayMusic) {
            // Close existing stream first (track switch or replay)
            if (s_state.musicStream != nullptr) {
                stb_vorbis_close(s_state.musicStream);
                s_state.musicStream = nullptr;
            }

            // MEDIUM-1 (security): bounds-check soundId before path retrieval.
            const u32 idx = pendingPlayMusic.soundId - 1u;
            if (idx < MAX_SOUNDS && s_state.sounds[idx].inUse &&
                s_state.sounds[idx].canonPath[0] != '\0')
            {
                // LOW-1 (security): check stb_vorbis error code on open failure.
                int vorbisError = 0;
                stb_vorbis* stream = stb_vorbis_open_filename(
                    s_state.sounds[idx].canonPath, &vorbisError, nullptr);

                if (stream) {
                    s_state.musicStream = stream;
                    s_state.musicLoop   = (pendingPlayMusic.flags & 0x01u) != 0u;
                    s_state.musicPlaying.store(true, std::memory_order_relaxed);
                } else {
                    FFE_LOG_ERROR("Audio",
                        "audioCallback: stb_vorbis_open_filename() failed "
                        "(error=%d) for \"%s\"",
                        vorbisError, s_state.sounds[idx].canonPath);
                    s_state.musicPlaying.store(false, std::memory_order_relaxed);
                }
            } else {
                FFE_LOG_ERROR("Audio",
                    "audioCallback: PLAY_MUSIC with invalid/unloaded soundId=%u",
                    pendingPlayMusic.soundId);
            }
        }

        // --- Activate queued PLAY_SOUND commands ---
        for (u32 c = 0u; c < pendingCount; ++c) {
            const AudioCommand& cmd = pendingPlay[c];
            bool placed = false;
            for (u32 i = 0u; i < MAX_AUDIO_VOICES; ++i) {
                if (!s_state.voices[i].active) {
                    const u32 idx = cmd.soundId - 1u;
                    if (idx < MAX_SOUNDS && s_state.sounds[idx].inUse) {
                        s_state.voices[i].buffer  = &s_state.sounds[idx];
                        s_state.voices[i].cursor  = 0u;
                        s_state.voices[i].volume  = cmd.volume;
                        s_state.voices[i].active  = true;
                        s_state.activeVoiceCount.fetch_add(1u, std::memory_order_relaxed);
                        placed = true;
                    }
                    break;
                }
            }
            if (!placed) {
                FFE_LOG_WARN("Audio",
                    "audioCallback: all %u voice slots occupied — sound dropped",
                    MAX_AUDIO_VOICES);
            }
        }

        // --- Mix active SFX voices ---
        for (u32 v = 0u; v < MAX_AUDIO_VOICES; ++v) {
            Voice& voice = s_state.voices[v];
            if (!voice.active) { continue; }

            const SoundBuffer* const buf = voice.buffer;
            if (!buf || !buf->data) {
                voice.active = false;
                s_state.activeVoiceCount.fetch_sub(1u, std::memory_order_relaxed);
                continue;
            }

            const float gainLeft  = voice.volume * master;
            const float gainRight = gainLeft;

            for (u32 f = 0u; f < outFrames; ++f) {
                if (voice.cursor >= buf->frameCount) {
                    voice.active = false;
                    s_state.activeVoiceCount.fetch_sub(1u, std::memory_order_relaxed);
                    break;
                }

                if (buf->channels == 2u) {
                    out[f * 2u + 0u] += buf->data[voice.cursor * 2u + 0u] * gainLeft;
                    out[f * 2u + 1u] += buf->data[voice.cursor * 2u + 1u] * gainRight;
                } else {
                    const float sample = buf->data[voice.cursor] * gainLeft;
                    out[f * 2u + 0u] += sample;
                    out[f * 2u + 1u] += sample;
                }
                ++voice.cursor;
            }
        }

        // --- Music streaming decode and mix ---
        if (s_state.musicStream != nullptr) {
            // Decode up to outFrames of float stereo PCM into a static buffer.
            // Static: avoids VLA or large stack allocation; safe here because
            // the audio callback runs on a single dedicated thread.
            //
            // 4096 frames × 2 channels × 4 bytes = 32 KB.
            // Typical callback size is 256–1024 frames; 4096 is a defensive cap.
            static constexpr u32 MUSIC_DECODE_CAP = 4096u;
            static float musicDecBuf[MUSIC_DECODE_CAP * 2u];

            const u32 decodeFrames = outFrames < MUSIC_DECODE_CAP
                                   ? outFrames : MUSIC_DECODE_CAP;

            const int decoded = stb_vorbis_get_samples_float_interleaved(
                s_state.musicStream, 2, musicDecBuf,
                static_cast<int>(decodeFrames * 2u));

            if (decoded > 0) {
                const float musicGain = musicVol * master;
                for (u32 f = 0u; f < static_cast<u32>(decoded); ++f) {
                    out[f * 2u + 0u] += musicDecBuf[f * 2u + 0u] * musicGain;
                    out[f * 2u + 1u] += musicDecBuf[f * 2u + 1u] * musicGain;
                }
            }

            // End-of-track: decoded < decodeFrames means the stream exhausted.
            if (static_cast<u32>(decoded) < decodeFrames) {
                if (s_state.musicLoop) {
                    // MEDIUM-2 (security): check seek return value — can fail on corrupt OGG.
                    const int seekOk = stb_vorbis_seek_start(s_state.musicStream);
                    if (!seekOk) {
                        FFE_LOG_ERROR("Audio",
                            "audioCallback: stb_vorbis_seek_start() failed — stopping music");
                        stb_vorbis_close(s_state.musicStream);
                        s_state.musicStream = nullptr;
                        s_state.musicPlaying.store(false, std::memory_order_relaxed);
                    }
                } else {
                    stb_vorbis_close(s_state.musicStream);
                    s_state.musicStream = nullptr;
                    s_state.musicPlaying.store(false, std::memory_order_relaxed);
                }
            }
        }
    }

    // Suppress unused parameter warning from miniaudio (pDevice is reserved for future use)
    static_cast<void>(pDevice);
}

// ---------------------------------------------------------------------------
// postCommand — main thread writes a command to the SPSC ring.
// LOW-2: head write uses memory_order_release so the payload is visible
//        before the index increment is observed by the consumer.
// ---------------------------------------------------------------------------
static bool postCommand(const AudioCommand& cmd) {
    const u32 head = s_state.cmdHead.load(std::memory_order_relaxed);
    const u32 tail = s_state.cmdTail.load(std::memory_order_acquire);
    const u32 nextHead = (head + 1u) % CMD_RING_CAPACITY;
    // M-3 fix: tail is always stored as (tail + 1) % CMD_RING_CAPACITY so it is
    // already in [0, CMD_RING_CAPACITY) — the modulo here was redundant.
    if (nextHead == tail) {
        // Ring buffer full — this is extremely unlikely in practice
        FFE_LOG_WARN("Audio", "postCommand: command ring buffer full — command dropped");
        return false;
    }
    s_state.cmdRing[head % CMD_RING_CAPACITY] = cmd;
    s_state.cmdHead.store(nextHead, std::memory_order_release);
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool init(const bool headless) {
    if (s_state.initialised) {
        FFE_LOG_WARN("Audio", "init: audio system already initialised — call ignored");
        return false;
    }

    s_state.headless    = headless;
    s_state.deviceOpen  = false;
    s_state.initialised = true;

    // Reset atomic state
    s_state.cmdHead.store(0u, std::memory_order_relaxed);
    s_state.cmdTail.store(0u, std::memory_order_relaxed);
    s_state.masterVolume.store(1.0f, std::memory_order_relaxed);
    s_state.activeVoiceCount.store(0u, std::memory_order_relaxed);
    s_state.musicVolume.store(1.0f, std::memory_order_relaxed);
    s_state.musicPlaying.store(false, std::memory_order_relaxed);
    s_state.musicStream  = nullptr;
    s_state.musicLoop    = false;

    if (headless) {
        // Headless mode: no real device. loadSound() still works for path testing.
        FFE_LOG_INFO("Audio", "init: headless mode — no audio device opened");
        return true;
    }

    // Configure miniaudio device: float32 stereo output at 44100 Hz.
    s_state.deviceConfig = ma_device_config_init(ma_device_type_playback);
    s_state.deviceConfig.playback.format   = ma_format_f32;
    s_state.deviceConfig.playback.channels = 2u;
    s_state.deviceConfig.sampleRate        = 44100u;
    s_state.deviceConfig.dataCallback      = audioCallback;
    s_state.deviceConfig.pUserData         = nullptr;

    if (ma_device_init(nullptr, &s_state.deviceConfig, &s_state.device) != MA_SUCCESS) {
        FFE_LOG_ERROR("Audio", "init: ma_device_init() failed — audio unavailable");
        // Engine continues without audio; all playback becomes no-op.
        return false;
    }

    if (ma_device_start(&s_state.device) != MA_SUCCESS) {
        FFE_LOG_ERROR("Audio", "init: ma_device_start() failed — audio unavailable");
        ma_device_uninit(&s_state.device);
        return false;
    }

    s_state.deviceOpen = true;
    FFE_LOG_INFO("Audio", "init: audio device opened (44100 Hz, float32 stereo)");
    return true;
}

void shutdown() {
    if (!s_state.initialised) {
        return;
    }

    if (s_state.deviceOpen) {
        ma_device_stop(&s_state.device);
        ma_device_uninit(&s_state.device);
        s_state.deviceOpen = false;
    }

    // Device is now stopped — callback is not running. Safe to close music stream directly.
    if (s_state.musicStream != nullptr) {
        stb_vorbis_close(s_state.musicStream);
        s_state.musicStream = nullptr;
    }
    s_state.musicPlaying.store(false, std::memory_order_relaxed);

    // Free all loaded sound buffers.
    // LOW-4: acquire mutex so no in-flight callback can access freed buffers.
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);

        // Deactivate all voices first
        for (u32 i = 0u; i < MAX_AUDIO_VOICES; ++i) {
            s_state.voices[i].active = false;
            s_state.voices[i].buffer = nullptr;
        }
        s_state.activeVoiceCount.store(0u, std::memory_order_relaxed);

        // Free all PCM buffers (after voices deactivated — no use-after-free)
        for (u32 i = 0u; i < MAX_SOUNDS; ++i) {
            if (s_state.sounds[i].inUse && s_state.sounds[i].data != nullptr) {
                // stb_vorbis_decode_filename allocates with malloc; free() it
                free(s_state.sounds[i].data);
                s_state.sounds[i].data = nullptr;
                s_state.sounds[i].inUse = false;
            }
        }
    }

    s_state.initialised = false;
    s_state.nextId = 1u;
    FFE_LOG_INFO("Audio", "shutdown: audio system shut down");
}

SoundHandle loadSound(const char* const path, const char* const assetRoot) {
    // SEC-1: path safety check is FIRST — before any syscall.
    if (!isAudioPathSafe(path)) {
        FFE_LOG_ERROR("Audio", "loadSound: unsafe path rejected: \"%s\"",
                      path ? path : "(null)");
        return SoundHandle{0};
    }

    // Validate asset root
    if (!validateAssetRoot(assetRoot)) {
        FFE_LOG_ERROR("Audio", "loadSound: invalid assetRoot: \"%s\"",
                      assetRoot ? assetRoot : "(null)");
        return SoundHandle{0};
    }

    // HIGH-1: Check combined path length before concatenation to prevent
    // stack buffer overflow. This is the same check as ADR-005 HIGH-2.
    // Buffer is PATH_MAX+1 bytes; rootLen + '/' + pathLen + '\0' must fit.
    const size_t rootLen = strnlen(assetRoot, static_cast<size_t>(PATH_MAX));
    const size_t pathLen = strnlen(path, static_cast<size_t>(MAX_AUDIO_PATH));
    if (rootLen + 1u + pathLen + 1u > static_cast<size_t>(PATH_MAX) + 1u) {
        FFE_LOG_ERROR("Audio", "loadSound: path concatenation would overflow");
        return SoundHandle{0};
    }

    // Build full path: assetRoot + "/" + path
    char fullPath[PATH_MAX + 1];
    memcpy(fullPath, assetRoot, rootLen);
    fullPath[rootLen] = '/';
    memcpy(fullPath + rootLen + 1u, path, pathLen);
    fullPath[rootLen + 1u + pathLen] = '\0';

    // SEC-1 (continued): Canonicalise path to resolve symlinks and
    // any encoding tricks the string check missed.
    // TOCTOU note: there is an inherent race between canonicalizePath() and fopen().
    // This is accepted for FFE's local-asset threat model — a local attacker
    // with write access to the asset directory can place content there directly.
    // FFE is not a multi-user server.
    char canonPath[PATH_MAX + 1];
    if (!ffe::canonicalizePath(fullPath, canonPath, sizeof(canonPath))) {
        FFE_LOG_ERROR("Audio", "loadSound: canonicalizePath() failed for \"%s\"", fullPath);
        return SoundHandle{0};
    }

    // SEC-1 (continued): Verify the canonical path is within assetRoot.
    // This catches symlink escapes that realpath() resolved outside the root.
    if (strncmp(canonPath, assetRoot, rootLen) != 0) {
        FFE_LOG_ERROR("Audio", "loadSound: canonical path \"%s\" escapes assetRoot \"%s\"",
                      canonPath, assetRoot);
        return SoundHandle{0};
    }
    if (canonPath[rootLen] != '/' && canonPath[rootLen] != '\0') {
        FFE_LOG_ERROR("Audio", "loadSound: canonical path \"%s\" escapes assetRoot \"%s\"",
                      canonPath, assetRoot);
        return SoundHandle{0};
    }

    // SEC-2: Pre-decode file-size check via stat().
    // Rejects oversized files before the decoder spends CPU on them.
    {
        struct stat fileStat{};
        if (::stat(canonPath, &fileStat) != 0) {
            FFE_LOG_ERROR("Audio", "loadSound: stat() failed for \"%s\"", canonPath);
            return SoundHandle{0};
        }
        const u64 fileSize = static_cast<u64>(fileStat.st_size);
        if (fileSize > AUDIO_MAX_FILE_BYTES) {
            FFE_LOG_ERROR("Audio",
                          "loadSound: file too large (%" PRIu64 " bytes, max %" PRIu64 "): \"%s\"",
                          fileSize, AUDIO_MAX_FILE_BYTES, canonPath);
            return SoundHandle{0};
        }
    }

    // SEC-3: Format allowlist — detect by magic bytes, not file extension.
    // Only WAV (RIFF) and OGG (OggS) are accepted.
    u8 magic[4] = {0, 0, 0, 0};
    {
        FILE* const magicFile = ::fopen(canonPath, "rb");
        if (!magicFile) {
            FFE_LOG_ERROR("Audio", "loadSound: fopen() failed for \"%s\"", canonPath);
            return SoundHandle{0};
        }
        const size_t magicRead = ::fread(magic, 1u, 4u, magicFile);
        ::fclose(magicFile);
        if (magicRead < 4u) {
            FFE_LOG_ERROR("Audio", "loadSound: file too small (< 4 bytes): \"%s\"", canonPath);
            return SoundHandle{0};
        }
    }

    const bool isWAV = (magic[0] == 0x52u && magic[1] == 0x49u &&
                        magic[2] == 0x46u && magic[3] == 0x46u); // RIFF
    const bool isOGG = (magic[0] == 0x4Fu && magic[1] == 0x67u &&
                        magic[2] == 0x67u && magic[3] == 0x53u); // OggS

    if (!isWAV && !isOGG) {
        FFE_LOG_ERROR("Audio", "loadSound: unsupported audio format (not WAV or OGG): \"%s\"",
                      canonPath);
        return SoundHandle{0};
    }

    // --- Decode to float32 PCM ---
    float*  pcmData    = nullptr;
    int     frameCount = 0;
    int     channels   = 0;
    int     sampleRate = 0;

    if (isWAV) {
        // Use miniaudio's WAV decoder — outputs float32 directly.
        ma_decoder_config decoderCfg = ma_decoder_config_init(ma_format_f32, 0u, 0u);
        ma_decoder decoder;
        if (ma_decoder_init_file(canonPath, &decoderCfg, &decoder) != MA_SUCCESS) {
            FFE_LOG_ERROR("Audio", "loadSound: ma_decoder_init_file() failed for \"%s\"", canonPath);
            return SoundHandle{0};
        }

        channels   = static_cast<int>(decoder.outputChannels);
        sampleRate = static_cast<int>(decoder.outputSampleRate);

        ma_uint64 totalFrames = 0u;
        ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
        frameCount = static_cast<int>(totalFrames);

        // HIGH-2: Validate decoder outputs BEFORE cast to u64 multiplication.
        // A negative int from the decoder cast to u64 = 0xFFFFFFFFFFFFFFFF.
        // Validation must run first (SEC-5 before SEC-4).
        if (frameCount <= 0 || (channels != 1 && channels != 2) || sampleRate <= 0) {
            FFE_LOG_ERROR("Audio", "loadSound: WAV decoder returned invalid parameters "
                          "(frames=%d channels=%d rate=%d) for \"%s\"",
                          frameCount, channels, sampleRate, canonPath);
            ma_decoder_uninit(&decoder);
            return SoundHandle{0};
        }

        // SEC-4: decoded size check using u64 arithmetic (after SEC-5 validation).
        const u64 decodedBytes = static_cast<u64>(frameCount)
                               * static_cast<u64>(channels)
                               * sizeof(float);
        if (decodedBytes > AUDIO_MAX_DECODED_BYTES) {
            FFE_LOG_ERROR("Audio",
                          "loadSound: decoded WAV size %" PRIu64 " bytes exceeds max %" PRIu64
                          " for \"%s\"", decodedBytes, AUDIO_MAX_DECODED_BYTES, canonPath);
            ma_decoder_uninit(&decoder);
            return SoundHandle{0};
        }

        pcmData = static_cast<float*>(malloc(decodedBytes));
        if (!pcmData) {
            FFE_LOG_ERROR("Audio", "loadSound: malloc failed for decoded WAV data");
            ma_decoder_uninit(&decoder);
            return SoundHandle{0};
        }

        ma_uint64 framesRead = 0u;
        ma_decoder_read_pcm_frames(&decoder, pcmData, static_cast<ma_uint64>(frameCount),
                                   &framesRead);
        ma_decoder_uninit(&decoder);

        if (framesRead == 0u) {
            FFE_LOG_ERROR("Audio", "loadSound: ma_decoder_read_pcm_frames() read 0 frames for \"%s\"",
                          canonPath);
            free(pcmData);
            return SoundHandle{0};
        }
        frameCount = static_cast<int>(framesRead);

    } else {
        // isOGG — decode via stb_vorbis.
        //
        // stb_vorbis_decode_filename returns interleaved i16 (short) PCM samples.
        // We decode to a temporary short buffer, validate, then convert to float32.
        //
        // MEDIUM-2: stb_vorbis v1.22 codebook heap allocation is not bounded by
        // the file size limit — a crafted OGG can allocate more working memory
        // than the file size implies. This is accepted for FFE's local-asset threat
        // model (not a mod-download server with untrusted OGG content).
        // stb_vorbis version: 1.22 (third_party/stb_vorbis.c, 2026-03-06).
        // Ref: ADR-006-security-review.md MEDIUM-2.
        short* shortData = nullptr;
        frameCount = stb_vorbis_decode_filename(canonPath, &channels, &sampleRate, &shortData);

        // HIGH-2: Validate decoder outputs BEFORE cast to u64 multiplication.
        // A negative int from the decoder cast to u64 = 0xFFFFFFFFFFFFFFFF.
        // Validation must run first (SEC-5 before SEC-4).
        if (frameCount <= 0 || (channels != 1 && channels != 2) || sampleRate <= 0) {
            FFE_LOG_ERROR("Audio", "loadSound: OGG decoder returned invalid parameters "
                          "(frames=%d channels=%d rate=%d) for \"%s\"",
                          frameCount, channels, sampleRate, canonPath);
            if (shortData) { free(shortData); }
            return SoundHandle{0};
        }

        // SEC-4: decoded size check using u64 arithmetic (after SEC-5 validation).
        // stb_vorbis_decode_filename returns short (i16) samples.
        // Check the short-buffer size as the actual allocation.
        const u64 shortBytes = static_cast<u64>(frameCount)
                             * static_cast<u64>(channels)
                             * sizeof(short);
        if (shortBytes > AUDIO_MAX_DECODED_BYTES) {
            FFE_LOG_ERROR("Audio",
                          "loadSound: decoded OGG size %" PRIu64 " bytes exceeds max %" PRIu64
                          " for \"%s\"", shortBytes, AUDIO_MAX_DECODED_BYTES, canonPath);
            free(shortData);
            return SoundHandle{0};
        }

        // Convert i16 PCM to float32. Allocate a float buffer.
        const u64 totalSamples = static_cast<u64>(frameCount) * static_cast<u64>(channels);
        const u64 floatBytes   = totalSamples * sizeof(float);
        pcmData = static_cast<float*>(malloc(floatBytes));
        if (!pcmData) {
            FFE_LOG_ERROR("Audio", "loadSound: malloc failed for OGG float conversion");
            free(shortData);
            return SoundHandle{0};
        }

        for (u64 s = 0u; s < totalSamples; ++s) {
            pcmData[s] = static_cast<float>(shortData[s]) / 32768.0f;
        }
        free(shortData);
    }

    // --- Register into the sound table ---
    // Find a free slot (linear scan of fixed array — O(MAX_SOUNDS), one-time cost)
    u32 slotIndex = MAX_SOUNDS; // sentinel = not found
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);

        for (u32 i = 0u; i < MAX_SOUNDS; ++i) {
            if (!s_state.sounds[i].inUse) {
                slotIndex = i;
                break;
            }
        }

        if (slotIndex == MAX_SOUNDS) {
            FFE_LOG_ERROR("Audio", "loadSound: sound table full (max %u sounds)",
                          MAX_SOUNDS);
            free(pcmData);
            return SoundHandle{0};
        }

        SoundBuffer& buf = s_state.sounds[slotIndex];
        buf.data        = pcmData;
        buf.frameCount  = static_cast<u64>(frameCount);
        buf.channels    = static_cast<u32>(channels);
        buf.sampleRate  = static_cast<u32>(sampleRate);
        buf.inUse       = true;
        // Store validated canonical path so playMusic() can open a stb_vorbis
        // stream without repeating path validation.
        const size_t canonLen = strnlen(canonPath, static_cast<size_t>(PATH_MAX));
        memcpy(buf.canonPath, canonPath, canonLen + 1u);  // +1 for null terminator
    }

    // The handle id is (slotIndex + 1): id 0 is invalid, id 1 maps to slot 0.
    const u32 handleId = slotIndex + 1u;
    FFE_LOG_INFO("Audio", "loadSound: loaded \"%s\" (id=%u, frames=%" PRIu64 ", ch=%d, rate=%d)",
                 canonPath, handleId, static_cast<u64>(frameCount), channels, sampleRate);
    return SoundHandle{handleId};
}

SoundHandle loadMusic(const char* const path, const char* const assetRoot) {
    // Lightweight music loader: validates path and stores canonical path for
    // streaming via playMusic(). Does NOT decode the file to PCM — skips the
    // AUDIO_MAX_DECODED_BYTES limit that blocks large music files in loadSound().

    // SEC-1: path safety check is FIRST — before any syscall.
    if (!isAudioPathSafe(path)) {
        FFE_LOG_ERROR("Audio", "loadMusic: unsafe path rejected: \"%s\"",
                      path ? path : "(null)");
        return SoundHandle{0};
    }

    if (!validateAssetRoot(assetRoot)) {
        FFE_LOG_ERROR("Audio", "loadMusic: invalid assetRoot: \"%s\"",
                      assetRoot ? assetRoot : "(null)");
        return SoundHandle{0};
    }

    // HIGH-1: Check combined path length before concatenation.
    const size_t rootLen = strnlen(assetRoot, static_cast<size_t>(PATH_MAX));
    const size_t pathLen = strnlen(path, static_cast<size_t>(MAX_AUDIO_PATH));
    if (rootLen + 1u + pathLen + 1u > static_cast<size_t>(PATH_MAX) + 1u) {
        FFE_LOG_ERROR("Audio", "loadMusic: path concatenation would overflow");
        return SoundHandle{0};
    }

    // Build full path: assetRoot + "/" + path
    char fullPath[PATH_MAX + 1];
    memcpy(fullPath, assetRoot, rootLen);
    fullPath[rootLen] = '/';
    memcpy(fullPath + rootLen + 1u, path, pathLen);
    fullPath[rootLen + 1u + pathLen] = '\0';

    // Canonicalise path.
    char canonPath[PATH_MAX + 1];
    if (!ffe::canonicalizePath(fullPath, canonPath, sizeof(canonPath))) {
        FFE_LOG_ERROR("Audio", "loadMusic: canonicalizePath() failed for \"%s\"", fullPath);
        return SoundHandle{0};
    }

    // Verify the canonical path is within assetRoot.
    if (strncmp(canonPath, assetRoot, rootLen) != 0) {
        FFE_LOG_ERROR("Audio", "loadMusic: canonical path \"%s\" escapes assetRoot \"%s\"",
                      canonPath, assetRoot);
        return SoundHandle{0};
    }
    if (canonPath[rootLen] != '/' && canonPath[rootLen] != '\0') {
        FFE_LOG_ERROR("Audio", "loadMusic: canonical path \"%s\" escapes assetRoot \"%s\"",
                      canonPath, assetRoot);
        return SoundHandle{0};
    }

    // SEC-2: Pre-decode file-size check via stat().
    {
        struct stat fileStat{};
        if (::stat(canonPath, &fileStat) != 0) {
            FFE_LOG_ERROR("Audio", "loadMusic: stat() failed for \"%s\"", canonPath);
            return SoundHandle{0};
        }
        const u64 fileSize = static_cast<u64>(fileStat.st_size);
        if (fileSize > AUDIO_MAX_FILE_BYTES) {
            FFE_LOG_ERROR("Audio",
                          "loadMusic: file too large (%" PRIu64 " bytes, max %" PRIu64 "): \"%s\"",
                          fileSize, AUDIO_MAX_FILE_BYTES, canonPath);
            return SoundHandle{0};
        }
    }

    // SEC-3: Format allowlist — detect by magic bytes.
    u8 magic[4] = {0, 0, 0, 0};
    {
        FILE* const magicFile = ::fopen(canonPath, "rb");
        if (!magicFile) {
            FFE_LOG_ERROR("Audio", "loadMusic: fopen() failed for \"%s\"", canonPath);
            return SoundHandle{0};
        }
        const size_t magicRead = ::fread(magic, 1u, 4u, magicFile);
        ::fclose(magicFile);
        if (magicRead < 4u) {
            FFE_LOG_ERROR("Audio", "loadMusic: file too small (< 4 bytes): \"%s\"", canonPath);
            return SoundHandle{0};
        }
    }

    const bool isWAV = (magic[0] == 0x52u && magic[1] == 0x49u &&
                        magic[2] == 0x46u && magic[3] == 0x46u);
    const bool isOGG = (magic[0] == 0x4Fu && magic[1] == 0x67u &&
                        magic[2] == 0x67u && magic[3] == 0x53u);

    if (!isWAV && !isOGG) {
        FFE_LOG_ERROR("Audio", "loadMusic: unsupported audio format (not WAV or OGG): \"%s\"",
                      canonPath);
        return SoundHandle{0};
    }

    // Register into sound table — store path only, no decoded PCM.
    u32 slotIndex = MAX_SOUNDS;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);

        for (u32 i = 0u; i < MAX_SOUNDS; ++i) {
            if (!s_state.sounds[i].inUse) {
                slotIndex = i;
                break;
            }
        }

        if (slotIndex == MAX_SOUNDS) {
            FFE_LOG_ERROR("Audio", "loadMusic: sound table full (max %u sounds)", MAX_SOUNDS);
            return SoundHandle{0};
        }

        SoundBuffer& buf = s_state.sounds[slotIndex];
        buf.data       = nullptr;   // no decoded PCM — streaming only
        buf.frameCount = 0u;
        buf.channels   = 0u;
        buf.sampleRate = 0u;
        buf.inUse      = true;
        const size_t canonLen = strnlen(canonPath, static_cast<size_t>(PATH_MAX));
        memcpy(buf.canonPath, canonPath, canonLen + 1u);
    }

    const u32 handleId = slotIndex + 1u;
    FFE_LOG_INFO("Audio", "loadMusic: registered \"%s\" for streaming (id=%u)",
                 canonPath, handleId);
    return SoundHandle{handleId};
}

void unloadSound(const SoundHandle handle) {
    if (!isValid(handle)) {
        return; // Safe no-op for invalid handles
    }

    const u32 slotIndex = handle.id - 1u;
    if (slotIndex >= MAX_SOUNDS) {
        return;
    }

    // LOW-4: Deactivate voice AND free PCM buffer under m_mutex in the SAME
    // critical section. Splitting these operations would create a window where
    // the audio callback could read from a freed buffer. Never release the mutex
    // between deactivating the voice and freeing the PCM data.
    std::lock_guard<std::mutex> lock(s_state.mutex);

    SoundBuffer& buf = s_state.sounds[slotIndex];
    if (!buf.inUse) {
        return;
    }

    // 1. Deactivate any voice using this buffer (must happen BEFORE free)
    for (u32 v = 0u; v < MAX_AUDIO_VOICES; ++v) {
        if (s_state.voices[v].active && s_state.voices[v].buffer == &buf) {
            s_state.voices[v].active = false;
            s_state.voices[v].buffer = nullptr;
            s_state.activeVoiceCount.fetch_sub(1u, std::memory_order_relaxed);
        }
    }

    // 2. Free PCM buffer (safe: no active voice can now reference it)
    if (buf.data != nullptr) {
        free(buf.data);
        buf.data = nullptr;
    }

    buf.frameCount    = 0u;
    buf.channels      = 0u;
    buf.sampleRate    = 0u;
    buf.inUse         = false;
    buf.canonPath[0]  = '\0';
}

void playSound(const SoundHandle handle, const float volume) {
    if (!isValid(handle)) {
        return;
    }
    if (!s_state.initialised || s_state.headless || !s_state.deviceOpen) {
        return; // No-op in headless mode or if device failed to open
    }

    // LOW-5: per-instance volume must also pass through clampVolume() — not
    // just master volume. This matches the volume clamping requirement in SEC-6.
    const float clampedVolume = clampVolume(volume);

    AudioCommand cmd;
    cmd.type    = AudioCommand::Type::PLAY_SOUND;
    cmd.soundId = handle.id;
    cmd.volume  = clampedVolume;
    postCommand(cmd);
}

void playMusic(const SoundHandle handle, const bool loop) {
    if (!isValid(handle)) {
        return;
    }
    if (!s_state.initialised || s_state.headless || !s_state.deviceOpen) {
        return;
    }

    AudioCommand cmd;
    cmd.type    = AudioCommand::Type::PLAY_MUSIC;
    cmd.soundId = handle.id;
    cmd.flags   = loop ? 0x01u : 0x00u;
    cmd.volume  = 0.0f; // unused for PLAY_MUSIC
    postCommand(cmd);
}

void stopMusic() {
    if (!s_state.initialised || s_state.headless || !s_state.deviceOpen) {
        return;
    }

    AudioCommand cmd;
    cmd.type = AudioCommand::Type::STOP_MUSIC;
    postCommand(cmd);
}

void setMusicVolume(const float volume) {
    const float clamped = clampVolume(volume);
    s_state.musicVolume.store(clamped, std::memory_order_relaxed);

    if (s_state.initialised && !s_state.headless && s_state.deviceOpen) {
        AudioCommand cmd;
        cmd.type   = AudioCommand::Type::SET_MUSIC_VOLUME;
        cmd.volume = clamped;
        postCommand(cmd);
    }
}

float getMusicVolume() {
    return s_state.musicVolume.load(std::memory_order_relaxed);
}

bool isMusicPlaying() {
    return s_state.musicPlaying.load(std::memory_order_relaxed);
}

void setMasterVolume(const float volume) {
    const float clamped = clampVolume(volume);
    if (!std::isfinite(volume) || volume < 0.0f || volume > 1.0f) {
        FFE_LOG_WARN("Audio", "setMasterVolume: value %f out of range [0,1] — clamped to %f",
                     static_cast<double>(volume), static_cast<double>(clamped));
    }
    s_state.masterVolume.store(clamped, std::memory_order_relaxed);

    // If the device is running, also post via command so the callback sees it
    // on the next tick (belt-and-suspenders with the atomic store above).
    if (s_state.initialised && !s_state.headless && s_state.deviceOpen) {
        AudioCommand cmd;
        cmd.type   = AudioCommand::Type::SET_MASTER_VOLUME;
        cmd.volume = clamped;
        postCommand(cmd);
    }
}

float getMasterVolume() {
    return s_state.masterVolume.load(std::memory_order_relaxed);
}

bool isAudioAvailable() {
    return s_state.initialised && !s_state.headless && s_state.deviceOpen;
}

u32 getActiveVoiceCount() {
    if (!s_state.initialised) { return 0u; }
    // M-2 fix: activeVoiceCount is maintained as a std::atomic<u32> alongside
    // voice pool operations. No mutex required — safe to call per-frame.
    return s_state.activeVoiceCount.load(std::memory_order_relaxed);
}

} // namespace ffe::audio
