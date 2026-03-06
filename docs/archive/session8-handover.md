# Session 8 Handover

**Written by:** project-manager
**Date:** 2026-03-06
**Session ended:** Session 7
**Session starting:** Session 8

---

## Current State

### Build and Tests
- **177/177 tests pass** on Clang-18 (primary) and GCC-13 (secondary)
- Zero warnings with `-Wall -Wextra` on both compilers
- All examples build clean: hello_sprites, headless_test, interactive_demo, lua_demo

### Engine Inventory
- **Core:** types, arena allocator, logging, ECS (EnTT wrapper), application, input system, system.h with FFE_SYSTEM macro, ShutdownSignal in ECS context
- **Renderer:** OpenGL 3.3 RHI, sprite batch (2048/batch), render queue with packed u64 sort keys, PreviousTransform interpolation, texture loader (stb_image v2.30)
- **Scripting:** ScriptEngine (LuaJIT raw C API, sandboxed), ECS bindings (getTransform, setTransform, isKeyHeld, isKeyPressed, isKeyReleased, getMouseX, getMouseY, KEY_* constants, requestShutdown), callFunction per-frame API
- **Assets:** assets/textures/white.png, assets/textures/checkerboard.png
- **Examples:** hello_sprites (static scene), headless_test (CI-safe), interactive_demo (C++ WASD), lua_demo (Lua WASD + callFunction + requestShutdown)
- **ADRs:** 001 (core skeleton), 002 (renderer RHI), 003 (input system), 004 (Lua scripting), 005 (texture loading)
- **.context.md files:** engine/core/ (complete), engine/renderer/ (complete), engine/scripting/ (complete — Session 7 revision), engine/audio/ (placeholder), engine/physics/ (placeholder), engine/editor/ (placeholder)

---

## What Was Completed This Session (7)

1. `ScriptEngine::callFunction(funcName, entityId, dt)` — per-frame Lua function invocation using lua_getglobal + lua_pcall (no recompile per call). Resolves FRICTION-1 HIGH from game-dev-tester.
2. `ffe.requestShutdown()` Lua binding — scripts can exit the engine cleanly. Null-guard for unregistered World (security-auditor fix).
3. **Jitter fix** — root cause: interpolation alpha was discarded in Application::render(). Fixed with:
   - `PreviousTransform` component (mirrors Transform fields)
   - `copyTransformSystem` (priority 5, before gameplay)
   - `renderPrepareSystem` moved outside registered system list, called from `Application::render(alpha)` directly
   - Two-pass: lerped for entities with PreviousTransform, raw for entities without
4. api-designer updated scripting/.context.md with callFunction and ffe.requestShutdown documentation
5. game-dev-tester validated lua_demo — no blockers

---

## Open Known Issues (ordered by priority)

| ID | Priority | Description | Owner |
|----|----------|-------------|-------|
| FRICTION-3 | HIGH | Entity creation from Lua — `ffe.createEntity()`, `ffe.destroyEntity()`, `ffe.addSprite()` not yet available | engine-dev |
| M-1 | MEDIUM | `getTransform` allocates a Lua table per call — GC pressure at scale | engine-dev |
| M-1 (hash) | LOW | Uniform cache hash collisions possible (FNV-1a, no string verify) | renderer-specialist |
| flickering | LOW | hello_sprites intermittent flicker — hardware (AMD driver). User reboot needed | user |

---

## Session 8 Goals

### P0 — Entity lifecycle from Lua (FRICTION-3)

Add to `ffe.*` Lua bindings:
- `ffe.createEntity()` → returns integer entity ID
- `ffe.destroyEntity(entityId)` — safe no-op for invalid IDs
- `ffe.addSprite(entityId, textureHandle, width, height, r, g, b, a, layer)` — attach Sprite component
- `ffe.addTransform(entityId, x, y, rotation, scaleX, scaleY)` — attach Transform component
- `ffe.addPreviousTransform(entityId)` — opt in to render interpolation from Lua

