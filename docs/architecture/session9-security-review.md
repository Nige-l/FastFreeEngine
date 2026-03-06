# Session 9 Post-Implementation Security Review

**Reviewer:** security-auditor
**Date:** 2026-03-06
**Review type:** Post-implementation verification against shift-left conditions

---

## Part 1: Audio Subsystem

**Verdict: PASS**

All shift-left conditions from ADR-006-security-review.md are implemented and correctly documented in code.

### Shift-Left Conditions Verified

**HIGH-1 (Path concatenation overflow check)**
SATISFIED. Lines 525-530 in `audio.cpp` use `strnlen` for both `assetRoot` and `path`, then check `rootLen + 1u + pathLen + 1u > static_cast<size_t>(PATH_MAX) + 1u` before any write. The buffer is declared as `char fullPath[PATH_MAX + 1]`. The code comment references ADR-006 HIGH-2 (the ADR-005 inheritance is documented at the top of the file in the condition list). Condition met.

**HIGH-2 (Decoder output validation ordering)**
SATISFIED. Both WAV and OGG decode paths validate `frameCount`, `channels`, and `sampleRate` before the `u64` multiplication (lines 634 and 692 respectively). Each site carries an explicit comment explaining the ordering dependency and the consequence of casting a negative `int` to `u64`. Condition met.

**MEDIUM-1 (strnlen in isAudioPathSafe)**
SATISFIED. Line 250: `strnlen(path, static_cast<size_t>(MAX_AUDIO_PATH))`. The comment on line 249 explicitly references MEDIUM-1. Condition met.

**MEDIUM-2 (stb_vorbis codebook heap risk documented at decode site)**
SATISFIED. Lines 680-684 contain a block comment at the `stb_vorbis_decode_filename` call site documenting the codebook allocation risk, referencing ADR-006-security-review.md MEDIUM-2, and stating the version: `stb_vorbis v1.22`. Condition met. The file header (lines 1-4) also pins both `miniaudio v0.11.25` and `stb_vorbis v1.22` (satisfying LOW-3).

### Additional Attack Surface — New Findings

**Volume clamping (NaN/Inf)**
`clampVolume()` (lines 230-235) returns `0.0f` for any value where `!std::isfinite(v)`, then clamps to `[0.0, 1.0]`. This correctly handles NaN, positive Inf, and negative Inf. Applied to `setMasterVolume` (line 826), to `playSound` per-instance volume (line 816, satisfying LOW-5), and implicitly to the `SET_MASTER_VOLUME` command path. No gap found.

**SPSC ring buffer memory ordering**
The implementation is correct. `postCommand` (write path, line 415) stores `cmdHead` with `memory_order_release` after writing the payload. The audio callback (read path, line 317) loads `cmdHead` with `memory_order_acquire`. The `cmdTail` write in the callback (line 354) uses `memory_order_release`; the `cmdTail` read in `postCommand` (line 407) uses `memory_order_acquire`. LOW-2 is satisfied, and the correct ordering is documented at both the struct definition (lines 173-176) and the `postCommand` function (lines 400-403).

One observation on `cmdTail` ring full check: line 409 computes `nextHead == tail % CMD_RING_CAPACITY`. Since `tail` is already a value in `[0, CMD_RING_CAPACITY)` (it is stored modulo capacity at line 353), the `% CMD_RING_CAPACITY` on line 409 is redundant but harmless. This is not a security or correctness issue.

**unloadSound safety while playing**
SATISFIED (LOW-4). Lines 779-803 acquire `s_state.mutex`, deactivate all voices referencing the buffer (step 1), then free the PCM buffer (step 2), all within the same critical section. The comment at lines 775-778 explicitly documents the required ordering and the use-after-free consequence of splitting the operations. The audio callback (lines 360-394) also holds `s_state.mutex` during mixing, so it cannot execute concurrently with `unloadSound`.

---

## Part 2: ffe.loadTexture / ffe.unloadTexture Lua Bindings

**Verdict: PASS**

All shift-left conditions from design-note-lua-texture-load-security-review.md are implemented correctly.

### Shift-Left Conditions Verified

**MEDIUM-1 (lua_type check before lua_tostring)**
SATISFIED. Line 859: `if (lua_type(state, 1) != LUA_TSTRING)` is the first operation in the `loadTexture` binding, before any `lua_tostring` call. Non-string arguments return nil immediately. The comment at lines 856-858 explicitly documents the coercion risk (numbers and booleans). Condition met.

**LOW-2 (rawHandle range check in unloadTexture)**
SATISFIED. Line 885: `if (rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(UINT32_MAX))`. This correctly rejects the null sentinel (0), negative values, and values exceeding UINT32_MAX. Condition met.

**LOW-5 (single-argument loadTexture overload only)**
SATISFIED. Line 869 calls `ffe::renderer::loadTexture(path)` — the single-argument overload. No asset root parameter is passed from Lua. The comment at lines 866-868 explicitly documents that the two-argument overload is intentionally not called. Condition met.

### Additional LOW Advisory Findings Verified

**L-1 (lua_tostring usage consistent)**
CONFIRMED. The binding uses `lua_tostring(state, 1)` on line 864, only after the `LUA_TSTRING` guard passes. No length-aware copy is made.

**L-2 (lua_Integer overflow handled)**
CONFIRMED. `luaL_checkinteger` on line 882 retrieves a 64-bit integer, and the range check on line 885 rejects values outside `(0, UINT32_MAX]` before the cast.

### Minor Formatting Note (does not affect verdict)

Lines 856 and 866 in `script_engine.cpp` use `/` instead of `//` to open comments (visible as `/ MEDIUM-1:` and `/ LOW-5:`). These are single-line comments with a missing second slash. This will not cause a compile error because a bare `/` followed by a space is parsed as a division operator — but these lines have no left-hand operand in a statement context, so they will either produce a warning or fail to compile depending on context. This is a code defect, not a security finding, and should be corrected by `engine-dev`. It does not affect the security properties of either binding.

---

## Summary

| Area | Verdict | Notes |
|------|---------|-------|
| Audio — HIGH-1 path concat overflow | PASS | strnlen + length check + comment present |
| Audio — HIGH-2 decoder validation ordering | PASS | Both WAV and OGG paths; comments present |
| Audio — MEDIUM-1 strnlen in isAudioPathSafe | PASS | Explicit with comment |
| Audio — MEDIUM-2 codebook risk documented | PASS | Inline comment + version pin |
| Audio — volume NaN/Inf clamping | PASS | clampVolume handles all non-finite values |
| Audio — SPSC ring buffer memory ordering | PASS | release/acquire on both sides, documented |
| Audio — unloadSound while playing | PASS | Single critical section, ordering documented |
| Lua — MEDIUM-1 type guard before lua_tostring | PASS | lua_type check is first operation |
| Lua — LOW-2 unloadTexture handle range | PASS | Correct boundary (0, UINT32_MAX] |
| Lua — LOW-5 single-arg overload only | PASS | Two-arg overload not called; documented |
| Lua — comment syntax defect (lines 856, 866) | DEFECT | Missing second slash on `//`; not a security issue; engine-dev must fix |
