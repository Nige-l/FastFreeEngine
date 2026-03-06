# Performance Review: Audio Subsystem

**Reviewer:** performance-critic
**Date:** 2026-03-06
**Files reviewed:** engine/audio/audio.h, engine/audio/audio.cpp

## Verdict: MINOR ISSUES

No BLOCK-level findings. The audio subsystem is correctly structured as a non-per-frame API with a pre-allocated voice pool and a lock-free SPSC ring buffer for the primary hot path. Three minor issues are noted, one of which (mutex in the audio callback) warrants attention.

---

## Findings

| ID | Severity | Finding |
|----|----------|---------|
| M-1 | MINOR ISSUE | Mutex acquired twice per audio callback invocation (command drain + mixing), contended with main thread `unloadSound()` and `getActiveVoiceCount()` |
| M-2 | MINOR ISSUE | `getActiveVoiceCount()` acquires `std::mutex` — not safe to call per-frame without cost awareness |
| M-3 | MINOR ISSUE | `postCommand()` full-ring check has a modulo inconsistency (defence-in-depth, not a correctness bug) |
| I-1 | INFO | `stb_vorbis_decode_filename` on main thread at load time — correctly documented as load-time only |
| I-2 | INFO | `playSound()` at capacity: silently drops with a log warning — correct behaviour |
| I-3 | INFO | SPSC ring buffer atomic count on `playSound()` hot path — acceptable for LEGACY tier |
| I-4 | INFO | OGG path performs two `malloc` calls (short buffer + float conversion buffer) at load time — correct, not per-frame |

---

## Details

### M-1 — Mutex acquired twice per audio callback invocation

**Severity: MINOR ISSUE**

The audio callback (`audioCallback`) acquires `s_state.mutex` twice per invocation:

1. Once inside the command-draining loop (line 325), for every `PLAY_SOUND` command processed.
2. Once for the full mixing pass over all active voices (line 361).

This mutex is the same one held by `unloadSound()` (main thread) and `getActiveVoiceCount()` (main thread). While miniaudio's audio thread runs at a high scheduling priority, contention here can cause the main thread to stall waiting for the callback to release the lock, and conversely the audio callback to spin waiting for `unloadSound()` to finish.

The mixing loop's mutex acquisition (lock 2) is the more significant one: it is held for the entire duration of mixing all active voices, which scales with both `MAX_AUDIO_VOICES` (32) and the audio frame size. Any main-thread call to `unloadSound()` or `getActiveVoiceCount()` during this window will block.

This is not a correctness bug and the audio API header correctly documents that `unloadSound()` is a load-time-only call. However, on LEGACY tier (single-core safe), if the OS schedules the audio callback and the main thread simultaneously on a single core, priority inversion through the mutex is possible in theory.

**Recommendation:** The command-drain mutex (lock 1) could be eliminated by allowing the SPSC ring consumer to stage voice activation into a separate lock-free structure, taking the mutex only during the mixing pass. This is a non-trivial refactor and is not required for correctness or current performance targets.

---

### M-2 — `getActiveVoiceCount()` acquires `std::mutex`

**Severity: MINOR ISSUE**

```cpp
u32 getActiveVoiceCount() {
    if (!s_state.initialised) { return 0u; }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    u32 count = 0u;
    for (u32 i = 0u; i < MAX_AUDIO_VOICES; ++i) {
        if (s_state.voices[i].active) { ++count; }
    }
    return count;
}
```

This function acquires the same mutex that the audio callback holds during mixing. If called every frame by a game system (e.g., to display an audio debug overlay or throttle `playSound()` calls), it will contend with the audio thread on every frame.

The function header in `audio.h` does not warn about this cost. It is listed as a diagnostics function, which implies occasional use, but the cost is not stated.

**Recommendation:** Add a comment to both the header declaration and the implementation noting that this function acquires a mutex and should not be called per-frame. Alternatively, maintain a separate `std::atomic<u32>` active voice counter that is incremented/decremented as voices are activated/deactivated — this would make `getActiveVoiceCount()` a single atomic load with no mutex.

---

### M-3 — Modulo inconsistency in `postCommand()` full-ring check

**Severity: MINOR ISSUE** (defence-in-depth, not a live bug at current capacity)

```cpp
const u32 nextHead = (head + 1u) % CMD_RING_CAPACITY;
if (nextHead == tail % CMD_RING_CAPACITY) {
```

