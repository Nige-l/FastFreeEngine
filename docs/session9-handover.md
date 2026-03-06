# Session 9 Handover

**Written by:** project-manager
**Date:** 2026-03-06
**Session ended:** Session 8
**Session starting:** Session 9

---

## Current State

### Build and Tests
- **196/196 tests pass** on Clang-18 (primary) and GCC-13 (secondary)
- Zero warnings with `-Wall -Wextra` on both compilers
- All examples build clean: hello_sprites, headless_test, interactive_demo, lua_demo (updated with follower AI + SPACE-to-spawn)

### Engine Inventory
- **Core:** types, arena allocator, logging, ECS (EnTT wrapper), application, input system, FFE_SYSTEM macro, ShutdownSignal
- **Renderer:** OpenGL 3.3 RHI, sprite batch, render queue, PreviousTransform interpolation, texture loader (stb_image v2.30)
- **Scripting:** ScriptEngine (LuaJIT, sandboxed), full ECS bindings:
  - getTransform, setTransform, requestShutdown, callFunction
  - **NEW Session 8:** createEntity, destroyEntity, addTransform, addSprite, addPreviousTransform
  - isKeyHeld, isKeyPressed, isKeyReleased, getMouseX, getMouseY, KEY_* constants, log
- **Audio:** placeholder only (engine/audio/) — ADR-006 written and security-reviewed
- **ADRs:** 001–005 implemented, ADR-006 (audio) designed + security-reviewed, implementation pending

---

## What Was Completed This Session (8)

1. `ffe.createEntity()` — create entities from Lua
2. `ffe.destroyEntity(entityId)` — destroy from Lua, safe for invalid IDs
3. `ffe.addTransform(entityId, x, y, rotation, scaleX, scaleY)` — add/overwrite Transform
4. `ffe.addSprite(entityId, texHandle, width, height, r, g, b, a, layer)` — add/overwrite Sprite; NaN-safe color clamp, layer clamped to [0,15]
5. `ffe.addPreviousTransform(entityId)` — opt in to render interpolation from Lua
6. ADR-006 (audio subsystem) designed with miniaudio + stb_vorbis, security reviewed
7. scripting/.context.md updated with full entity lifecycle API documentation
8. lua_demo updated: follower entity chases player, SPACE spawns markers — all from Lua

---

## Open Known Issues (ordered by priority)

| ID | Priority | Description |
|----|----------|-------------|
| FRICTION-4 | HIGH | No `ffe.loadTexture` Lua binding — scripts cannot load their own textures |
| ADR-006 audio | HIGH | Audio subsystem designed + reviewed — NOT YET IMPLEMENTED |
| M-1 | MEDIUM | getTransform allocates Lua table per call — GC pressure at scale |
| flickering | LOW | hello_sprites AMD driver flicker — user reboot needed |

---

## Session 9 Goals

### P0 — Audio subsystem implementation (ADR-006)

The security review is done. Implementation may begin. Read these before starting:
- `docs/architecture/ADR-006-audio.md` — design specification
- `docs/architecture/ADR-006-security-review.md` — conditions to satisfy

**Security conditions that must be satisfied before merge:**

**HIGH-1:** Path concatenation: check `strnlen(assetRoot, PATH_MAX) + 1 + strnlen(path, AUDIO_MAX_PATH) + 1 <= PATH_MAX` before concatenating. Buffer must be `PATH_MAX + 1` bytes.

**HIGH-2:** Decoder output validation ordering: `frameCount > 0`, `channels in {1,2}`, `sampleRate allowlist` must execute BEFORE the `static_cast<u64>` multiplication for decoded-size. Add code comment documenting the ordering dependency.

**MEDIUM-1:** `isAudioPathSafe()` must use `strnlen(path, AUDIO_MAX_PATH) >= AUDIO_MAX_PATH` instead of `strlen` to prevent UB on non-null-terminated buffers.

**MEDIUM-2:** stb_vorbis codebook heap allocation limit documented in a code comment at the decode call site. stb_vorbis version pinned in a comment.

**Library:** miniaudio (single-header, public domain) + stb_vorbis. Embed in `third_party/`. No vcpkg changes needed.

