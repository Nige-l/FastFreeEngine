# Performance Review: Audio M-1 and M-2 Fixes

**Reviewer:** performance-critic
**Date:** 2026-03-06
**Target:** `engine/audio/audio.cpp` — M-1 (callback mutex reduction) + M-2 (atomic voice count)
**Verdict:** PASS

---

## M-2 Fix: getActiveVoiceCount() — mutex scan → atomic read

### Before
```cpp
u32 getActiveVoiceCount() {
    if (!s_state.initialised) { return 0u; }
    std::lock_guard<std::mutex> lock(s_state.mutex);  // mutex acquire
    u32 count = 0u;
    for (u32 i = 0u; i < MAX_AUDIO_VOICES; ++i) {    // linear scan (32 iterations)
        if (s_state.voices[i].active) { ++count; }
    }
    return count;
}
```

### After
```cpp
u32 getActiveVoiceCount() {
    if (!s_state.initialised) { return 0u; }
    return s_state.activeVoiceCount.load(std::memory_order_relaxed);  // O(1), no lock
}
```

### Analysis

**Correctness:** The `activeVoiceCount` atomic is maintained with `fetch_add`/`fetch_sub` at every voice activation and deactivation point, all of which occur under `s_state.mutex`. The main thread reads with `relaxed` ordering — this means the count may be a few nanoseconds stale relative to the callback thread, which is acceptable for a diagnostic counter. No data race: `std::atomic<u32>` operations are individually atomic regardless of ordering.

**Performance gain:** Previous implementation: mutex acquire (~20–100 ns on uncontested mutex on LEGACY hardware) + 32 cache-line touches (likely 2–4 cache lines for 32 × 8-byte Voice structs). New: single relaxed atomic load, typically 1–4 ns. Safe to call per-frame without concern.

**Counter invariant:** All mutations verified:
- `audioCallback` — PLAY_SOUND activation: `fetch_add(1)` under mutex ✓
- `audioCallback` — mixing loop completion: `fetch_sub(1)` under mutex ✓
- `audioCallback` — null buffer guard: `fetch_sub(1)` under mutex ✓
- `unloadSound` — voice stop: `fetch_sub(1)` under mutex ✓
- `shutdown` — `store(0)` after device stop and voice deactivation ✓
- `init` — `store(0)` on initialization ✓

**Verdict: PASS** — correct, O(1), no lock.

---

## M-1 Fix: audioCallback — N+1 mutex → 1 mutex

### Before
Each `PLAY_SOUND` command in the ring drain loop acquired the mutex once (N acquisitions), plus the mixing loop acquired it once more (1 acquisition) = N+1 total per callback.

### After
Phase 1 drains commands into a stack-local `pendingPlay[64]` array without any mutex. Phase 2 takes the mutex once for voice activation + mixing.

### Analysis

**Stack allocation:** `AudioCommand pendingPlay[64]`. `sizeof(AudioCommand)` = 1 (Type) + 3 (padding) + 4 (soundId) + 4 (volume) = 12 bytes. Total: 768 bytes on the audio callback stack. Audio callbacks typically have 64 KB+ of stack. This is fine.

**Ring drain semantics:** `cmdTail` is still advanced per-command during Phase 1 (same as before). The main thread sees freed ring slots incrementally, unchanged from the previous behavior.

**mutex acquisition count:**
- Before: N (PLAY_SOUND commands) + 1 (mixing) = N+1
- After: 1 (voice activation + mixing combined)

At typical game audio rates (1–4 sound events per frame, 1 music track), N=1–4. Reduction: 2–5 acquisitions → 1. For zero sound events: 1 → 1 (unchanged).

**Mutex hold time:** The single mutex now covers both voice activation and mixing. Previously, the mixing lock held the mutex for the same duration. Voice activation adds a small inner loop over `pendingCount` (typically 0–4 iterations of a 32-voice scan). This is negligible.

**Correctness:** Combining voice activation and mixing under one mutex is correct:
- `unloadSound()` acquires the same mutex → cannot race between activation and mixing
- `shutdown()` calls `ma_device_stop()` before mutex → callback is stopped before access

**M-3 fix (redundant modulo):** `if (nextHead == tail)` replaces `if (nextHead == tail % CMD_RING_CAPACITY)`. Since `tail` is maintained as `(tail + 1) % CMD_RING_CAPACITY`, it is always in `[0, CMD_RING_CAPACITY)`. Redundant modulo eliminated. Zero behavioral change, minor instruction savings.

**Verdict: PASS** — reduces mutex acquisitions from N+1 to 1, maintains correctness, stack usage within budget.

---

## Remaining Known Issue (not addressed this session)

**Mixing loop mutex hold time:** The mixing lock holds `s_state.mutex` for the entire mix of all active voices (up to 32 voices × frameCount frames). At 32 voices × 256 frames = 8,192 stereo float operations, this is ~0.02 ms on LEGACY hardware — well within the ~5.8 ms callback budget. This is the intrinsic cost of the mutex-protected mixing model. It would only become a concern above ~100 simultaneous voices, which exceeds `MAX_AUDIO_VOICES = 32`. Not a blocking issue.

---

## Summary

| Fix | Before | After | Verdict |
|-----|--------|-------|---------|
| M-2: getActiveVoiceCount | mutex + O(N) scan | O(1) atomic load | PASS |
| M-1: callback mutex count | N+1 per callback | 1 per callback | PASS |
| M-3: redundant modulo | `tail % capacity` | `tail` | PASS |
