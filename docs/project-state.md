# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 68 |
| Total tests | 1005 (FAST build: Clang-18, zero warnings) |
| Total Lua bindings | ~169 |
| Phase 1 (2D Foundation) | COMPLETE (Linux) |
| Phase 2 (3D Foundation) | COMPLETE |
| Phase 3 (Standalone Editor) | MVP COMPLETE (6 milestones, Sessions 51-56) |
| Phase 4 (Networking) | COMPLETE (Sessions 57-60) |
| Phase 5 (Website/Learning) | COMPLETE (Sessions 62-65) |
| Windows build | DONE (MinGW-w64 cross-compilation) |
| macOS build | DONE (arm64 + x86_64) |
| GitHub Actions CI | DONE (Linux Clang-18, Linux GCC-13, macOS arm64) |

## Engine Subsystems

| Subsystem | Status | Key APIs |
|-----------|--------|----------|
| ECS | Stable | World, createEntity/destroyEntity, function-pointer systems |
| Renderer (2D) | Stable | OpenGL 3.3, sprite batching (2048/batch), render queue, rotation+flip |
| Renderer (3D) | Stable | cgltf .glb, Blinn-Phong, Transform3D/Mesh/Material3D, point lights (4 max), specular/normal maps, skeletal animation, linear fog |
| Shadow Mapping | Stable | Depth FBO, PCF 3x3, enableShadows/disableShadows/setShadowBias/setShadowArea |
| Skybox | Stable | Cubemap loading (6-face), SKYBOX shader, loadSkybox/unloadSkybox/setSkyboxEnabled |
| Text (bitmap) | Stable | 8x8 bitmap font atlas, drawText, drawRect |
| Text (TTF) | Stable | stb_truetype, loadFont/drawFontText/measureText, 8 font slots |
| Sprite Animation | Stable | SpriteAnimation component, grid atlas |
| Tilemap | Stable | Tilemap component, renderTilemaps, 1024x1024 max |
| Particles | Stable | ParticleEmitter, 128 inline pool, gravity/color/size interp |
| Scene Serialisation | Stable | SceneSerialiser — saveScene/loadScene (JSON), entity count limits, NaN rejection, path traversal rejection |
| Editor | MVP Complete | Standalone binary, ImGui dockspace, scene hierarchy tree (parent/child), inspector, undo/redo (entity + component + inspector fields + add/remove + reparent), FBO viewport, file dialogs, play-in-editor (snapshot/restore), asset browser with drag-and-drop, viewport gizmos (translate/rotate/scale), keyboard shortcuts, component add/remove, build pipeline (game export) |
| Runtime | Stable | ffe_runtime generic Lua game runner, ffe.loadSceneJSON for exported scenes |
| Scene Graph | Stable | setParent/removeParent/getParent/getChildren/isRoot/isAncestor/getRootEntities, circular parenting prevention |
| Scene Mgmt | Stable | destroyAllEntities, cancelAllTimers, loadScene |
| Input | Stable | keyboard+mouse+gamepad, pressed/held/released, action bindings |
| Audio | Stable | miniaudio, WAV/OGG, playSound/playMusic, streaming, headless, 3D positional (playSound3D, listener sync) |
| Physics (3D) | Stable | Jolt Physics, rigid bodies, collision callbacks, raycasting, entity-body mapping, 13 Lua bindings |
| Collision | Stable | Spatial hash, AABB/Circle, layer/mask, CollisionEvent |
| Scripting | Stable | LuaJIT sandbox, 1M instruction budget, ffe.* API |
| Save/Load | Stable | JSON on disk, path security, atomic writes |
| Timers | Stable | ffe.after/every/cancelTimer, 256 max, fixed-size array |
| Camera | Stable | CameraShake, ClearColor, set3DCameraFPS/Orbit |
| Screenshot | Stable | glReadPixels, PNG via stb_image_write, ffe.screenshot |
| Networking | Stable | ENet transport, PacketReader/Writer, rate limiting, ReplicationRegistry (32 types, snapshot serialization, slerp interpolation), NetworkServer (authoritative broadcast, input processing, lobby management), NetworkClient (snapshot receiving, interpolation, prediction, lobby client), ClientPrediction (64-slot ring buffer, reconciliation), LobbyServer/LobbyClient (create/join/leave/ready/start), LagCompensator (64-frame history, ray-vs-sphere hit check), network_system module, 30 Lua bindings |

## Demo Games

1. **Collect Stars** — top-down 2D, Lua-only
2. **Pong** — classic 2D, Lua-only
3. **Breakout** — brick-breaker 2D, Lua-only
4. **3D Demo** — mesh loading, Blinn-Phong, point lights, materials, shadows, skybox
5. **Net Arena** — 2D multiplayer arena, client-side prediction, server reconciliation
6. **Echoes of the Ancients** — 3D showcase (IN PROGRESS, scaffold + test level)

