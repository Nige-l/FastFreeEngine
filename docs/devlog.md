# FastFreeEngine Development Log

> **Quick context:** Read `docs/project-state.md` first — it has the full project state in under 100 lines.
> **Archive:** Sessions 1-50 are in `docs/devlog-archive.md`.

## 2026-03-07 — Session 69: Phase 6 M3 (part 1) — Level 2 "The Temple"

### Summary

Session 69 delivered the first half of Phase 6 Milestone 3: Level 2 "The Temple" — an underground temple environment with dark atmospheric lighting, lava pit, crystal pedestals, narrow bridges, 2 purple guardians, and an artifact on a central altar. Also fixed macOS CI by making vcpkg overlay always-on (upstream LuaJIT port broken on arm64-osx). FAST build: 1005 tests pass on Clang-18, zero warnings.

### Planned

- Level 2 "The Temple" — underground environment with dark lighting, hazards, guardians
- macOS CI fix (vcpkg overlay)

### Delivered

- **Level 2 "The Temple"** — 532-line underground temple level with dark atmospheric lighting (low ambient 0.05/0.05/0.08), lava pit with orange glow lights, crystal pedestals with blue/purple/green lights, narrow stone bridges, 2 purple guardians patrolling corridors, artifact on central altar. Short-range dark fog (dark blue/purple, 3-18 range). Files: `examples/showcase/levels/level2.lua`, `examples/showcase/game.lua` (Level 2 added to LEVELS table).
- **macOS CI fix (take 2)** — Made vcpkg overlay always-on instead of conditional on MinGW. Upstream LuaJIT port is broken on arm64-osx, so the overlay is needed everywhere. Files: `CMakeLists.txt`, `docs/environment.md`.

### Reviews

- performance-critic: PASS (57/80 entities, 4/4 lights, 31/40 bodies)
- api-designer: PASS (all ffe.* calls verified, 1 unused variable noted, 2 doc gaps noted)
- security-auditor: SKIPPED (no new attack surface)
- game-dev-tester: SKIPPED (no new API paradigm)

### Critical User Feedback (HIGH PRIORITY for Session 70)

1. **"The showcase needs to be EPIC"** — User wants REAL 3D models from CC0 sources (Kenney, Quaternius, etc.). Cubes are NOT acceptable. This is the #1 priority.
2. **"We have music"** — User has Suno tracks in `assets/audio/` (BattleMusic.mp3, music_pixelcrown.ogg, Pixel Crown.wav, etc.). Use these.
3. **"Be conscious of disk space"** — Selective downloads, no massive packs.
4. **"Exclude models from build"** — Downloaded assets should be copied, not compiled.
5. **GitHub Pages showing just README** — `jekyll-gh-pages.yml` (commit e935f47) overrides MkDocs deploy. Must be deleted.

### Next Session (70)

Priority: Download real CC0 3D models (Kenney .glb packs, Quaternius characters), wire up existing Suno music tracks, fix GitHub Pages by removing Jekyll workflow, Level 2 gameplay (crystal puzzle, timed platforms, boss guardian). The user wants EPIC visuals — colored cubes must go.

---

## 2026-03-07 — Session 68: Phase 6 M2 — Level 1 "The Courtyard"

### Summary

Session 68 delivered Phase 6 Milestone 2: Level 1 "The Courtyard" is now a complete, playable level in the "Echoes of the Ancients" showcase game. Also fixed macOS CI by making the vcpkg overlay port conditional on MinGW targets. FAST build: 1005 tests pass on Clang-18, zero warnings.

### Planned

- Level 1 "The Courtyard" — full gameplay (puzzles, combat, artifact collection)
- macOS CI fix (overlay port shadowing upstream LuaJIT on non-MinGW platforms)

### Delivered

- **Level 1 "The Courtyard"** — Complete playable level with push-block puzzle (2 blocks, 2 pressure plates, 1 gate), 2 guardian enemies with patrol/chase AI, destructible wall hiding the artifact, fog, directional shadows, 4 point lights (torches). Files: `examples/showcase/levels/level1.lua`.
- **Game flow updates** — `game.lua` updated with level cleanup on transitions, level name display, completion delay, proper shutdown cleanup. `lib/hud.lua` updated with dt-based prompt fade (replacing ffe.after timer approach).
- **Assets** — cube.glb model, PressStart2P TTF font, placeholder audio files (courtyard music, collect/gate/hit SFX). `examples/showcase/ASSETS.md` documents asset sources and licenses.
- **CMakeLists** — `examples/showcase/CMakeLists.txt` updated to copy assets to build directory alongside Lua scripts.
- **macOS CI fix** — Made `VCPKG_OVERLAY_PORTS` conditional on `VCPKG_TARGET_TRIPLET` matching `*mingw*`. Previously the overlay unconditionally shadowed the upstream LuaJIT port, breaking macOS arm64 builds. Files: `CMakeLists.txt`, `docs/environment.md`.