**Security:** architect must write shift-left design note before implementation. Key questions: can a Lua script destroy entities it doesn't own? Entity ID validation is already in place — extend it to the new bindings.

**Dispatch order:** architect → security-auditor review → engine-dev implements → security-auditor post-impl review → test-engineer → api-designer updates .context.md → game-dev-tester writes demo.

### P1 — Audio subsystem design (ADR-006)

- architect designs audio subsystem: OpenAL or miniaudio, LEGACY tier compatible, streaming vs one-shot
- Security consideration: audio file loading (ogg/wav parsing) is an attack surface — security-auditor shift-left review of ADR-006 required before implementation
- Placeholder at engine/audio/ already exists

### P2 — M-1: getTransform GC pressure

- Explore batching: a `ffe.getTransforms(entityIdTable)` call that returns multiple transforms per Lua call, reducing the per-entity overhead. Or a Lua-side transform cache with explicit invalidation.
- This is a research task — architect should weigh options before engine-dev implements anything.

### P3 — Uniform cache hash collision (M-1 hash, renderer)

- Renderer-specialist: evaluate FNV-1a collision risk in uniform cache. Options: add string verification in slow path, use a sorted array with binary search, or accept the risk for current entity counts with documentation.

---

## Agent Dispatch Plan for Session 8

### Phase 1 — Parallel (no dependencies between these)

| Agent | Task | Files |
|-------|------|-------|
| architect | ADR-006 audio subsystem design | docs/architecture/006-audio.md |
| architect | Entity-from-Lua design note | docs/architecture/design-note-entity-from-lua.md |
| renderer-specialist | M-1 hash: evaluate + fix uniform cache collision risk | engine/renderer/opengl/rhi_opengl.cpp |

### Phase 2 — Sequential (after architect design notes)

| Agent | Task | Depends On |
|-------|------|------------|
| security-auditor | Shift-left review of entity-from-Lua design | Phase 1 architect note |
| security-auditor | Shift-left review of ADR-006 audio | Phase 1 ADR-006 |

### Phase 3 — Sequential (after security-auditor clears)

| Agent | Task | Depends On |
|-------|------|------------|
| engine-dev | Implement entity lifecycle Lua bindings | Phase 2 security review PASS |

### Phase 4 — Sequential (after implementation)

| Agent | Task | Depends On |
|-------|------|------------|
| security-auditor | Post-impl review of Lua entity bindings | Phase 3 |
| test-engineer | Tests for ffe.createEntity/destroyEntity/addSprite | Phase 3 |
| api-designer | Update scripting/.context.md | Phase 3 |

### Phase 5 — Demo

| Agent | Task | Depends On |
|-------|------|------------|
| game-dev-tester | Build demo: Lua creates/destroys entities at runtime | Phase 4 |

---

## Build Commands (for reference)

```bash
# Clang-18 (primary)
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build build
ctest --test-dir build --output-on-failure

# GCC-13 (secondary)
cmake -B build-gcc -G Ninja -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build build-gcc

# Run headless (CI-safe, no display needed)
./build/examples/headless_test/ffe_headless_test

# Run lua_demo (needs display or xvfb — WASD to move, ESC to quit)
./build/examples/lua_demo/ffe_lua_demo
```

---

## Key File Locations

| What | Where |
|------|-------|
| Engine constitution | `.claude/CLAUDE.md` |
| Agent definitions | `.claude/agents/` |
| Architecture ADRs | `docs/architecture/` |
| Devlog | `docs/devlog.md` |
| Scripting API docs | `engine/scripting/.context.md` |
| Core API docs | `engine/core/.context.md` |
| Renderer API docs | `engine/renderer/.context.md` |
| Lua demo | `examples/lua_demo/` |
| PNG assets | `assets/textures/` |
| Tests | `tests/` |
