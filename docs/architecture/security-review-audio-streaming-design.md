# Security Review: Audio Streaming Design Note

**Reviewer:** security-auditor
**Date:** 2026-03-06
**Target:** `docs/architecture/design-note-audio-streaming.md`
**Type:** Shift-left review (before implementation)
**Verdict:** PASS WITH CONSTRAINTS

---

## Summary

The design is sound. No CRITICAL or HIGH findings. Three implementation constraints (MEDIUM-1, LOW-1, LOW-2) must be enforced during implementation — they are all straightforward error checks. Implementation may proceed.

---

## Findings

### MEDIUM-1: soundId bounds check in PLAY_MUSIC callback handler

**Location:** Section 7 (Security Considerations), Section 8 (step 5)

**Risk:** When the audio callback processes a `PLAY_MUSIC` command, it looks up the canonical path via `soundId - 1u` into `s_state.sounds[]`. If `soundId` is out of range, or if `sounds[idx].inUse` is false, the access is invalid.

**Status:** The design correctly identifies this as a constraint ("soundId lookup must be bounds-checked"). The PLAY_SOUND handler sets the correct pattern:
```cpp
const u32 idx = cmd.soundId - 1u;
if (idx < MAX_SOUNDS && s_state.sounds[idx].inUse) { ... }
```

**Constraint for implementation:** The PLAY_MUSIC handler MUST check all three conditions:
```cpp
const u32 idx = cmd.soundId - 1u;
if (idx >= MAX_SOUNDS || !s_state.sounds[idx].inUse ||
    s_state.sounds[idx].canonPath[0] == '\0') {
    FFE_LOG_ERROR("Audio", "playMusic: invalid or unloaded sound handle id=%u", cmd.soundId);
    // do not call stb_vorbis_open_filename
}
```

Additionally: `canonPath` must be populated at `loadSound()` time for ALL sounds, not just those intended for music. This is safe — it's stored in the fixed-size `SoundBuffer` struct and does not require dynamic allocation.

---

### MEDIUM-2: stb_vorbis_seek_start return value must be checked

**Location:** Section 3 (state machine), Section 5 (stb_vorbis lifecycle)

**Risk:** The design states "Loop by calling `stb_vorbis_seek_start()` when `get_samples_float_interleaved()` returns 0." However, `stb_vorbis_seek_start()` can fail — it returns a non-zero int on success, 0 on failure. A corrupt or truncated OGG stream may fail to seek. If the implementation retries on failure in a loop, this creates a hang; if it does not check, it may read from a bad stream state.

**Constraint for implementation:**
```cpp
if (s_state.musicLoop) {
    const int seekOk = stb_vorbis_seek_start(s_state.musicStream);
    if (!seekOk) {
        // Corrupt stream — cannot loop, stop music gracefully
        FFE_LOG_ERROR("Audio", "audioCallback: stb_vorbis_seek_start() failed — stopping music");
        stb_vorbis_close(s_state.musicStream);
        s_state.musicStream = nullptr;
        s_state.musicPlaying.store(false, std::memory_order_relaxed);
    }
}
```

---

### LOW-1: stb_vorbis_open_filename error code must be logged

**Location:** Section 5 (stb_vorbis lifecycle), Section 8 (step 5)

**Risk:** `stb_vorbis_open_filename(path, &error, nullptr)` returns null on failure. The `error` int contains a `VORBIS_` error code (e.g., `VORBIS_invalid_first_page`). If the implementation discards the error code, failures are difficult to diagnose.

**Constraint for implementation:**
```cpp
int vorbisError = 0;
stb_vorbis* stream = stb_vorbis_open_filename(canonPath, &vorbisError, nullptr);
if (!stream) {
    FFE_LOG_ERROR("Audio", "playMusic: stb_vorbis_open_filename() failed "
                  "(error=%d) for \"%s\"", vorbisError, canonPath);
    // post STOP_MUSIC implicit state, return
}
```

---

### LOW-2: Music mixing must remain under the existing mutex

**Location:** Section 8 (step 6): "Add music mixing pass after SFX mixing (separate loop, same mutex)"

**Confirmation:** The design correctly specifies that music mixing must occur within the same `std::lock_guard` as SFX mixing. This prevents a window where `unloadSound()` (which acquires `s_state.mutex`) could free a sound buffer that the music mixing pass is reading from. The constraint is already stated — confirming it is correct and MUST NOT be changed during implementation.

---

## Confirmed Safe Properties

**Path reuse from validated SoundHandle:** The design routes `playMusic` through the `SoundHandle` / `canonPath` chain that was already hardened by the `loadSound()` security review. No new path handling code is introduced. This is the correct approach.

**No new Lua attack surface:** `ffe.playMusic(handle, loop)` takes a handle (integer) and boolean — no string path ever reaches Lua. Path traversal via Lua is impossible. PASS.

**stb_vorbis codebook heap:** The MEDIUM-2 concern from audio.cpp is inherited. The file-size check at `loadSound()` time (`AUDIO_MAX_FILE_BYTES = 32 MB`) is accepted mitigation for FFE's local-asset threat model. No change needed.

**`stb_vorbis_open_filename` on callback thread:** File I/O on the audio callback thread is a known engineering trade-off explicitly documented in the design. For FFE's single-process local-asset model, this is acceptable. Not a security concern.

**`AudioCommand` struct growth:** Adding a `u8 flags` field for the loop boolean is safe. The struct remains small (≤ 16 bytes) and within the ring buffer capacity.

---

## Verdict

**PASS WITH CONSTRAINTS**

Implementation may proceed. The three constraints (MEDIUM-1, MEDIUM-2, LOW-1) are simple error checks — each is ≤ 10 lines. LOW-2 is a design confirmation, not a new change.

No CRITICAL or HIGH findings. Security-auditor post-implementation review required before merge.