### Reviews

- performance-critic: PASS (38/80 entities, 4/4 lights, 34/40 physics bodies — within LEGACY budget)
- api-designer: PASS (all ffe.* calls verified correct)
- security-auditor: SKIPPED (no new attack surface)
- game-dev-tester: SKIPPED (no new API paradigm — all calls use existing patterns)

### Deferred

- Net Arena 's' key issue — user-reported, investigate in a future session
- Unreal project porting tools — long-term roadmap item
- GitHub Pages deployment — user enabled Pages; existing MkDocs workflow should deploy on push

### Next Session (69)

Phase 6 M3 (first half): Level 2 "The Temple" — underground scene with dark lighting (minimal ambient, point lights from crystals and lava glow), fog (dark, short range), pillars/bridges/lava pit/crystal pedestals, particles (lava bubbles, crystal sparkle), dark ambient audio. Per ADR Section 3.2.

---

## 2026-03-07 — Session 67: Phase 6 M1 — Linear Fog + Showcase Scaffold

### Summary

Session 67 delivered Phase 6 Milestone 1: linear fog in the Blinn-Phong shader and the "Echoes of the Ancients" showcase game scaffold. FAST build: 1005 tests pass on Clang-18, zero warnings.

### Planned

- Linear fog shader (ffe.setFog / ffe.disableFog)
- Showcase game project scaffold with player controller, camera, combat, AI, HUD
- Asset acquisition plan

### Delivered

- **Linear fog** -- FogParams struct (color, near, far), Blinn-Phong fragment shader integration, `ffe.setFog(r, g, b, near, far)` and `ffe.disableFog()` Lua bindings, 14 Catch2 tests. Files: `engine/renderer/mesh_renderer.h/cpp`, `engine/renderer/shader_library.cpp`, `engine/core/application.h/cpp`, `engine/scripting/script_engine.cpp`, `tests/renderer/test_fog.cpp`.
- **Showcase scaffold** -- "Echoes of the Ancients" project structure under `examples/showcase/`. Includes `game.lua` (main menu + level sequencing), `lib/player.lua` (WASD + gamepad movement, jump, orbit camera), `lib/camera.lua` (orbit camera with collision avoidance), `lib/hud.lua` (health bar, artifact count, interaction prompts), `lib/combat.lua` (melee attack, damage, health system), `lib/ai.lua` (patrol + chase AI state machine), `levels/test_level.lua` (test environment).
- **Asset plan** -- `examples/showcase/ASSETS.md` documenting CC0 asset sources (Kenney, Quaternius, ambientCG, OpenGameArt).
- **Doc updates** -- `engine/renderer/.context.md` (fog API), `engine/scripting/.context.md` (fog bindings).

### Reviews

- performance-critic: PASS
- api-designer: PASS (remediation: fog docs added to scripting .context.md)
- security-auditor: SKIPPED (no attack surface)
- game-dev-tester: SKIPPED (no new API paradigm)

### Deferred

- Net Arena 's' key issue -- user-reported, investigate in a future session
- GitHub Pages 404 -- user must enable Pages in repo Settings > Pages (not a code fix)

### Next Session (68)

Phase 6 M2 (first half): Level 1 "The Courtyard" -- download CC0 assets, build the courtyard environment with walls/floor/archways/fountain/torches, set up lighting (golden-hour directional + shadows + torch point lights), skybox, particle effects (fire, dust), spatial audio, and enemy encounters. The player should be able to walk through, fight guardians, solve the push-block puzzle, and collect the artifact.

---

## 2026-03-07 — Session 66: Editor Crash Fix, macOS CI, README Overhaul, Phase 6 ADR

### Summary

Session 66 fixed the editor crash caused by ImGui key handling migration, repaired macOS CI for LuaJIT on arm64, overhauled the README to reflect the full engine (all 5 phases), and produced the Phase 6 "Echoes of the Ancients" ADR. FAST build: 991 tests pass on Clang-18, zero warnings.

### Planned

- Fix editor crash (ImGui key handling)
- Fix macOS CI (LuaJIT arm64)
- Overhaul README
- Design Phase 6 showcase game

### Delivered

