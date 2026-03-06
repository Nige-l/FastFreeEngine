# ADR-006 Audio — Security-Auditor Shift-Left Review

**Reviewer:** security-auditor
**Date:** 2026-03-06
**Document reviewed:** ADR-006-audio.md v1.0
**Review type:** Shift-left design review — gates implementation start

---

## Verdict: PASS WITH CONDITIONS

**Implementation may begin: YES WITH CONDITIONS**

ADR-006 is the strongest security-oriented ADR this project has produced. The architect has explicitly addressed every major attack surface from the ADR-005 review cycle and has pre-answered most of the questions that review would otherwise raise. Four conditions must be satisfied before merge — two HIGH and two MEDIUM. There are no CRITICAL findings. The post-implementation review will verify each condition.

---

## Conditions for Implementation (must be met before merge)

### HIGH — Hard requirements

**HIGH-1: Path concatenation buffer overflow (inherited from ADR-005 HIGH-2)**

The ADR specifies `AUDIO_MAX_PATH = 4096` as a constant for path length but does not explicitly state that the concatenation `assetRoot + "/" + path` is bounds-checked before the concatenation occurs. ADR-005's HIGH-2 finding applies identically here.

Required: Before concatenating `assetRoot + "/" + path`, check that `strlen(assetRoot) + 1 + strlen(path) + 1 <= PATH_MAX`. The concatenation buffer must be `PATH_MAX + 1` bytes (4097 on Linux). Failure to check this allows a stack buffer overflow in the concatenation step that executes before `realpath()` can apply its own bounds. `AUDIO_MAX_PATH` bounds the caller-supplied path but does not bound the asset root; the combined length must be checked.

The implementation checklist item `isAudioPathSafe()` only bounds the caller path — the asset root concatenation check is absent from the checklist. Add it.

**HIGH-2: Decoder output integer cast ordering for decoded size check (SEC-4 before SEC-5/SEC-4 ordering)**

SEC-4 specifies validating `frameCount`, `channels`, and `sampleRate` from the decoder. SEC-4 (decoded size) specifies casting these values to `u64` before multiplication. The ADR describes this correctly:

```cpp
const u64 decodedBytes = static_cast<u64>(frameCount)
                       * static_cast<u64>(channels)
                       * static_cast<u64>(sizeof(i16));
```

However, the decoder output validation (SEC-5 — checking `frameCount > 0`, `channels in {1,2}`, `sampleRate in allowlist`) must execute before this multiplication, not after. If `frameCount` or `channels` is negative (returned as `int` from stb_vorbis or miniaudio), casting to `u64` produces `0xFFFFFFFFFFFFFFFF`, which trivially passes the `> AUDIO_MAX_DECODED_BYTES` check in the wrong direction only if the arithmetic wraps — but more importantly it guarantees the decodedBytes check is meaningless if the inputs are not already validated.

Required: The implementation must enforce this ordering with an explicit code comment:

```cpp
// Validate decoder outputs BEFORE the u64 size multiplication.
// A negative int cast to u64 produces a huge value that makes
// the size check nonsensical. SEC-5 validation must precede SEC-4 arithmetic.
if (frameCount <= 0) { /* free and reject */ }
if (channels != 1 && channels != 2) { /* free and reject */ }
// ... then compute decodedBytes ...
```

Add this ordering note to the implementation checklist.

---

### MEDIUM — Required before merge

**MEDIUM-1: stb_vorbis null-byte and path injection (Q5 — partially addressed)**

ADR-006 Q5 asks about null byte injection and defers to the security-auditor. The ADR's `isAudioPathSafe()` pseudocode does not include a `strnlen` check — it uses `strlen(path) >= AUDIO_MAX_PATH` which is unsafe against non-null-terminated buffers (undefined behaviour if the buffer has no null terminator within readable memory).

Required: Replace the `strlen` length check in `isAudioPathSafe()` with `strnlen(path, AUDIO_MAX_PATH) >= AUDIO_MAX_PATH`. This mirrors the MEDIUM-3 condition from ADR-005. The null byte scan described in the pseudocode is unreachable via C string iteration and must not be relied upon — the `strnlen` bound is the correct mitigation. Add this to the implementation checklist.