`tail` is always stored modulo `CMD_RING_CAPACITY` (the callback increments it as `tail = (tail + 1u) % CMD_RING_CAPACITY` on line 353). Therefore `tail % CMD_RING_CAPACITY` is redundant but harmless. However, `head` is also stored as its raw index (never reduced modulo before storage in `cmdHead`), so the `(head + 1u) % CMD_RING_CAPACITY` on the first line is the only modulo that matters for the comparison. This is consistent in practice but the dual-modulo on `tail` creates a reading confusion about whether the invariant is that indices are stored pre-reduced or not. The ring buffer indexing would be cleaner if the invariant were made explicit in a comment or if both read sites used the same reduction pattern.

**Recommendation:** Add a comment documenting whether stored indices are pre-reduced or raw, and remove the redundant `% CMD_RING_CAPACITY` on the `tail` read in the full-ring check.

---

### I-1 — `stb_vorbis_decode_filename` on main thread at load time

**Severity: INFO**

`stb_vorbis_decode_filename()` is called inside `loadSound()` for OGG files. This performs full Vorbis decode, which is CPU-intensive and allocates working memory on the heap. It is correctly documented in both `audio.h` (line 20: "NOT a per-frame API") and in the implementation comment (MEDIUM-2 note). The header explicitly warns: "Never call `loadSound()` per-frame."

This is acceptable. No action required.

---

### I-2 — `playSound()` at capacity: silent drop with log warning

**Severity: INFO**

When all `MAX_AUDIO_VOICES` (32) slots are occupied, the `PLAY_SOUND` command is processed by the audio callback but no voice is allocated. A `FFE_LOG_WARN` is emitted. No heap allocation occurs. No new voice is created.

This is the correct behaviour per the engine's performance philosophy (no hidden heap allocations in hot paths). The drop is logged, giving the developer visibility. No action required.

---

### I-3 — SPSC ring buffer atomic count on `playSound()` hot path

**Severity: INFO**

`playSound()` calls `postCommand()`, which performs:
- 1x `atomic<u32>::load(memory_order_relaxed)` — read `head`
- 1x `atomic<u32>::load(memory_order_acquire)` — read `tail` (acquire fence)
- 1x struct copy into `cmdRing[head % CMD_RING_CAPACITY]`
- 1x `atomic<u32>::store(nextHead, memory_order_release)` — write head

Total: 2 atomic loads + 1 atomic store. This is the minimum required for a correct SPSC ring. On LEGACY-tier single-core hardware, atomics compile to plain loads/stores with compiler barriers (no hardware fence instructions on x86). This is entirely acceptable.

`playSound()` is documented as safe for game-event boundaries (not per-frame), so even the acquire/release cost is a non-issue.

---

### I-4 — Two `malloc` calls in OGG load path

**Severity: INFO**

The OGG decode path calls `malloc` twice: once for `stb_vorbis_decode_filename`'s output buffer (short PCM), and once for the float32 conversion buffer. The short buffer is freed immediately after conversion. This is load-time only and entirely correct. No action required.

---

## Pre-allocated Resources Confirmed

The following were verified as correctly pre-allocated at `init()` time with no per-frame allocations:

- `s_state.sounds[MAX_SOUNDS]` — static array of `SoundBuffer`, zero-initialised
- `s_state.voices[MAX_AUDIO_VOICES]` — static array of `Voice`, zero-initialised
- `s_state.cmdRing[CMD_RING_CAPACITY]` — static SPSC ring buffer

No `new`, `delete`, `malloc`, or `free` appears in any per-frame code path (`audioCallback`, `playSound`, `postCommand`, `setMasterVolume`, `getMasterVolume`, `isAudioAvailable`). The constitution's rule on hidden heap allocations in hot paths is satisfied.

---

## LEGACY Tier Assessment

The audio subsystem targets LEGACY tier. LEGACY requires single-core safety. The SPSC ring buffer correctly uses `memory_order_acquire`/`memory_order_release` rather than `memory_order_relaxed` on the index updates, ensuring correctness even if the OS schedules main thread and audio callback concurrently. The mutex usage (M-1) is the only point of contention between threads, and it is bounded in scope. The design is suitable for LEGACY tier with the caveats noted under M-1 and M-2.