- **Editor crash fix** -- Migrated ShortcutManager from legacy int key indices to ImGuiKey enums. Fixed GLFW callback overwrite in editor_app. Updated `engine/core/input.h/cpp`, `engine/editor/editor.cpp`, `editor/editor_app.cpp`, `editor/input/shortcut_manager.h/cpp`, `tests/editor/test_shortcuts.cpp`.
- **macOS CI fix** -- LuaJIT vcpkg overlay port corrected for arm64-osx cross-compilation. Updated `cmake/vcpkg-overlays/ports/luajit/portfile.cmake` and `configure`.
- **README overhaul** -- Comprehensive rewrite reflecting 991 tests, ~167 Lua bindings, editor, networking, website, all 5 phases, hardware tier system.
- **Phase 6 ADR** -- "Echoes of the Ancients" 3D showcase game: 3-level action-exploration, 8-10 session estimate, linear fog as only engine enhancement. Written to `docs/architecture/adr-phase6-showcase.md`.
- **Doc fixes** -- `editor/.context.md` (ShortcutManager ImGuiKey), `docs/architecture-map.md` (binding count), `docs/environment.md` (diagnostics).

### Reviews

- performance-critic: PASS
- api-designer: PASS (minor issues fixed in remediation)
- security-auditor: SKIPPED (no attack surface changes)
- game-dev-tester: SKIPPED (no new API paradigms)

### User Directives for Autonomous Development

The user has provided long-term directives for continuous autonomous development:
1. Don't stop after Phase 6 -- keep planning and improving
2. GitHub site should have polished tutorials with navigation
3. Cool demos after each phase
4. Sample for each hardware tier
5. Competitive analysis: what do competitors offer that FFE doesn't?
6. Xbox controller support in game demos
7. Utilize GitHub's free features for open source
8. After each phase, PM updates README with new screenshots

### Next Session

Session 67: Phase 6 M1 -- Linear fog shader + showcase project scaffold + player controller prototype. Per ADR Section 8, fog is the only engine C++ change; the rest is Lua game code scaffolding.

### Devlog Maintenance

Archived Sessions 35-50 to `docs/devlog-archive.md` (now covers Sessions 1-50).

---

## 2026-03-07 — Session 51: Phase 3 Kickoff — Standalone Editor Milestone 1

### Summary

Session 51 kicked off Phase 3 (Standalone Editor). Delivered Milestone 1: a fully functional editor scaffold with ImGui, scene serialisation, inspector panel, scene hierarchy, and an undo/redo command system. FULL build passed: 766 tests on both Clang-18 and GCC-13, zero warnings.

### New Subsystems

- **Editor application** (`editor/`) — separate binary from game runtime. ImGui dockspace layout with menu bar, panels for scene hierarchy, inspector, and viewport (placeholder). Links against engine and ImGui.
- **Scene serialisation** (`engine/scene/`) — `SceneSerialiser` with JSON save/load. Security hardening: entity count limits, NaN/Inf rejection, path traversal rejection, file size limits.
- **Editor-hosted mode** — `Application` gained `initSubsystems()`, `shutdownSubsystems()`, `tickOnce()`, `renderOnce()`, `setWindow()` for editor control of the engine lifecycle.

### Editor Features

- **Scene hierarchy panel** — lists all entities by Name component (or "Entity N" fallback), click-to-select, right-click context menu for create/delete
- **Inspector panel** — editable Transform/Transform3D/Name fields, display-only Sprite/Material3D. Wired to command system for undo.
- **Command system** — `CommandHistory` with 256-depth bounded deque, `ICommand` interface with execute/undo. Entity create/destroy commands snapshot all components for full undo fidelity.
- **Viewport panel** — placeholder ready for FBO rendering in Milestone 2

### ECS Additions

- `Name`, `Parent`, `Children` components added to `render_system.h` for scene graph support

### Architecture

- ADR: `docs/architecture/adr-editor-architecture.md` — documents editor-hosted mode, panel architecture, command pattern, serialisation security model

### Tests

- 28 new tests across `tests/editor/` and `tests/scene/`
- **766 tests** total, passing on both Clang-18 and GCC-13, zero warnings

### Documentation

- 3 `.context.md` files: `editor/.context.md`, `engine/scene/.context.md`, updated `engine/core/.context.md`

### Reviews

- performance-critic: MINOR ISSUES (approved) — no blocking concerns
- security-auditor: MINOR ISSUES (approved) — serialisation security model solid
- api-designer: clean
- game-dev-tester: SKIP — editor-hosted mode API is internal to editor, not a new game developer-facing paradigm. Will invoke when play-in-editor is implemented.

### Build

- FULL build: 766 tests on Clang-18 + GCC-13, zero warnings, zero failures

---

