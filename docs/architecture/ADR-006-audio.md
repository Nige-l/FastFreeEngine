# ADR-006: Audio Subsystem

**Status:** PROPOSED — awaiting security-auditor shift-left review before implementation begins
**Author:** architect
**Date:** 2026-03-06
**Tiers:** LEGACY (primary), STANDARD, MODERN — RETRO explicitly not supported
**Security Review Required:** YES — this ADR touches file I/O and external data parsing (CLAUDE.md Section 5, attack surface)

---

## 1. Problem Statement

FFE has no audio support. Games built on FFE cannot play sound effects or music. Audio is one of the most impactful features for player experience — a game that runs silently feels broken no matter how well it renders.

This ADR introduces a minimal, first-version audio subsystem at `engine/audio/`. It covers:

- Loading OGG/WAV audio files from disk into memory buffers
- Playing one-shot sound effects (fire-and-forget)
- Playing and looping a single background music track with streaming from disk
- Per-channel volume control (master, sound effects, music)
- ECS integration strategy and thread-safety model
- Security constraints for file I/O and format parsing

A placeholder directory exists at `engine/audio/`. No audio code exists today.

---

## 2. Library Decision

### 2.1 Decision: miniaudio

Use **miniaudio** (single public-domain C header, `miniaudio.h`) as the audio backend.

### 2.2 Rationale

**miniaudio vs OpenAL Soft:**

| Concern | miniaudio | OpenAL Soft |
|---------|-----------|-------------|
| License | Public domain (Unlicense/MIT-0) | LGPL 2.0 — requires attribution and dynamic linking care |
| Integration | Single header in `third_party/` — same pattern as stb_image and glad | vcpkg dependency, runtime shared library dependency on some platforms |
| Dependency count | Zero | Depends on platform audio APIs + driver-level compatibility |
| Build complexity | None beyond `#define MINIAUDIO_IMPLEMENTATION` in one `.cpp` | CMake find_package, vcpkg entry, link flags |
| LEGACY hardware fit | Designed to run on resource-constrained hardware | Same effective hardware support, but heavier abstraction layer |
| FFE project pattern | Follows the established single-header pattern (stb_image, glad) | Breaks the established pattern by requiring vcpkg |
| Exception safety | Pure C library — no exceptions possible | C API — same |
| Format support | OGG (via stb_vorbis — also single header) and WAV natively | Depends on AL extension + separate libogg/libvorbis |

The single-header approach is consistent with how FFE handles stb_image and glad. It keeps the project buildable on any machine without vcpkg setup beyond the core dependencies already in `vcpkg.json`. An MIT-licensed engine should not carry LGPL audio library baggage into every project built on it.

miniaudio provides a device abstraction layer over the platform audio API (ALSA/PulseAudio/JACK on Linux, CoreAudio on macOS, WASAPI/DirectSound on Windows). It handles the audio callback thread internally.

### 2.3 Dependency Note (CLAUDE.md — must flag explicitly)

- `miniaudio.h` is embedded in `third_party/miniaudio.h`
- `stb_vorbis.h` (for OGG decoding, also a public-domain single header from the stb family) is embedded in `third_party/stb_vorbis.h` — **this is not a new vcpkg dependency**
- Neither is added to `vcpkg.json` (same pattern as stb_image and glad)
- The commit message must state: `feat(audio): add audio subsystem via miniaudio + stb_vorbis (headers in third_party/)`
- No other new dependencies are introduced by this ADR

---

## 3. Architecture

### 3.1 Global Singleton vs ECS Component System

**Decision: global module singleton in `ffe::audio` namespace, with optional ECS integration for spatial audio in a future ADR.**

Rationale:

The renderer uses a global RHI singleton (`ffe::rhi::init`, `ffe::rhi::shutdown`, etc.) because rendering state is fundamentally global — there is one GPU, one framebuffer, one context. Audio is structurally identical: there is one audio device, one mixer, one output stream. The same design that makes the renderer's API clean and testable applies directly to audio.

An ECS component approach (attach `AudioEmitter` to entities) is appropriate for spatial/3D audio where the audio position depends on entity position. That is a STANDARD/MODERN tier concern. For LEGACY-tier 2D games, the dominant audio patterns are:

