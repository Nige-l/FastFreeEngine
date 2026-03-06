# Session 10 Handover

**Written by:** project-manager
**Date:** 2026-03-06
**Session ended:** Session 9
**Session starting:** Session 10

---

## Current State

### Build and Tests
- **224/224 tests pass** on Clang-18 (primary) and GCC-13 (secondary)
- Zero warnings with `-Wall -Wextra` on both compilers
- All examples build clean: hello_sprites, headless_test, interactive_demo, lua_demo (updated with ffe.loadTexture for follower sprite)

### Engine Inventory
- **Core:** types, arena allocator, logging, ECS, application, input, FFE_SYSTEM macro, ShutdownSignal
- **Renderer:** OpenGL 3.3 RHI, sprite batch, render queue, PreviousTransform interpolation, texture loader (stb_image v2.30)
- **Audio:** `ffe::audio` — init/shutdown, loadSound (WAV+OGG), unloadSound, playSound (one-shot), setMasterVolume, headless mode, SPSC command ring buffer, voice pool. miniaudio v0.11.25 + stb_vorbis v1.22 embedded in third_party/.
- **Scripting:** ScriptEngine (LuaJIT), full Lua `ffe.*` API:
  - getTransform, setTransform, requestShutdown, callFunction
  - createEntity, destroyEntity, addTransform, addSprite, addPreviousTransform
  - **NEW Session 9:** loadTexture, unloadTexture
  - isKeyHeld, isKeyPressed, isKeyReleased, getMouseX, getMouseY, KEY_* constants, log
- **ADRs:** 001–006 (006 implemented this session)
- **.context.md:** engine/core/ ✓, engine/renderer/ ✓, engine/scripting/ ✓, engine/audio/ ✓ (new), engine/physics/ placeholder, engine/editor/ placeholder

---

## What Was Completed This Session (9)

1. Audio subsystem (`engine/audio/audio.h` + `.cpp`) — miniaudio + stb_vorbis, headless mode, all ADR-006 security conditions satisfied
2. `ffe.loadTexture(path)` + `ffe.unloadTexture(handle)` Lua bindings
3. `renderer::setAssetRoot()` called in `lua_demo/main.cpp` — Lua texture loading now works end-to-end
4. `engine/audio/.context.md` written
5. `engine/scripting/.context.md` updated with Texture Lifecycle API
6. 28 new tests (total: 224)

---

## Open Known Issues (ordered by priority)

| ID | Priority | Description |
|----|----------|-------------|
| FRICTION-5 | HIGH | No Lua shutdown callback — `ffe.unloadTexture` cannot be called at scene teardown |
| Audio loop/music | MEDIUM | `playMusic(handle, loop)`, `stopMusic()` not yet implemented |
| M-1 getTransform GC | MEDIUM | getTransform allocates Lua table per call — GC pressure at scale |
| M-1 uniform hash | LOW | Renderer uniform cache FNV-1a collision risk (no string verify) |
| flickering | LOW | hello_sprites AMD driver flicker — user reboot pending |

---

## Session 10 Goals

### P0 — Lua shutdown callback (FRICTION-5)

Add support for an optional `shutdown()` global Lua function. When `ScriptEngine::shutdown()` is called, before closing the Lua state, check if `shutdown` is a callable global and call it with no arguments. This allows scripts to call `ffe.unloadTexture()` and perform other cleanup.

**Scope:** Small change — add one `callFunction`-style call at the top of `ScriptEngine::shutdown()`. No new C++ API needed. `engine-dev` can implement without an ADR — just a design decision note.

**Design:** Call the Lua `shutdown()` function (if it exists) before `lua_close(L)`. Use the same `lua_getglobal + lua_isfunction + lua_pcall` pattern as `callFunction`. Do not pass any arguments. Wrap in the instruction budget hook to prevent infinite loops.

### P1 — Audio streaming / loop support

Add to `ffe::audio`:
- `playMusic(SoundHandle, bool loop)` — streaming playback with optional loop
- `stopMusic()` — stop current music track
- `getMusicPosition()` → float (seconds) — for sync

**Note:** This requires architect to design the streaming architecture first (ring buffer from decode thread? stb_vorbis streaming API?). Start with architect design note, then security-auditor shift-left review, then engine-dev implements.

### P2 — performance-critic reviews audio subsystem

The audio implementation has not been reviewed by performance-critic yet. Dispatch `performance-critic` to read `engine/audio/audio.cpp` and report on:
- Voice pool allocation strategy (pre-allocated — good)
- SPSC ring buffer per-frame overhead
- stb_vorbis decode time for one-shot sounds (is it on the main thread?)
- mutex contention in `unloadSound`

### P3 — M-1 getTransform GC batching

Have `architect` write a design note for `ffe.getTransforms({id1, id2, ...})` — batch transform query to reduce per-entity Lua table allocations. Do not implement until architect and performance-critic have reviewed the design.

---

## Agent Dispatch Plan for Session 10

### Phase 1 — Parallel

| Agent | Task | Files |
|-------|------|-------|
| engine-dev | Lua shutdown callback in ScriptEngine::shutdown() | engine/scripting/script_engine.cpp |
| performance-critic | Review audio subsystem performance | Read-only: engine/audio/audio.cpp |
| architect | Audio streaming/music design note | docs/architecture/design-note-audio-streaming.md |

### Phase 2 — Security + API review (after Phase 1)

| Agent | Task | Depends On |
|-------|------|------------|
| security-auditor | Shift-left review of audio streaming design | Phase 1 architect |
| api-designer | Update scripting/.context.md with shutdown() pattern | Phase 1 engine-dev |
| test-engineer | Tests for Lua shutdown callback | Phase 1 engine-dev |

### Phase 3 — Implementation

| Agent | Task | Depends On |
|-------|------|------------|
| engine-dev | Audio streaming/music (playMusic, stopMusic) | Phase 2 security review |

### Phase 4 — Validation

| Agent | Task | Depends On |
|-------|------|------------|
| security-auditor | Post-impl review of streaming | Phase 3 |
| game-dev-tester | Demo with music + shutdown cleanup | Phase 3 |

---

## Build Commands

```bash
# Clang-18 (primary)
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build build
ctest --test-dir build --output-on-failure

# GCC-13 (secondary)
cmake -B build-gcc -G Ninja -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build build-gcc
ctest --test-dir build-gcc --output-on-failure
```

---

## Key File Locations

| What | Where |
|------|-------|
| Engine constitution | `.claude/CLAUDE.md` |
| Audio ADR | `docs/architecture/ADR-006-audio.md` |
| Audio API docs | `engine/audio/.context.md` |
| Scripting API docs | `engine/scripting/.context.md` |
| Audio implementation | `engine/audio/audio.h`, `engine/audio/audio.cpp` |
| Audio tests | `tests/audio/test_audio.cpp` |
| miniaudio | `third_party/miniaudio.h` (v0.11.25) |
| stb_vorbis | `third_party/stb_vorbis.c` (v1.22) |
| Lua demo | `examples/lua_demo/` |
