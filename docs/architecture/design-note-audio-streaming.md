# Design Note: Audio Streaming / Music Playback

**Author:** architect
**Date:** 2026-03-06
**Status:** DRAFT — pending security-auditor shift-left review
**Implements:** Session 11 P1 goal

---

## 1. Problem Statement

The current audio subsystem supports one-shot sound effects via `playSound(handle)`. There is no way to play a looping background music track. Games need:

- A persistent looping track that continues until explicitly stopped
- Optional fade-in/fade-out on transitions
- Volume control independent of sound-effect master volume
- One active music track at a time (LEGACY-tier budget constraint)

---

## 2. Decode Strategy: Stream vs Decode-All

### Option A: Decode all upfront (`stb_vorbis_decode_filename`)

The current approach for sound effects: decode the entire file to a PCM float buffer at load time, store in `SoundBuffer`, play from memory.

**Pros:**
- Zero per-frame decode cost — mixing is a pure float array scan
- Already implemented and security-reviewed
- Dead simple

**Cons:**
- Memory: a 3-minute music track at 44.1 kHz stereo float = ~100 MB — far exceeds `AUDIO_MAX_DECODED_BYTES` (10 MB)
- Not viable for full music tracks

### Option B: stb_vorbis streaming API (decode per-callback)

`stb_vorbis` has a stateful streaming API:
```
stb_vorbis* stb_vorbis_open_filename(const char* path, int* error, ...)
int stb_vorbis_get_samples_float_interleaved(stb_vorbis* f, int channels,
                                              float* buffer, int num_floats)
void stb_vorbis_seek_start(stb_vorbis* f)    // for looping
void stb_vorbis_close(stb_vorbis* f)
```

The audio callback decodes exactly `frameCount` frames per invocation from the stb_vorbis state. No large upfront allocation — working memory is ~64 KB per open track.

**Pros:**
- Low memory: working memory ~64 KB + one small decode ring (~4 KB of output per callback at 256 frames)
- Supports arbitrarily long tracks
- Loop by calling `stb_vorbis_seek_start()` when `get_samples_float_interleaved()` returns 0

**Cons:**
- stb_vorbis decode runs on the audio callback thread — adds CPU cost per-callback
- The codebook heap allocation risk (MEDIUM-2, documented in audio.cpp) applies here too

### Option C: Dedicated decode thread + ring buffer

A background thread decodes OGG into a circular PCM buffer. The audio callback reads from it. The decode thread refills it.

**Pros:**
- Decode off the audio callback thread (lowest latency impact)
- Can prefetch across loop points

**Cons:**
- Significant added complexity (thread, ring buffer, sync, underrun handling)
- LEGACY-tier CPUs can handle ~256 frames × 2 channels = 512 samples of OGG decode per callback easily (~0.1ms at 44100 Hz)
- Overkill for a single music track

### Recommendation: Option B (streaming in callback)

For LEGACY tier and one track at a time, Option B is sufficient. The CPU budget for OGG decode at 44.1 kHz, 256 frames/callback is negligible. Option C adds ~300 lines of threading complexity for no measurable gain on target hardware.

If profiling shows decode overhead on RETRO-tier hardware in future, Option C can be added as a higher-tier path.

---

## 3. Music State Machine

```
         playMusic()         stopMusic() or track ends (no loop)
STOPPED ──────────────> PLAYING ──────────────────────────────> STOPPED
                           │                           ▲
                           │ track ends (loop=true)    │
                           └───────────────────────────┘
                              seek_start → continue
```

States (internal to audio module, not exposed in public API):
- `STOPPED` — no music track active; `m_musicStream` is null
- `PLAYING` — decoding and mixing on each callback
- Future extension: `FADING_OUT` (for cross-fades — out of scope for Session 11)

---

## 4. Proposed Public API

```cpp
// Play a music track. Only one track may be active at a time.
// If a track is already playing, it is stopped first.
//
// handle: must be a valid SoundHandle loaded by loadSound().
//   NOTE: music streaming opens the file directly via stb_vorbis — the handle
//   is used to retrieve the validated canonical path from the sound table,
//   NOT to read from the decoded PCM buffer. This avoids double-decoding.
//
// loop: if true, the track loops indefinitely. If false, it plays once and
//   transitions to STOPPED.
//
// No-op if handle is invalid, audio is unavailable, or in headless mode.
void playMusic(SoundHandle handle, bool loop = true);

// Stop the currently playing music track immediately.
// No-op if no music is playing.
void stopMusic();

// Get/set the music volume, independent of the master (sound-effects) volume.
// Clamped to [0.0, 1.0]. NaN/Inf -> 0.0.
void setMusicVolume(float volume);
float getMusicVolume();

// Returns true if a music track is currently playing.
bool isMusicPlaying();
```

### Why use SoundHandle for streaming?