1. "Play a sound effect when the player jumps" — no spatial component
2. "Play background music" — definitely not an entity

Forcing a component model on these use cases would require users to create a dummy entity just to play a sound, which is worse ergonomics than a plain function call.

**ECS integration path for spatial audio** is reserved for a future ADR targeting STANDARD tier.

### 3.2 File Layout

```
engine/audio/
    audio.h          — public API (all types, handle, functions)
    audio.cpp        — implementation (miniaudio init/shutdown, all audio functions)
    .context.md      — LLM-facing documentation (owned by api-designer)
```

This mirrors the established flat-file pattern: `engine/renderer/rhi.h` + `engine/renderer/opengl/rhi_opengl.cpp` uses a backend subfolder, but the texture loader (`texture_loader.h` + `texture_loader.cpp`) uses the simpler two-file form. Audio v1 has no backend abstraction requirement — miniaudio already abstracts the platform. If a Vulkan/spatial backend is added later, a subfolder structure can be introduced then.

### 3.3 Threading Model

miniaudio runs its audio callback on a background thread managed by the device. This is required for low-latency audio — the callback must always have data available, regardless of what the game loop is doing.

The public API functions (`playSound`, `playMusic`, `setMasterVolume`, etc.) are called from the main thread. They must communicate with the audio thread safely.

**Synchronisation strategy:**

- All per-sound state (handle → buffer mapping, active voice list) is protected by a single `std::mutex` (the `m_mutex` lock in the `AudioSystem` implementation struct).
- The audio callback acquires this mutex for the duration of mixing. Main-thread API calls also acquire it.
- This is a coarse-lock design. It is safe, predictable, and appropriate for LEGACY-tier game audio (32 simultaneous sounds, WAV/OGG data already decoded into memory, no per-sample computation beyond volume scaling).
- If profiling reveals that the main thread is blocked on the audio lock, this can be replaced with a lock-free ring buffer or double-buffering scheme in a future pass. That optimisation is deferred — CLAUDE.md Section 3 says "measure before optimising."

**Music streaming** runs on the audio callback thread. The streaming state (file cursor, decode buffer) is owned by the audio module and only touched by the audio callback under the mutex. Main-thread calls to `playMusic`, `stopMusic` post a command that the callback thread processes at the start of its next invocation (see Section 3.4).

### 3.4 Command Queue for Thread-Safe Control

The main thread never writes directly to audio voice state. Instead, it posts `AudioCommand` entries into a fixed-capacity ring buffer (capacity: 64 commands). The audio callback drains this queue at the start of each callback invocation before mixing.

```
struct AudioCommand {
    enum class Type : u8 {
        PLAY_SOUND,
        PLAY_MUSIC,
        STOP_MUSIC,
        SET_MASTER_VOLUME,
        SET_SOUND_VOLUME,
        SET_MUSIC_VOLUME,
    };
    Type type;
    SoundHandle handle;    // PLAY_SOUND, PLAY_MUSIC
    float volume;          // SET_*_VOLUME
    bool loop;             // PLAY_MUSIC
};
```

The ring buffer uses a single producer (main thread), single consumer (audio callback) pattern with `std::atomic<u32>` head and tail indices. No mutex is required for the ring buffer itself. This means the main thread never blocks waiting for the audio thread to process a command.

If the ring buffer is full (64 outstanding commands — extremely unlikely in practice), `postCommand` logs a warning and drops the command. This is acceptable for game audio, where a dropped "play footstep" is inaudible.

---

## 4. Public API

### 4.1 Header: `engine/audio/audio.h`