**File layout (from ADR-006):**
```
engine/audio/
  audio.h         — public API (AudioSystem class or ffe::audio namespace)
  audio.cpp       — implementation
third_party/
  miniaudio.h     — embedded (one file, #define MINIAUDIO_IMPLEMENTATION in audio.cpp)
  stb_vorbis.c    — embedded (one file, #define STB_VORBIS_IMPLEMENTATION in audio.cpp)
```

**Dispatch order for audio:**
1. system-engineer: download/embed miniaudio.h and stb_vorbis.c into third_party/ (check they are present first)
2. engine-dev: implement audio.h + audio.cpp satisfying all HIGH+MEDIUM security conditions
3. security-auditor: post-impl review
4. test-engineer: write audio tests (headless — no real audio device needed, use null device)
5. api-designer: write engine/audio/.context.md
6. game-dev-tester: build demo that plays a sound

### P1 — ffe.loadTexture Lua binding

Allow Lua scripts to load textures without C++ pre-loading:

```lua
local handle = ffe.loadTexture("sprites/player.png")
if handle == nil then
    ffe.log("Failed to load texture")
    return
end
ffe.addSprite(entityId, handle, 32, 32, 1, 1, 1, 1, 0)
```

**Design considerations (architect must write a quick design note first):**
- Asset root must be set on C++ side before any Lua texture load
- The binding calls `renderer::loadTexture(path, assetRoot)` — needs the asset root
- Options: (a) Lua calls `ffe.setTextureRoot(path)` first; (b) C++ pre-calls `setAssetRoot` and Lua just calls `loadTexture(relativePath)`; (c) use the existing global asset root from texture_loader
- Path validation: same rules as C++ loadTexture — the binding just forwards to the existing validated function
- Security: this is an attack surface — security-auditor shift-left review required before implementation

### P2 — M-1: getTransform GC pressure (batching design)

Discuss with architect: design `ffe.getTransforms({id1, id2, ...})` returning a table of tables, reducing Lua C API overhead for multi-entity scripts. Do not implement until architect has a design note and performance-critic has reviewed the proposal.

---

## Agent Dispatch Plan for Session 9

### Phase 1 — Parallel

| Agent | Task | Files |
|-------|------|-------|
| system-engineer | Verify/embed miniaudio.h + stb_vorbis.c in third_party/ | third_party/ |
| architect | Quick design note for ffe.loadTexture binding | docs/architecture/design-note-lua-texture-load.md |

### Phase 2 — Security review (after Phase 1 design note)

| Agent | Task | Depends On |
|-------|------|------------|
| security-auditor | Shift-left review of ffe.loadTexture design note | Phase 1 architect note |

### Phase 3 — Implementation (parallel, independent subsystems)

| Agent | Task | Depends On |
|-------|------|------------|
| engine-dev | Implement audio subsystem (ADR-006 + security review conditions) | Phase 1 system-engineer (third_party files) |
| engine-dev | Implement ffe.loadTexture Lua binding | Phase 2 security review PASS |

(These can run in parallel if separate agent instances — audio touches engine/audio/, Lua binding touches engine/scripting/)

### Phase 4 — Reviews

| Agent | Task | Depends On |
|-------|------|------------|
| security-auditor | Post-impl review of audio implementation | Phase 3 audio |
| security-auditor | Post-impl review of ffe.loadTexture | Phase 3 binding |
| test-engineer | Audio tests + ffe.loadTexture tests | Phase 3 both |
| api-designer | engine/audio/.context.md + update scripting/.context.md | Phase 3 both |

### Phase 5 — Demo

| Agent | Task | Depends On |
|-------|------|------------|
| game-dev-tester | Demo: Lua creates entities, loads textures, plays sound | Phase 4 all |

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

# Check for miniaudio/stb_vorbis in third_party:
ls /home/nigel/FastFreeEngine/third_party/
```

---

## Key File Locations

| What | Where |
|------|-------|
| Engine constitution | `.claude/CLAUDE.md` |
| ADR-006 audio design | `docs/architecture/ADR-006-audio.md` |
| ADR-006 security review | `docs/architecture/ADR-006-security-review.md` |
| Entity-from-Lua design | `docs/architecture/design-note-entity-from-lua.md` |
| Scripting API docs | `engine/scripting/.context.md` |
| Audio placeholder | `engine/audio/` |
| Lua demo | `examples/lua_demo/` |
| Tests | `tests/` |