The `SoundHandle` was obtained via `loadSound()` which already ran the full security chain (path traversal check, realpath, assetRoot confinement). Reusing the handle avoids re-running path validation for music. The implementation retrieves the canonical path stored during `loadSound()` and passes it to `stb_vorbis_open_filename`.

**Design change required:** `SoundBuffer` must store the canonical path string (up to `PATH_MAX+1` bytes) alongside the PCM data. This is a load-time cost only.

---

## 5. Thread Safety

### Music state lives in the callback thread

The `stb_vorbis*` state pointer and music playback parameters (loop, volume, position) are accessed from the audio callback. The main thread communicates changes via the existing SPSC command ring, adding two new command types:

```cpp
enum class Type : u8 {
    PLAY_SOUND,
    SET_MASTER_VOLUME,
    PLAY_MUSIC,      // new
    STOP_MUSIC,      // new
    SET_MUSIC_VOLUME // new
};
```

`PLAY_MUSIC` command payload: `soundId` (to look up canonical path), `loop` flag (packed into the `volume` field as 0.0/1.0 to avoid struct changes, or add a new bool field).

**Struct change required:** `AudioCommand` needs a `bool loop` field (or pack into an existing field). Adding a `u8 flags` field keeps it at 16 bytes total (alignment).

### stb_vorbis state lifecycle

- `stb_vorbis_open_filename()` is called from the audio callback when it processes a `PLAY_MUSIC` command.
- `stb_vorbis_close()` is called from the audio callback when `stopMusic` is received or the track ends.
- `audio::shutdown()` posts a `STOP_MUSIC` command and then calls `ma_device_stop()`, which drains the callback. If the stream is still open after the device stops, `shutdown()` closes it directly (device stopped → callback no longer running → safe).

### Main-thread queries

`isMusicPlaying()` and `getMusicVolume()` read atomics — no mutex required. `getMusicVolume()` mirrors `getMasterVolume()`.

---

## 6. Memory Budget (LEGACY Tier)

| Resource | Size |
|---|---|
| stb_vorbis working memory | ~64 KB per open track |
| Canonical path stored in SoundBuffer | PATH_MAX+1 = 4097 bytes × 256 slots = ~1 MB |
| Decode output per callback (256 frames, stereo float) | 256 × 2 × 4 = 2 KB |

One music track at a time. Total overhead: ~66 KB + the path table (already bearable at 1 MB for 256 sound slots — and in practice games load far fewer).

---

## 7. Security Considerations

The music path reuses the already-validated canonical path from the sound table. No new path handling is introduced. The `stb_vorbis_open_filename()` call receives the canonical path that was already verified by `realpath()` + assetRoot prefix check at `loadSound()` time.

Risks requiring security-auditor review:
- **PLAY_MUSIC command**: soundId lookup must be bounds-checked before retrieving the stored path (same pattern as PLAY_SOUND soundId validation)
- **stb_vorbis streaming state**: a corrupt or adversarial OGG file could trigger the codebook heap issue during streaming decode. The file-size limit was already checked at `loadSound()` time — confirm this is sufficient mitigation.
- **Callback-thread open**: `stb_vorbis_open_filename()` performs file I/O on the audio callback thread. This is acceptable (same model as many game engines) but should be noted. For FFE's local-asset threat model, this is fine.

---

## 8. Implementation Plan (after security review)

1. Add `char canonPath[PATH_MAX+1]` to `SoundBuffer` — populate in `loadSound()`
2. Add `PLAY_MUSIC`, `STOP_MUSIC`, `SET_MUSIC_VOLUME` to `AudioCommand::Type`
3. Add `bool loop` / `u8 flags` field to `AudioCommand`
4. Add music state to module state struct: `stb_vorbis* musicStream`, `std::atomic<float> musicVolume`, `std::atomic<bool> musicPlaying`, `bool musicLoop`
5. Handle new command types in `audioCallback()`:
   - `PLAY_MUSIC`: close existing stream if open, open new stb_vorbis stream
   - `STOP_MUSIC`: close stream, clear pointer
   - `SET_MUSIC_VOLUME`: atomic store
6. Add music mixing pass after SFX mixing (separate loop, same mutex)
7. In `shutdown()`: if `musicStream` is non-null, close it after device stop
8. Expose `playMusic`, `stopMusic`, `setMusicVolume`, `getMusicVolume`, `isMusicPlaying` in `audio.h`
9. Add `ffe.playMusic`, `ffe.stopMusic`, `ffe.setMusicVolume` Lua bindings in `script_engine.cpp`

---

## 9. Out of Scope (Session 11)

- Cross-fade between tracks
- Music position query (getMusicPosition) — complex with streaming, deferred
- Multiple simultaneous music tracks
- RETRO tier support (no miniaudio streaming equivalent without stb_vorbis)