```cpp
#pragma once

#include "core/types.h"

namespace ffe::audio {

// ---------------------------------------------------------------------------
// SoundHandle — opaque handle to a loaded sound buffer.
// A handle with id == 0 is invalid (not loaded or load failed).
// ---------------------------------------------------------------------------
struct SoundHandle {
    u32 id = 0;
};

// Validity helper — mirrors the rhi::isValid() pattern.
inline bool isValid(SoundHandle h) { return h.id != 0; }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Initialize the audio subsystem. Call once, from the main thread, before
// any other audio function. Creates the miniaudio device and starts the
// audio callback thread.
//
// Returns false if the audio device could not be opened (e.g., no audio
// hardware present). The engine continues without audio in this case;
// all subsequent audio calls become no-ops.
//
// headless: if true, skip device creation entirely (for CI/headless tests).
//
// Thread safety: call from main thread only.
bool initAudio(bool headless = false);

// Shut down the audio subsystem. Stops the audio thread, frees all loaded
// sounds, closes the device. Call once at shutdown, after all other audio
// calls.
//
// Thread safety: call from main thread only, with no concurrent audio calls.
void shutdownAudio();

// ---------------------------------------------------------------------------
// Asset root (path traversal protection — same model as texture_loader.h)
// ---------------------------------------------------------------------------

// Set the root directory for all loadSound() calls.
// Must be an absolute path to an existing directory.
// Write-once: once set successfully, further calls are silently ignored.
// Returns false if the path is invalid, non-absolute, or does not exist.
//
// Thread safety: call from main thread at startup, before any loadSound().
bool setAudioAssetRoot(const char* absolutePath);

// Query the configured audio asset root (for diagnostics and tests).
// Returns an empty string if setAudioAssetRoot() has not been called.
const char* getAudioAssetRoot();

// ---------------------------------------------------------------------------
// Loading and unloading
// ---------------------------------------------------------------------------

// Load an audio file into memory and return a handle.
//
// path must be a relative path with no traversal sequences (see SEC-1).
// Supported formats: WAV, OGG (Vorbis). Format detected by magic bytes.
//
// One-shot sound effects are decoded fully into PCM at load time.
// Long tracks (> AUDIO_STREAMING_THRESHOLD_BYTES) that will be played via
// playMusic() should use the streaming path — pass to playMusic() directly.
// For simplicity in v1, ALL sounds are decoded fully at load time.
// Streaming from disk is deferred to v2.
//
// Returns SoundHandle{0} on failure. Logs the error.
//
// Thread safety: call from main thread only (file I/O + heap allocation).
// Performance: NOT a per-frame function. Call at scene load time.
SoundHandle loadSound(const char* path);

// Convenience overload: load from an explicit asset root without changing
// the global root. Same path validation rules apply.
SoundHandle loadSound(const char* path, const char* assetRoot);

// Release all resources associated with a loaded sound.
// After this call, handle.id must not be used.
// Safe to call with SoundHandle{0} — no-op.
// If the sound is currently playing, it is stopped before unloading.
//
// Thread safety: call from main thread only.
void unloadSound(SoundHandle handle);

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

// Play a one-shot sound effect. Fire and forget — no way to stop it
// individually once started. If 32 sounds are already playing, the new
// sound is dropped and a warning is logged (not an error).
//
// volume: per-instance volume multiplier [0.0, 1.0]. Clamped.
//
// Thread safety: may be called from main thread. DO NOT call from within
// the audio callback.
void playSound(SoundHandle handle, float volume = 1.0f);

// Play a music track on the dedicated music channel. Only one music track
// plays at a time. Starting a new track while one is playing stops the
// previous track immediately.
//
// loop: if true, the track loops indefinitely until stopMusic() is called
//       or a new playMusic() call replaces it.
//
// Thread safety: main thread only.
void playMusic(SoundHandle handle, bool loop = true);

// Stop the currently playing music track. No-op if no music is playing.
//
// Thread safety: main thread only.
void stopMusic();

// Pause the current music track. Playback resumes from the same position
// when resumeMusic() is called.
//
// Thread safety: main thread only.
void pauseMusic();

// Resume a paused music track. No-op if music is not paused.
//
// Thread safety: main thread only.
void resumeMusic();

// ---------------------------------------------------------------------------
// Volume control
// ---------------------------------------------------------------------------

// Set the master volume. Applied to all audio output.
// Clamped to [0.0, 1.0]. Values outside this range are clamped and a
// warning is logged.
void setMasterVolume(float volume);

// Set the sound effects channel volume. Multiplied with master volume.
// Clamped to [0.0, 1.0].
void setSoundVolume(float volume);

// Set the music channel volume. Multiplied with master volume.
// Clamped to [0.0, 1.0].
void setMusicVolume(float volume);

// Query current volume values (useful for UI sliders).
float getMasterVolume();
float getSoundVolume();
float getMusicVolume();

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

// Returns the number of sound voices currently active (0–MAX_AUDIO_VOICES).
u32 getActiveVoiceCount();

// Returns true if the audio device was successfully opened.
// Returns false if initAudio() failed or was called with headless=true.
bool isAudioAvailable();

} // namespace ffe::audio
```

