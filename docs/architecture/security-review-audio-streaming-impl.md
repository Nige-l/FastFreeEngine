# Security Review: Audio Streaming Implementation

**Reviewer:** security-auditor
**Date:** 2026-03-06
**Target:** `engine/audio/audio.cpp` — music streaming implementation (Session 11)
**Type:** Post-implementation review
**Verdict:** PASS — no CRITICAL or HIGH findings

---

## Shift-Left Constraints Verified

All three constraints from `security-review-audio-streaming-design.md` are implemented correctly.

### MEDIUM-1: soundId bounds check — IMPLEMENTED ✓

```cpp
// audio.cpp audioCallback(), Phase 2 PLAY_MUSIC handler (line ~433):
const u32 idx = pendingPlayMusic.soundId - 1u;
if (idx < MAX_SOUNDS && s_state.sounds[idx].inUse &&
    s_state.sounds[idx].canonPath[0] != '\0')
```

All three conditions are checked: range (`idx < MAX_SOUNDS`), slot occupancy (`inUse`), and path validity (`canonPath[0] != '\0'`). Error branch logs the invalid soundId. Correct.

### MEDIUM-2: stb_vorbis_seek_start return value — IMPLEMENTED ✓

```cpp
// audio.cpp audioCallback(), music decode section (line ~549):
const int seekOk = stb_vorbis_seek_start(s_state.musicStream);
if (!seekOk) {
    FFE_LOG_ERROR("Audio",
        "audioCallback: stb_vorbis_seek_start() failed — stopping music");
    stb_vorbis_close(s_state.musicStream);
    s_state.musicStream = nullptr;
    s_state.musicPlaying.store(false, std::memory_order_relaxed);
}
```

On seek failure: stream is closed, pointer nulled, musicPlaying set to false. No retry loop, no use-after-close. Correct.

### LOW-1: stb_vorbis_open_filename error code logged — IMPLEMENTED ✓

```cpp
int vorbisError = 0;
stb_vorbis* stream = stb_vorbis_open_filename(
    s_state.sounds[idx].canonPath, &vorbisError, nullptr);

if (stream) { ... } else {
    FFE_LOG_ERROR("Audio",
        "audioCallback: stb_vorbis_open_filename() failed "
        "(error=%d) for \"%s\"",
        vorbisError, s_state.sounds[idx].canonPath);
    s_state.musicPlaying.store(false, std::memory_order_relaxed);
}
```

Error code captured, logged. musicPlaying set to false on failure. Correct.

---

## Additional Review Findings

### LOW-2: Music mixing under mutex — CONFIRMED ✓

The music decode and mix pass (`stb_vorbis_get_samples_float_interleaved` + output write) runs inside the same `std::lock_guard<std::mutex>` that protects the SFX voice pool. This prevents `unloadSound()` from racing against the mix pass. The design constraint is met.

### stb_vorbis_close on track switch — CONFIRMED ✓

When `hasPendingPlayMusic` is true and a stream is already open:
```cpp
if (s_state.musicStream != nullptr) {
    stb_vorbis_close(s_state.musicStream);
    s_state.musicStream = nullptr;
}
```

The old stream is closed before the new one is opened. No double-open, no leak. Correct.

### shutdown() music stream cleanup — CONFIRMED ✓

```cpp
// audio::shutdown() — after ma_device_stop():
if (s_state.musicStream != nullptr) {
    stb_vorbis_close(s_state.musicStream);
    s_state.musicStream = nullptr;
}
s_state.musicPlaying.store(false, std::memory_order_relaxed);
```

`ma_device_stop()` is called before this block, so the callback is not running. Direct close is safe. Correct.

### canonPath populated for all sounds — CONFIRMED ✓

```cpp
// loadSound() — after PCM decode, inside mutex:
const size_t canonLen = strnlen(canonPath, static_cast<size_t>(PATH_MAX));
memcpy(buf.canonPath, canonPath, canonLen + 1u);
```

Bounded by `strnlen` with `PATH_MAX`. `canonPath` was produced by `::realpath()` so it is guaranteed null-terminated and ≤ `PATH_MAX` bytes. The `+1u` correctly copies the null terminator. `SoundBuffer::canonPath` is `char[PATH_MAX + 1]` — no overflow. Correct.

### static float musicDecBuf — CONFIRMED ✓

```cpp
static constexpr u32 MUSIC_DECODE_CAP = 4096u;
static float musicDecBuf[MUSIC_DECODE_CAP * 2u];
```

Using `static` local (not stack) avoids VLA and large stack allocation. The audio callback runs on a single dedicated thread (miniaudio's background thread), so there is no concurrent access to this buffer. Correct.

### decoded < decodeFrames as end-of-track detection — NOTE

`stb_vorbis_get_samples_float_interleaved` returns the number of frames decoded. A return of less than `decodeFrames` indicates end-of-stream. This is the correct idiom per the stb_vorbis documentation. No security issue, but note: a corrupt stream that returns 0 on the first call is handled by `0 < decodeFrames` — it immediately triggers the end-of-track path, closes the stream, and stops music. Correct for adversarial input.

---

## Verdict

**PASS** — all shift-left constraints implemented, no new CRITICAL or HIGH findings. The implementation is correct with respect to the security requirements.
