# Session 11 Handover

**Written by:** project-manager
**Date:** 2026-03-06
**Session ended:** Session 10
**Session starting:** Session 11

---

## Current State

### Build and Tests
- **228/228 tests pass** on Clang-18 (primary) and GCC-13 (secondary)
- Zero warnings with `-Wall -Wextra` on both compilers

### Engine Inventory
- **Core:** types, arena, logging, ECS, application, input, FFE_SYSTEM, ShutdownSignal
- **Renderer:** OpenGL 3.3, sprite batch, render queue, PreviousTransform interpolation, texture loader
- **Audio:** `ffe::audio` — init/shutdown, loadSound (WAV+OGG), unloadSound, playSound, setMasterVolume, headless mode
- **Scripting:** Full Lua `ffe.*` API including: getTransform, setTransform, createEntity, destroyEntity, addTransform, addSprite, addPreviousTransform, loadTexture, unloadTexture, requestShutdown, callFunction, log, KEY_* constants, isKeyHeld/Pressed/Released, getMouseX/Y. **Lifecycle callbacks:** `update(entityId, dt)` (via callFunction), `shutdown()` (auto-called by ScriptEngine::shutdown)
- **.context.md:** core ✓, renderer ✓, scripting ✓ (Session 10 update), audio ✓, physics placeholder, editor placeholder

---

## What Was Completed This Session (10)

1. Lua `shutdown()` callback — ScriptEngine calls it before closing Lua state
2. `lua_demo/game.lua` updated — `shutdown()` calls `ffe.unloadTexture`
3. performance-critic reviewed audio — MINOR ISSUES (3, non-blocking, tracked)
4. scripting/.context.md updated — lifecycle callbacks documented
5. 228/228 tests, both compilers clean

---

## Open Known Issues

| ID | Priority | Description |
|----|----------|-------------|
| Audio M-1 | MEDIUM | Mutex acquired twice per audio callback (command drain + mixing) |
| Audio M-2 | MEDIUM | `getActiveVoiceCount()` acquires mutex + linear scan — replace with atomic |
| Audio M-3 | LOW | Redundant modulo in ring-full check |
| Audio music | HIGH | No playMusic/stopMusic — one-shot only |
| M-1 getTransform | MEDIUM | getTransform allocates Lua table per call — GC pressure at scale |

---

## Session 11 Goals

### P0 — Audio performance fixes (M-1, M-2)

These are in-engine fixes only — no new public API. `engine-dev` can implement without an ADR:

**M-2 fix (easy):** Replace the linear voice count scan in `getActiveVoiceCount()` with a `std::atomic<u32>` counter maintained alongside voice activation/deactivation. Update the comment in `audio.h` to reflect that this is now safe to call per-frame.

**M-1 fix (medium):** Reduce mutex acquisitions in the audio callback. Options:
- Drain commands using the SPSC ring buffer atomics (no mutex) — the ring buffer is already lock-free; only the voice pool modification needs the mutex
- Batch voice pool changes outside the mixing loop, take the mutex once

Read the implementation carefully before changing anything. These are concurrent-access changes — correctness is paramount.

### P1 — Audio streaming/music design + implementation

**Step 1: architect** writes `design-note-audio-streaming.md` covering:
- stb_vorbis streaming API vs `stb_vorbis_decode_filename` (decode-all upfront vs stream)
- Music track state machine: stopped, playing, fading, looping
- `playMusic(SoundHandle, bool loop)`, `stopMusic()`, `getMusicVolume()`, `setMusicVolume(float)`
- Thread safety: music decode runs on audio callback thread or a dedicated decode thread?
- Memory budget: one music track at a time (LEGACY), streaming from memory (decoded OGG)

**Step 2: security-auditor** shift-left review of design note (music is not a new attack surface — the loadSound path traversal is already hardened — but confirm)

**Step 3: engine-dev** implements from design note

**Step 4: test-engineer** writes tests (headless — null device)

**Step 5: api-designer** updates `engine/audio/.context.md`

### P2 — getTransform GC batching (architect design note)

Architect writes `design-note-gettransforms-batch.md`:
- Proposed: `ffe.getTransforms({id1, id2, ...})` returning a table of tables
- Alternative: pre-allocated Lua table reused per call
- Performance impact estimate: how many getTransform calls/frame to hit visible GC?
- Recommendation: implement or defer?

---

## Agent Dispatch Plan for Session 11

### Phase 1 — Parallel (no dependencies)

| Agent | Task |
|-------|------|
| engine-dev | Audio perf fixes: M-2 (atomic voice count), M-1 (reduce callback mutex) |
| architect | design-note-audio-streaming.md |
| architect | design-note-gettransforms-batch.md |

### Phase 2 — Sequential (after Phase 1 architect notes)

| Agent | Task | Depends On |
|-------|------|------------|
| security-auditor | Shift-left review of audio streaming design | Phase 1 architect streaming note |
| performance-critic | Review M-1/M-2 engine-dev fixes | Phase 1 engine-dev |

### Phase 3 — Implementation (after security review)

| Agent | Task | Depends On |
|-------|------|------------|
| engine-dev | Audio streaming/music implementation | Phase 2 security review PASS |

### Phase 4 — Validation

| Agent | Task | Depends On |
|-------|------|------------|
| security-auditor | Post-impl audio streaming review | Phase 3 |
| test-engineer | Streaming + M-1/M-2 tests | Phase 3 |
| api-designer | Update engine/audio/.context.md | Phase 3 |
| game-dev-tester | Demo with looping music | Phase 3 |

---

## Build Commands

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build build
ctest --test-dir build --output-on-failure

cmake -B build-gcc -G Ninja -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build build-gcc
ctest --test-dir build-gcc --output-on-failure
```