### 4.2 Constants

```cpp
// Maximum simultaneous sound voices (sound effects only; music is separate).
// This is a LEGACY-tier budget. On STANDARD/MODERN the same limit applies
// until a spatial audio ADR raises it.
static constexpr u32 MAX_AUDIO_VOICES = 32u;

// Maximum path length for audio asset paths (matches texture_loader).
static constexpr u32 AUDIO_MAX_PATH = 4096u;

// Maximum decoded audio data size per sound in bytes.
// Prevents loading a 2-hour WAV file into RAM as a "sound effect".
// 10 MB covers ~30 seconds of uncompressed 44.1kHz stereo 16-bit PCM.
// Longer content (music) should be loaded with playMusic() which in v2
// will stream from disk. In v1 it is clamped to this limit.
static constexpr u64 AUDIO_MAX_DECODED_BYTES = 10ull * 1024ull * 1024ull;

// Maximum raw audio file size accepted before decoding begins.
// OGG files are typically 10:1 compression; WAV is uncompressed.
// 32 MB allows for large uncompressed WAV sound effects.
static constexpr u64 AUDIO_MAX_FILE_BYTES = 32ull * 1024ull * 1024ull;
```

### 4.3 ECS Integration (Non-API)

Audio is a global service, not an ECS component. Game code calls `ffe::audio::playSound()` directly from within systems. There is no `AudioEmitter` component in v1.

Example of correct usage inside an ECS system:

```cpp
void playerJumpSystem(ffe::World& world, float dt) {
    if (ffe::isActionPressed(g_actionJump)) {
        ffe::audio::playSound(g_jumpSound);
    }
}
```

This is intentional: the call is in the system's hot path only on the frame the jump happens (i.e., one call per player jump, not 60 calls per second).

---

## 5. Security Constraints

This section is the primary input for the security-auditor shift-left review. Every constraint listed here MUST be enforced in the implementation before any merge.

### SEC-1: Path Traversal Prevention

Identical policy to ADR-005 (texture loading). Path validation is the FIRST operation in `loadSound()`. No filesystem call is made before the path passes validation.

```
bool isAudioPathSafe(const char* path):
    if path is null                          → reject
    if path is empty string                  → reject
    if path[0] == '/' or path[0] == '\\'    → reject (absolute Unix/Windows path)
    if path[1] == ':'                        → reject (drive letter: "C:")
    if path contains "../"                   → reject
    if path contains "..\\"                 → reject
    if path contains "/.."                   → reject
    if path contains "\\.."                 → reject
    if strlen(path) >= AUDIO_MAX_PATH        → reject
    return true
```

After the string check, construct the full path as `assetRoot + "/" + path`, call `realpath()` to canonicalise it (resolving symlinks and any encoding tricks the string check missed), and verify the canonical path begins with the asset root prefix. If `realpath()` fails, return failure immediately. Only after both checks pass does `fopen` execute.

The asset root is write-once: `setAudioAssetRoot()` cannot be redirected after initial successful configuration.

### SEC-2: File Size Limit Before Decoding

Before opening the file for decode, `stat()` the canonical path and check the file size:

```cpp
struct stat st;
if (stat(canonicalPath, &st) != 0) → reject (file not found)
if (static_cast<u64>(st.st_size) > AUDIO_MAX_FILE_BYTES) → reject, log error
```

This prevents a large file from being opened and partially read before a size-based rejection.

### SEC-3: Format Allowlist

Only WAV and OGG are accepted. Format is detected by magic bytes, not file extension:

- WAV: first 4 bytes are `RIFF` (`0x52 0x49 0x46 0x46`)
- OGG: first 4 bytes are `OggS` (`0x4F 0x67 0x67 0x53`)

Any other magic byte pattern is rejected before passing the file to the decoder. This is a defence-in-depth measure in addition to the decoder's own format validation.

```cpp
u8 magic[4];
// read 4 bytes from file
if (isWAV(magic))  → decode via miniaudio WAV path
if (isOGG(magic))  → decode via stb_vorbis
else               → reject, log "unsupported audio format"
```