## Known Issues / Deferred Items

- UNC path (`\\server\share`) blocking on Windows — comment-only, no test env
- MSVC native build support — deferred (MinGW cross-compile works)
- set3DCameraFPS lacks NaN/Inf guards (MINOR — set3DCameraOrbit has them; backlog)
- NAT traversal / relay server — deferred to backlog (relay is infrastructure/ops, not engine library code; ENet direct connect covers LAN and public IP scenarios)

## Recent Sessions (last 5)

| Session | Summary |
|---------|---------|
| 68 | Phase 6 M2: Level 1 "The Courtyard" (push-block puzzle, 2 guardians, destructible wall, artifact, fog+shadows+4 lights), macOS CI fix (conditional overlay ports). 1005 tests (FAST). |
| 67 | Phase 6 M1: linear fog shader (ffe.setFog/disableFog, 14 tests), "Echoes of the Ancients" scaffold (player, camera, HUD, combat, AI, test level). 1005 tests (FAST). |
| 66 | Editor crash fix (ImGuiKey migration), macOS CI fix (LuaJIT arm64), README overhaul, Phase 6 ADR ("Echoes of the Ancients"). 991 tests (FAST). |
| 65 | **Phase 5 COMPLETE.** "Build Your Own Engine" learning track (ECS from Scratch tutorial), community game showcase, nav updates. All 5 phases complete. 991 tests. |
| 64 | Phase 5 deep dives — ECS, renderer, networking "How It Works" pages (~1243 lines total), GitHub Pages deployment workflow, Mermaid diagram support. 991 tests (FAST). |

## Phase 2 — 3D Foundation: COMPLETE

**Goal:** Extend the renderer to support 3D games while keeping full 2D capability.

All deliverables met:
- [x] 3D mesh loading (cgltf, .glb format)
- [x] 3D mesh rendering (Blinn-Phong, vertex/index buffers)
- [x] Basic lighting (directional + point lights, Blinn-Phong)
- [x] 3D camera (FPS and orbit modes)
- [x] Materials system (diffuse, specular, normal maps, shininess)
- [x] Skeletal animation (bone hierarchy, GPU skinning)
- [x] 3D physics integration (Jolt — rigid bodies, collision callbacks, raycasting)
- [x] Skybox / cubemap environment rendering
- [x] Shadow mapping (depth FBO, PCF 3x3)
- [x] 3D positional audio (spatial voices, listener sync)
- [x] Lua bindings for all 3D features (~45 bindings)
- [x] 3D demo game
- [x] .context.md files for all new subsystems

## Phase 3 — Standalone Editor: MVP COMPLETE

**Goal:** A graphical application for building games with FFE, like Unity or Unreal Editor.

### Delivered (6 milestones, Sessions 51-56)
- [x] Standalone editor application (separate binary from the game runtime) — M1
- [x] Scene serialisation (save/load scene files) — M1
- [x] Entity inspector (create, modify, delete entities and components) — M1
- [x] Undo/redo system (entity, component, inspector, add/remove, reparent) — M1-M5
- [x] Scene view with 2D and 3D viewport (FBO rendering) — M2
- [x] File dialogs for Open/Save Scene — M2
- [x] Play-in-editor (snapshot/restore, Play/Pause/Resume/Stop) — M3
- [x] Asset browser (directory traversal, file type indicators, security boundary) — M3
- [x] Viewport gizmos (translate/rotate/scale, axis constraints, undo integration) — M4
- [x] Keyboard shortcuts (ShortcutManager, 7 default bindings) — M4
- [x] Component add/remove from inspector (undoable) — M4
- [x] Scene hierarchy tree (parent/child relationships, drag reorder) — M5
- [x] Drag-and-drop (asset browser to inspector for texture/mesh assignment) — M5
- [x] Build pipeline (game export, runtime binary, ffe.loadSceneJSON) — M6

### Backlog (polish, not blocking Phase 4)
- [ ] Editor preferences persistence
- [ ] Project creation wizard
- [ ] LLM integration panel (connect AI assistant, generate code, explain systems)

## Phase 4 — Networking and Multiplayer: COMPLETE

**Goal:** Built-in multiplayer support for both 2D and 3D games.