**MEDIUM-2: stb_vorbis memory allocation is not bounded by SEC-2 alone**

Q3 asks whether the file-size limit (SEC-2, `AUDIO_MAX_FILE_BYTES = 32 MB`) sufficiently constrains stb_vorbis internal memory allocation during OGG decoding.

The answer is: partially, but not completely. stb_vorbis allocates working memory proportional to the stream's codebook complexity, not merely the raw byte count. A pathologically crafted OGG file within the 32 MB file-size limit can contain Vorbis codebooks that cause stb_vorbis to allocate significantly more than 32 MB of working heap before the decoded PCM size check in SEC-4 runs.

However: the 10 MB `AUDIO_MAX_DECODED_BYTES` limit means that stb_vorbis will decode at most ~10 MB of output PCM before the size check rejects it. The working heap allocation risk is real but bounded by the quality of stb_vorbis's own allocator behaviour. For FFE's threat model (local game assets, not a mod-download server with untrusted OGG content), this is acceptable with one additional constraint:

Required: Add `MA_DR_VORBIS_MAX_FILE_SIZE` or equivalent stb_vorbis configuration if available (check the stb_vorbis API for a stream-size limit). If no such limit exists in the version being used, add a code comment at the `stb_vorbis_decode_file` (or equivalent) call site documenting this known limitation and referencing this finding. Do not leave the limitation undocumented in the code.

Additionally, note the version of stb_vorbis in a comment at the top of `third_party/stb_vorbis.h` — the same pinning requirement that applied to stb_image applies here. Add this to the implementation checklist (it is currently absent).

---

## Open Questions — Answered

| Q# | Question | Answer |
|----|----------|--------|
| Q1 | Is stb_vorbis safe enough for untrusted OGG content given SEC-2 and SEC-4 limits? | **With conditions.** The file-size limit (SEC-2) and decoded-size limit (SEC-4) together provide reasonable defence-in-depth. The codebook memory allocation risk is real but within acceptable bounds for FFE's local-asset threat model. Add a code comment at the decode call site documenting the stb_vorbis internal allocation risk (see MEDIUM-2). No pre-validation of the OGG stream header is required beyond the magic byte check in SEC-3. |
| Q2 | TOCTOU between stat() and fopen() acceptable? | **Yes, accepted for FFE's threat model.** Same determination as ADR-005 Q4: FFE is a local game engine, not a multi-user server. An attacker with write access to the asset directory can place content there directly and needs no TOCTOU exploit. Document the race in a code comment near the `realpath()` call, as was required for the texture loader. |
| Q3 | stb_vorbis internal allocation bounded by file-size limit? | **Partially.** See MEDIUM-2 above. The constraint is sufficient for the local-asset threat model but must be documented in code. |
| Q4 | Audio device selection as a DoS/path-opening vector? | **No risk in current design.** The device is selected by miniaudio/OS automatically with no user-controlled name parameter. This is the correct design. If a future ADR adds device selection (e.g., for audio device enumeration in an options menu), that feature must be reviewed separately. Do not expose device names from external sources to miniaudio device selection without validation. |
| Q5 | Null byte injection in audio paths? | **Use strnlen.** The null-byte scan in the `isAudioPathSafe()` pseudocode is unreachable via C string iteration — C string APIs stop at the first null byte and never read the embedded null. Replace with `strnlen(path, AUDIO_MAX_PATH) >= AUDIO_MAX_PATH`. See MEDIUM-1. |

---

## Advisory Findings (LOW — do not gate implementation)