### SEC-4: Decoded Data Size Limit

After decoding, check the decoded PCM size before accepting it into the voice pool:

```cpp
const u64 decodedBytes = static_cast<u64>(frameCount)
                       * static_cast<u64>(channels)
                       * static_cast<u64>(sizeof(i16)); // 16-bit PCM
if (decodedBytes > AUDIO_MAX_DECODED_BYTES) → free decoded data, reject
```

Arithmetic uses `u64` throughout. `frameCount` and `channels` from the decoder are `int` — cast before multiply.

### SEC-5: Decoder Output Validation

Decoder outputs are treated as untrusted:

- Check returned PCM pointer is non-null
- Check `frameCount > 0`
- Check `channels` is 1 or 2 (mono or stereo) — reject anything else
- Check `sampleRate > 0` and is a recognised value (8000, 11025, 22050, 44100, 48000)

### SEC-6: Volume Clamping

All volume inputs are clamped to `[0.0f, 1.0f]` before storage or use. Values outside this range log a warning and are clamped — they are not rejected as errors. `NaN` and `inf` are detected via `std::isfinite()` and replaced with `0.0f`.

```cpp
float clampVolume(float v) {
    if (!std::isfinite(v)) return 0.0f;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
```

### SEC-7: No Per-Frame File I/O

`loadSound()` is explicitly documented as a load-time-only function. It performs string validation, `stat()`, `realpath()`, `fopen()`, and full PCM decode. None of these are acceptable per-frame. The implementation makes no attempt to optimise for repeated call frequency. If `loadSound()` appears on a profiler hot path, that is a caller bug.

### SEC-8: Audio Callback Thread Safety

The audio callback runs on a thread managed by miniaudio. It accesses:

- The voice pool (active sounds, PCM buffers, playback cursors)
- The command queue (ring buffer, lock-free)
- The music state (buffer, cursor, loop flag)
- The volume values (`std::atomic<float>` for master/sfx/music volumes)

Volume values are stored as `std::atomic<float>` with `std::memory_order_relaxed` (reads in callback, writes from main thread). No mutex is needed for volume changes.

Voice pool and music state are protected by `m_mutex`. The callback holds this mutex only for the duration of mixing. Main-thread API calls hold this mutex only long enough to modify state. Long-duration file I/O in `loadSound()` does NOT hold the mutex — the mutex is only acquired at the end to register the loaded sound into the handle table.

The command ring buffer uses lock-free atomic indices (`std::atomic<u32>` head/tail). The main thread writes commands; the callback drains them. No mutex on the ring buffer itself.

### SEC-9: Lua Sandbox Boundary

When the Lua scripting layer is extended to expose audio (a future ADR), the audio API exposed to Lua MUST:

- Accept only string paths through `loadSound()`, never raw integers or pointers
- Validate path safety before any C++ call (Lua layer is untrusted; do not rely on callers to call `loadSound()` correctly)
- Not expose `initAudio()`, `shutdownAudio()`, or `setAudioAssetRoot()` to Lua (these are engine-lifecycle functions, not game functions)

This constraint is documented here so that the scripting integration ADR inherits it.

---

## 6. Tier Support

| Tier | Supported | Notes |
|------|-----------|-------|
| RETRO | **No** | Not supported. RETRO (~2005) hardware may lack a reliable audio API compatible with miniaudio. Adding audio to RETRO would require additional testing infrastructure that does not exist and is out of scope for FFE's current target platforms. Games targeting RETRO have no audio. This is an explicit limitation, not an oversight. |
| LEGACY | **Yes** — primary target | 44.1 kHz stereo output, OGG/WAV formats, up to 32 simultaneous voices. miniaudio's ALSA/CoreAudio/WASAPI backends are stable on 2012-era hardware. |
| STANDARD | **Yes** | Same as LEGACY in v1. Streaming from disk (for music > AUDIO_MAX_DECODED_BYTES) is planned for v2 as a STANDARD-tier enhancement. |
| MODERN | **Yes** | Same as STANDARD in v1. |

**No feature may silently degrade performance on a lower tier (CLAUDE.md Section 2).** The audio subsystem runs on its own thread. The performance budget impact on the game loop is: one mutex acquisition per `playSound()` call (on game-event boundaries, not per-frame) plus occasional atomic reads for volume. This is acceptable on LEGACY hardware.