### Delivered (Sessions 57-60)
- [x] Network transport (ENet wrapper — ServerTransport/ClientTransport, function-pointer callbacks)
- [x] Packet system (PacketReader/Writer, bounds-checked, NaN/Inf rejection, 1200-byte MTU limit)
- [x] Network security: packet validation, per-connection rate limiting
- [x] Networking ADR (`docs/architecture/adr-networking.md`)
- [x] .context.md for networking subsystem
- [x] Client-server architecture (NetworkServer authoritative broadcast, NetworkClient snapshot receiving)
- [x] ECS state replication (ReplicationRegistry, 32 component types, snapshot serialization, slerp interpolation)
- [x] Network interpolation (client-side entity interpolation via SnapshotBuffer)
- [x] Lua bindings for networking (30 bindings)
- [x] Client-side prediction and server reconciliation (ClientPrediction, 64-slot ring buffer, MoveFn, configurable threshold)
- [x] Server input processing (INPUT packet validation, per-connection queue, InputCallbackFn)
- [x] At least one networked demo game (Net Arena — 2D multiplayer arena)
- [x] Lobby/matchmaking API (LobbyServer/LobbyClient, create/join/leave/ready/game start, max player enforcement, duplicate rejection)
- [x] Lag compensation (LagCompensator, 64-frame history, ray-vs-sphere hit check, server-side rewind window)

NAT traversal deferred to backlog — relay server is infrastructure/ops, not engine library code.

## Phase 5 — Website and Learning Platform: COMPLETE

**Goal:** A documentation and training website that gets young people into game development and engineering.

### Delivered (Sessions 62-65)
- [x] Documentation site scaffolding (MkDocs + Material theme, 21 pages)
- [x] API reference pipeline (generate_api_docs.py extracts from 8 .context.md files)
- [x] Getting Started guide (install, build, first game in 15 minutes)
- [x] Tutorial series — 3 complete tutorials (first 2D game, first 3D game, multiplayer basics)
- [x] "How It Works" deep dives (ECS internals, renderer architecture, networking)
- [x] GitHub Pages deployment workflow (auto-deploy on push to main)
- [x] "Build Your Own Engine" learning track (first installment: Build an ECS from Scratch)
- [x] Community showcase (all 5 official demo games)

### Website Backlog (ongoing, not blocking)
- [ ] Video/interactive content — requires WASM tooling and video production infrastructure
- [ ] Asset library — requires curation infrastructure and hosting
- [ ] Forum or Discord integration — community operations, not engine development
- [ ] More "Build Your Own Engine" installments — ongoing content creation
- [ ] More "How It Works" installments — ongoing content creation

## All Phases Complete

All 5 phases of the FFE roadmap have been delivered:
1. **Phase 1 (2D Foundation)** — COMPLETE
2. **Phase 2 (3D Foundation)** — COMPLETE
3. **Phase 3 (Standalone Editor)** — MVP COMPLETE
4. **Phase 4 (Networking)** — COMPLETE
5. **Phase 5 (Website/Learning)** — COMPLETE

## Phase 6 — Showcase Game: "Echoes of the Ancients" (IN PROGRESS)

**Goal:** A multi-level 3D showcase game proving FFE can ship real, playable games. ADR: `docs/architecture/adr-phase6-showcase.md`

### Planned (8-10 sessions)
- [x] M1 (Session 67): Linear fog shader + project scaffold + player controller
- [x] M2 (Session 68): Level 1 — The Courtyard (outdoor, puzzles, guardians, artifacts)
- [ ] M3 (Sessions 70-71): Level 2 — The Temple (underground, dark lighting, boss)
- [ ] M4 (Sessions 72-73): Level 3 — The Summit (floating platforms, victory)
- [ ] M5 (Sessions 74-75): Polish, screenshots, gamepad pass, README update
- [ ] Optional M6: Stretch goals (skeletal anim, minimap, time-of-day)

## Build Commands

```bash
# Clang-18 (FAST — default)
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build && ctest --test-dir build --output-on-failure --parallel $(nproc)

# GCC-13 (FULL builds only)
cmake -B build-gcc -G Ninja -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build-gcc && ctest --test-dir build-gcc --output-on-failure --parallel $(nproc)

# Windows (MinGW cross-compile from Linux)
cmake -B build-mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build-mingw
```

## Key File Locations

| What | Where |
|------|-------|
| Constitution | `.claude/CLAUDE.md` |
| Roadmap (archival) | `docs/ROADMAP.md` (full phases — read only for phase transitions) |
| Devlog (recent) | `docs/devlog.md` (Sessions 35+) |
| Devlog (archive) | `docs/devlog-archive.md` (Sessions 1-34) |
| Project state | `docs/project-state.md` (this file) |
| Architecture map | `docs/architecture-map.md` (subsystems, files, Lua bindings, ECS components) |
| Environment log | `docs/environment.md` |
| Agent definitions | `.claude/agents/*.md` |