**LOW-1: UNC path rejection on Windows.**
The `isAudioPathSafe()` check rejects absolute paths starting with `/` or `\` and drive letters (`C:`). It does not explicitly reject UNC paths (`\\server\share\...`). The design is documented as POSIX-primary with `realpath()`. Add rejection of paths where `path[0] == '\\' && path[1] == '\\'` for future Windows portability. Document that `realpath()` is POSIX-only and a porting note will be needed for Windows (`_fullpath()`).

**LOW-2: Ring buffer producer/consumer memory ordering.**
The command ring buffer uses `std::atomic<u32>` head and tail indices with a single producer (main thread) and single consumer (audio callback). The ADR does not specify the memory order for the atomic operations. For SPSC ring buffers, `memory_order_release` on the write side and `memory_order_acquire` on the read side are required to ensure the command payload is visible to the consumer when the index update is observed. Using `memory_order_relaxed` on the indices (as implied by the volume value precedent in the same section) would be a data race on the command payload fields. Required ordering: head write uses `memory_order_release`; tail read uses `memory_order_acquire`. This is a correctness note, not just a performance note — `relaxed` on the indices is a data race on the struct fields. Document the required memory orders in the implementation.

This is filed as LOW (advisory) rather than HIGH because the correct implementation is straightforward and any competent review of the ring buffer code would catch `relaxed` index ordering as incorrect. However, engine-dev should be aware of this requirement before writing the ring buffer.

**LOW-3: miniaudio and stb_vorbis version pinning.**
The version of `miniaudio.h` and `stb_vorbis.h` must be noted in a comment at the top of each header file in `third_party/`. The implementation checklist already mentions this for miniaudio and stb_vorbis but it is worth emphasising: version pinning is the only way to audit the decoder attack surface against published CVEs over time.

**LOW-4: unloadSound() race window documentation.**
The ADR states: "If the sound is currently playing, it is stopped before unloading." The mechanism for this is that `unloadSound()` acquires `m_mutex`, marks the voice as inactive, and then frees the PCM buffer — all under the lock. The audio callback also runs under `m_mutex`. This is correct and safe. However, it should be made explicit in a code comment at the `unloadSound()` implementation: the PCM buffer must only be freed while holding `m_mutex`, and the voice must be deactivated before the buffer is freed, both within the same critical section. Splitting these operations (deactivate voice, release lock, free buffer) would reintroduce the use-after-free window if the callback re-reads a cached pointer. Document the required ordering in code.

**LOW-5: Volume NaN/Inf clamping for the per-instance volume on playSound().**
SEC-6 defines `clampVolume()` and applies it to `setMasterVolume`, `setSoundVolume`, `setMusicVolume`. The `playSound(SoundHandle handle, float volume = 1.0f)` parameter also accepts a per-instance volume. Ensure `clampVolume()` is applied to the `volume` parameter of `playSound()` before it is stored in the voice slot. The ADR's API comment says "Clamped" but the implementation checklist does not explicitly call this out as a separate item. Add it.

---

## Full Findings Reference

| ID | Severity | Summary |
|----|----------|---------|
| HIGH-1 | HIGH | Path concatenation before realpath() can overflow buffer — inherited from ADR-005 HIGH-2; not addressed in ADR-006 checklist |
| HIGH-2 | HIGH | Decoder int outputs must be validated (SEC-5) before u64 cast multiplication (SEC-4) — ordering must be explicit in code |
| MEDIUM-1 | MEDIUM | isAudioPathSafe() uses strlen — must be strnlen(path, AUDIO_MAX_PATH) to protect against non-null-terminated buffers |
| MEDIUM-2 | MEDIUM | stb_vorbis internal codebook heap allocation not bounded by file-size limit alone — document in code, add version pin to checklist |
| LOW-1 | LOW | UNC path rejection missing for Windows portability |
| LOW-2 | LOW | SPSC ring buffer requires release/acquire memory ordering on indices — relaxed ordering on indices is a data race on the command payload |
| LOW-3 | LOW | miniaudio and stb_vorbis version must be pinned in third_party/ header comments |
| LOW-4 | LOW | unloadSound() buffer-free ordering under mutex must be documented in code to prevent future regression |
| LOW-5 | LOW | playSound() per-instance volume must also pass through clampVolume() — not explicitly in checklist |