---

## 7. Performance Constraints

Per CLAUDE.md Section 3:

- **No per-frame heap allocations.** `loadSound()` allocates on the heap (decoded PCM buffer). `playSound()` does not allocate — it places a voice into a pre-allocated voice pool (fixed array of `MAX_AUDIO_VOICES` voice slots).
- **No virtual functions in hot paths.** The audio callback calls a static function pointer. No virtual dispatch.
- **No `std::function` in hot paths.** The callback registration with miniaudio uses a plain function pointer.
- **Voice pool.** Pre-allocated at `initAudio()` time: `MAX_AUDIO_VOICES` voice structs in a flat array. Activation is finding a free slot (linear scan, 32 entries — negligible). No allocation on `playSound()`.
- **PCM buffers.** Loaded at `loadSound()` time. The voice pool stores `const i16*` pointers to the decoded data. The audio callback reads these pointers without allocation.
- **Command ring buffer.** Fixed capacity of 64 entries, allocated at `initAudio()`. No allocation on `postCommand()`.
- **Volume values.** Stored as `std::atomic<float>`. Reads in the callback use `memory_order_relaxed` — no fence, no cache miss beyond the atomic itself.

---

## 8. Error Handling

All errors follow the engine convention:

- Log via `FFE_LOG_ERROR("Audio", ...)` with a specific message
- Return `SoundHandle{0}` from `loadSound()` on failure
- Return `false` from `initAudio()` and `setAudioAssetRoot()` on failure
- No exceptions, no `abort()`, no `assert` in release builds
- Audio device unavailability is not fatal — the engine continues; all playback functions become no-ops when `isAudioAvailable()` returns false

Callers distinguish failure by checking `ffe::audio::isValid(handle)`. The specific error reason is in the log.

---

## 9. File Layout and CMake Integration

```
engine/audio/
    audio.h        — public API header (included by game code)
    audio.cpp      — implementation
                     #define MINIAUDIO_IMPLEMENTATION (exactly once)
                     #define STB_VORBIS_IMPLEMENTATION (exactly once, stb_vorbis.h)
    .context.md    — owned by api-designer, written after implementation review
```

`CMakeLists.txt` in `engine/audio/` must be updated to compile `audio.cpp`. The top-level `engine/CMakeLists.txt` (or `engine/audio/CMakeLists.txt` if it contains the target definition) must add `audio.cpp` as a source.

Warnings suppression for the C headers (miniaudio and stb_vorbis generate warnings under `-Wall -Wextra`) must be handled by compiling them inside a dedicated wrapper `.cpp` with `#pragma GCC diagnostic push/pop` or by setting `-w` on that translation unit only via `set_source_files_properties`. No project-wide warning suppression is permitted (CLAUDE.md Section 4).

---

## 10. Lua Binding Sketch (deferred — informational)

The audio API maps naturally to Lua. This is not implemented in v1 but is included for the api-designer to reference when the scripting layer is extended:

```lua
-- Load sounds at startup (in an init script or application.on_start)
local jump_sfx  = audio.load("sfx/jump.wav")
local theme     = audio.load("music/theme.ogg")

-- Play effects from systems
function on_player_jump()
    audio.play(jump_sfx)
end

-- Music control
audio.play_music(theme, true)   -- loop=true
audio.stop_music()
audio.pause_music()
audio.resume_music()

-- Volume (0.0 to 1.0)
audio.set_master_volume(0.8)
audio.set_sfx_volume(1.0)
audio.set_music_volume(0.6)
```

The Lua binding must NOT expose `audio.init`, `audio.shutdown`, or `audio.set_asset_root` — these are engine-level lifecycle functions. See SEC-9.

---

## 11. Implementation Checklist

For engine-dev to verify before marking implementation complete. security-auditor uses this as the review framework.

- [ ] `third_party/miniaudio.h` present, version noted in comment at top of file
- [ ] `third_party/stb_vorbis.h` present, version noted in comment at top of file
- [ ] `MINIAUDIO_IMPLEMENTATION` defined in exactly one `.cpp` file
- [ ] `STB_VORBIS_IMPLEMENTATION` defined in exactly one `.cpp` file
- [ ] `isAudioPathSafe()` is the first operation in `loadSound()` — before any syscall
- [ ] `stat()` file size check occurs before `fopen()`
- [ ] `realpath()` called after string validation, before `fopen()`
- [ ] Canonical path prefix-checked against asset root after `realpath()`
- [ ] Magic byte check performed before passing file to decoder
- [ ] Decoded `frameCount`, `channels`, `sampleRate` validated before accepting PCM
- [ ] Decoded size computed with `u64` arithmetic before comparison to `AUDIO_MAX_DECODED_BYTES`
- [ ] PCM memory freed on all rejection paths after a successful decode
- [ ] Volume inputs clamped via `clampVolume()` before storage — `NaN` and `inf` handled
- [ ] Voice pool allocated at `initAudio()` time as a fixed array — no per-`playSound()` allocation
- [ ] Command ring buffer uses `std::atomic<u32>` head/tail — no mutex on ring buffer itself
- [ ] Volume values stored as `std::atomic<float>`
- [ ] `m_mutex` held only during voice pool / music state access — NOT during file I/O
- [ ] `setAudioAssetRoot()` is write-once — second call returns false and is a no-op
- [ ] `isAudioAvailable()` returns false in headless mode and if device open failed
- [ ] All playback functions are no-ops when audio is unavailable
- [ ] `initAudio()`, `shutdownAudio()`, `setAudioAssetRoot()` are NOT exposed to Lua
- [ ] `unloadSound()` stops any voice using the handle before freeing PCM data
- [ ] All errors logged via `FFE_LOG_ERROR("Audio", ...)` with specific messages
- [ ] Zero warnings on Clang-18 and GCC-13 with `-Wall -Wextra` (use per-TU pragma guards for miniaudio/stb_vorbis)
- [ ] `tests/audio/` contains: path traversal tests, size limit tests, headless init test, volume clamp test
- [ ] security-auditor has reviewed the implementation (post-implementation review)
- [ ] api-designer has reviewed the public API and written `engine/audio/.context.md`
- [ ] game-dev-tester has built an interactive demo using sound effects and music

---

## 12. Open Questions for security-auditor

**Q1: OGG decoder attack surface.** stb_vorbis is the same stb family as stb_image. Is it considered safe enough for audio files from untrusted sources (e.g., downloaded mod packs), given the magic byte allowlist, file size limit (SEC-2), and decoded size limit (SEC-4)? Should a pre-validation step (e.g., checking the OGG stream header before full decode) be added?

**Q2: TOCTOU between stat() and fopen().** Same race as ADR-005 Q4. A file could be replaced between `stat()` (size check) and `fopen()`. For FFE's threat model (local game assets, not a server), is this acceptable? Does security-auditor recommend `open(O_NOFOLLOW)` or other hardening?

**Q3: stb_vorbis memory allocation.** stb_vorbis internally allocates memory during OGG decoding. This allocation is not under our control. Is this a concern for the stated decoded-size limits, or does the file-size limit (SEC-2) sufficiently constrain the attack surface?

**Q4: Audio device as a Denial of Service vector.** If `initAudio()` is called with attacker-controlled device selection (not currently in the API, but worth noting), could it be used to open an arbitrary file as an audio device? In the current design, the device is selected by the OS/miniaudio automatically — there is no user-controlled device name parameter. Is this sufficient?

**Q5: Null byte injection in audio paths.** Same concern as ADR-005 Q5. Should `loadSound()` enforce a `strnlen` check with `AUDIO_MAX_PATH` before the path safety check?

---

## 13. Dependencies

| Dependency | Source | vcpkg.json change? |
|------------|--------|--------------------|
| miniaudio | `third_party/miniaudio.h` (embedded) | **No** — same pattern as stb_image, glad |
| stb_vorbis | `third_party/stb_vorbis.h` (embedded) | **No** — same pattern as stb_image |
| `ffe::core` types and logging | `engine/core/` (existing) | No |

No new vcpkg dependencies. The commit message must state: `feat(audio): add audio subsystem via miniaudio + stb_vorbis (headers in third_party/)`.

---

*ADR-006 v1.0 — awaiting security-auditor shift-left review. Implementation blocked until review returns no CRITICAL or HIGH findings.*
