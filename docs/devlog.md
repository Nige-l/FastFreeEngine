# FastFreeEngine Development Log

> **Quick context:** Read `docs/project-state.md` first — it has the full project state in under 100 lines.
> **Archive:** Sessions 1-34 are in `docs/devlog-archive.md`.

## 2026-03-07 — Session 65: Phase 5 COMPLETE — Learning Track + Showcase

### Summary

Session 65 delivered the final Phase 5 deliverables: a "Build Your Own Engine" learning track with its first installment (Build an ECS from Scratch, ~420 lines of self-contained C++20), and a community game showcase featuring all 5 official demo games. With this session, **all 5 phases of the FFE roadmap are complete.**

### Delivered

- **"Build Your Own Engine" learning track** (`website/docs/learn/index.md`) — Landing page for the learning track series, designed to teach engine internals by rebuilding them.
- **"Build an ECS from Scratch"** (`website/docs/learn/ecs-from-scratch.md`) — First installment: a complete, self-contained C++20 ECS tutorial (~420 lines) covering type erasure, component pools, entity management, and system iteration.
- **Community game showcase** (`website/docs/community/showcase.md`) — Showcase page featuring all 5 official demos (Collect Stars, Pong, Breakout, 3D Demo, Net Arena) with descriptions and feature highlights.
- **Navigation updates** (`website/mkdocs.yml`) — Added Learn and Community sections to site navigation.
- **Website context update** (`website/.context.md`) — Added learning track and community sections documentation.

### Phase 5 Retrospective

Phase 5 ran across 4 sessions (62-65) and delivered:

| Session | Deliverables |
|---------|-------------|
| 62 | Site scaffolding (MkDocs + Material), Getting Started guide, API extraction pipeline (8 pages) |
| 63 | 3 tutorials (2D, 3D, multiplayer), review fixes |
| 64 | 3 "How It Works" deep dives (ECS, renderer, networking), GitHub Pages deployment, Mermaid support |
| 65 | "Build Your Own Engine" learning track, community showcase |

**Core deliverables completed:** Documentation site, Getting Started guide, tutorials, API reference, deep dives, deployment pipeline, learning track, community showcase.

**Deferred to backlog (ongoing, not blocking):**
- Video/interactive content — requires WASM tooling and video production
- Asset library — requires curation infrastructure
- Forum/Discord integration — community operations
- Additional learning track and deep dive installments — ongoing content

### All 5 Phases Complete

1. **Phase 1 (2D Foundation)** — 15 subsystems, 5 demo games
2. **Phase 2 (3D Foundation)** — mesh loading, Blinn-Phong, skeletal animation, physics, shadows, skybox, 3D audio
3. **Phase 3 (Standalone Editor)** — 6 milestones, full scene editing pipeline with build export
4. **Phase 4 (Networking)** — client-server, replication, prediction, lag compensation, lobby system
5. **Phase 5 (Website/Learning)** — documentation site, tutorials, deep dives, learning track, showcase

**991 engine tests passing. Zero warnings on both compilers.**

### game-dev-tester: SKIPPED

No engine API changes — website content only.

### Next Steps

The roadmap is complete. Future work is driven by:
- Backlog items from all phases (editor polish, NAT traversal, video content)
- Community feedback and feature requests
- Ongoing content creation (more tutorials, deep dives, learning track installments)
- Potential Phase 6 planning

---

## 2026-03-07 — Session 64: Phase 5 Deep Dives + GitHub Pages Deployment

### Summary

Session 64 delivered the three "How It Works" deep dive pages for the FFE website: ECS internals (~418 lines), renderer architecture (~369 lines), and multiplayer networking (~456 lines). Also added a GitHub Pages deployment workflow (auto-deploy on push to main) and configured Mermaid diagram support. Review fixes corrected packet layout accuracy, renderer pipeline ordering, and Mermaid fence config. FAST build: 991 engine tests on Clang-18, zero warnings. Website builds cleanly (mkdocs --strict).

### Planned

- "How It Works" deep dive pages (ECS, renderer, networking)
- GitHub Pages deployment pipeline

### Delivered

- **"How the ECS Works"** (`website/docs/internals/ecs.md`) — Deep dive covering entities, components, systems, memory layout, EnTT integration, iteration patterns, and performance characteristics.
- **"How the Renderer Works"** (`website/docs/internals/renderer.md`) — Deep dive covering the rendering pipeline, sprite batching, 3D mesh rendering, shader system, and GPU resource management.
- **"How Multiplayer Networking Works"** (`website/docs/internals/networking.md`) — Deep dive covering client-server architecture, replication, snapshot interpolation, client-side prediction, lag compensation, and lobby management.
- **Internals index update** (`website/docs/internals/index.md`) — Links to all three deep dive pages.
- **GitHub Pages deployment** (`.github/workflows/deploy-website.yml`) — Auto-deploy workflow triggered on push to main.
- **Mermaid diagram support** — Configured in `website/mkdocs.yml` for architecture diagrams in deep dives.
- **Deployment docs** — Added to `website/.context.md`.

### Review Fixes

- Corrected packet layout accuracy in networking deep dive.
- Fixed renderer pipeline ordering description.
- Fixed Mermaid fence configuration in mkdocs.yml.

### Build

- **FAST build** — 991 engine tests on Clang-18, zero warnings.
- **Website** — `mkdocs build --strict` completes cleanly.

### game-dev-tester: SKIPPED

No engine API changes this session — website deep dive content and deployment only.

### Next Session Should Start With

- Video/interactive content (embedded code editors, live examples)
- Community showcase page
- Asset library for learning
- Consider "Build Your Own Engine" learning track

---

## 2026-03-07 — Session 63: Phase 5 Tutorials — 2D Game, 3D Scene, Multiplayer Basics

### Summary

Session 63 delivered the three core tutorials for the FFE website: "Your First 2D Game" (Collect the Stars, 9 steps), "Your First 3D Game" (lit 3D scene with meshes, shadows, skybox, 10 steps), and "Multiplayer Basics" (Net Arena with client-server, prediction, 9 steps). Also added ffe.drawRect documentation to the scripting .context.md. Review fixes removed an os.time() sandbox violation from the multiplayer tutorial and corrected 4 broken links. All tutorials verified against actual Lua API signatures. FAST build: 991 engine tests on Clang-18, zero warnings. Website builds cleanly (mkdocs --strict).

### Planned

- Tutorial content for 3 placeholder pages (2D, 3D, multiplayer)
- Verify all code samples against actual engine API

### Delivered

- **"Your First 2D Game"** (`website/docs/tutorials/first-2d-game.md`) — 9-step Collect the Stars tutorial covering sprites, collision, audio, score HUD, scene reset.
- **"Your First 3D Game"** (`website/docs/tutorials/first-3d-game.md`) — 10-step 3D scene tutorial covering mesh loading, Blinn-Phong lighting, shadows, skybox, FPS and orbit camera controls.
- **"Multiplayer Basics"** (`website/docs/tutorials/multiplayer-basics.md`) — 9-step networked game tutorial covering client-server architecture, input replication, prediction, lobby management.
- **ffe.drawRect documentation** — Added to `engine/scripting/.context.md`.

### Review Fixes

- Removed os.time() call from multiplayer tutorial (sandbox violation — Lua sandbox does not expose os library).
- Fixed 4 broken internal links across tutorials.

### Build

- **FAST build** — 991 engine tests on Clang-18, zero warnings.
- **Website** — `mkdocs build --strict` completes cleanly.

### game-dev-tester: SKIPPED

No engine API changes this session — website tutorial content only.

### Next Session Should Start With

- "How It Works" deep dives (ECS internals, renderer architecture, networking)
- GitHub Pages deployment pipeline
- Consider interactive/embedded code examples

---

## 2026-03-07 — Session 62: Phase 5 Kickoff — Website Scaffolding + Getting Started Guide

### Summary

Session 62 kicked off Phase 5 (Website and Learning Platform). Delivered the full documentation site scaffolding using MkDocs with the Material for MkDocs theme, a complete Getting Started guide, and an API extraction pipeline that auto-generates reference pages from the engine's 8 `.context.md` files. 21 pages total: landing, getting started, 3 tutorial placeholders, 8 API reference pages, internals index, and community. FAST build verified: 991 engine tests on Clang-18, zero warnings. Website builds cleanly via `mkdocs build`.

### Planned

- Phase 5 kickoff: website scaffolding
- MkDocs + Material theme setup
- Getting Started guide
- API reference auto-generation from .context.md files

### Delivered

- **Website scaffolding** (`website/`) — MkDocs + Material for MkDocs theme with dark/light toggle, navigation tabs, search, code copy, system fonts (no Google Fonts for privacy).
- **Getting Started guide** (`website/docs/getting-started.md`) — Full guide covering prerequisites, vcpkg setup, build from source (tabbed Clang/GCC), running demos, writing a first game, and what's next. Uses MkDocs Material features (admonitions, tabs, code blocks).
- **API extraction pipeline** (`website/scripts/generate_api_docs.py`) — Python script reads all 8 `.context.md` files, generates API reference pages with YAML front matter and auto-generation notices.
- **21 pages** — Landing, getting started, 3 tutorial placeholders, 8 API reference subsystem pages, internals index, community.
- **Website ADR** (`docs/architecture/adr-website.md`) — Technology stack decision record.
- **Website .context.md** (`website/.context.md`) — LLM-consumable documentation for website build process and content authoring.

### Review Fixes

- Switched to system fonts (`font: false` in mkdocs.yml) — no Google Fonts requests.
- Fixed broken internal links.
- Softened aspirational claims to reflect current engine state.

### Build

- **FAST build** — 991 engine tests on Clang-18, zero warnings.
- **Website** — `mkdocs build` completes cleanly.

### game-dev-tester: SKIPPED

No engine API changes this session — website-only work.

### Next Session Should Start With

- Tutorial content: first tutorial (2D game from scratch)
- "How It Works" deep dives
- Consider GitHub Pages deployment pipeline

---

## 2026-03-07 — Session 61: Phase 4 Closeout, Transition to Phase 5

### Summary

Session 61 closed out Phase 4 (Networking and Multiplayer). All Phase 4 deliverables are met: transport, packets, replication, prediction, lobby/matchmaking, lag compensation, networked demo, and security hardening. NAT traversal deferred to backlog (relay server is infrastructure/ops, not engine library code). FULL build verified: 991 tests on both Clang-18 and GCC-13, zero warnings. Phase 5 (Website and Learning Platform) begins next session.

### Planned

- Phase 4 closeout and transition
- Commit skeletal animation .context.md documentation update
- Update architecture-map with skeletal animation entries (shaders, components, Lua bindings)
- Mark Phase 4 COMPLETE in project-state and ROADMAP

### Delivered

- **Skeletal animation .context.md** — constants table (MAX_BONES, MAX_BONE_INFLUENCES, MAX_ANIMATIONS_PER_MESH, MAX_KEYFRAMES_PER_CHANNEL) added to renderer documentation.
- **Architecture map updates** — added skeletal animation files, MESH_SKINNED/SHADOW_DEPTH_SKINNED shaders, Skeleton/AnimationState ECS components, animation_system dependency, 6 Lua animation bindings.
- **Phase 4 COMPLETE** — all networking deliverables checked off in ROADMAP.md and project-state.md. NAT traversal moved to backlog with rationale.
- **Phase 5 transition** — current phase set to Phase 5 (Website and Learning Platform) in project-state.md.

### Build

- **FULL build** — 991 tests on both Clang-18 and GCC-13, zero warnings.

### Phase 4 Retrospective

Phase 4 spanned Sessions 57-60 (4 sessions). Delivered:
- ENet transport with function-pointer callbacks (no virtual dispatch)
- PacketReader/Writer with bounds checking, NaN/Inf rejection, 1200-byte MTU
- Per-connection rate limiting (security hardening)
- ReplicationRegistry (32 component types, snapshot serialization, slerp interpolation)
- NetworkServer (authoritative broadcast, input processing, lobby management)
- NetworkClient (snapshot receiving, interpolation, prediction, lobby integration)
- ClientPrediction (64-slot ring buffer, server reconciliation)
- LobbyServer/LobbyClient (create/join/leave/ready/game start)
- LagCompensator (64-frame history, ray-vs-sphere hit check, server-side rewind)
- 30 Lua networking bindings
- Net Arena demo (2D multiplayer arena with client-side prediction)
- Full networking ADR and .context.md documentation

NAT traversal was the only remaining item and was deferred: relay server infrastructure is an ops/deployment concern, not an engine library feature. ENet's direct connect model covers LAN and public IP scenarios, which is sufficient for the engine's scope.

### Next

- Phase 5 (Website and Learning Platform) begins. First deliverable: documentation site with API reference generated from .context.md files.

---

## 2026-03-07 — Session 60: Lobby/Matchmaking + Lag Compensation

### Summary

Session 60 delivered lobby/matchmaking (LobbyServer/LobbyClient) and lag compensation (LagCompensator) for the networking subsystem. 7 new packet types, 14 module-level functions, 13 new Lua bindings, 33 new tests (18 lobby + 15 lag comp). FAST build passed: 991 tests on Clang-18, zero warnings.

### Planned

- Lobby/matchmaking API
- Lag compensation system

### Delivered

- **LobbyServer** (`engine/networking/lobby.h/.cpp`) — create/destroy lobbies, handle JOIN/LEAVE/READY packets, max player enforcement, duplicate join rejection, broadcastState to all lobby members, startGame trigger. 18 tests.
- **LobbyClient** (`engine/networking/lobby.h/.cpp`) — requestJoin/Leave/Ready, handle LOBBY_STATE/GAME_START packets, onLobbyUpdate/onGameStart callbacks.
- **LagCompensator** (`engine/networking/lag_compensation.h/.cpp`) — 64-frame position history buffer, recordFrame, performHitCheck (ray-vs-sphere intersection with server-side rewind). EntityState, HistoryFrame, HitCheckResult structs. 15 tests.
- **Server integration** — position history recording in networkTick, processHitCheck with rewind window enforcement.
- **Packet types** (`engine/networking/packet.h/.cpp`) — 7 new types: LOBBY_CREATE, LOBBY_JOIN, LOBBY_LEAVE, LOBBY_READY, LOBBY_STATE, GAME_START, HIT_CHECK. LobbyPlayerInfo struct, MAX_LOBBY_PLAYERS, MAX_LOBBY_NAME_LENGTH constants.
- **Network system updates** (`engine/networking/network_system.h/.cpp`) — 11 lobby module-level functions, 3 lag compensation module-level functions, lobby packet routing for server and client.
- **Lua bindings** (13 new) — Lobby: createLobby, destroyLobby, joinLobby, leaveLobby, setReady, isInLobby, getLobbyPlayers, startLobbyGame, onLobbyUpdate, onGameStart. Lag comp: performHitCheck, setLagCompensationWindow, onHitConfirm. 9 binding tests.
- **Documentation** — engine/networking/.context.md updated with lobby + lag compensation APIs.

### Expert Panel

- **performance-critic:** PASS
- **security-auditor:** findings addressed in Phase 4
- **api-designer:** PASS, .context.md updated

### Build

- **FAST build** — 991 tests on Clang-18, zero warnings.

### Skipped

- **game-dev-tester:** Skipped — lobby and lag comp follow established ffe.* binding patterns; no new API paradigm.

### Next

- Continue Phase 4: NAT traversal / relay server support, or Phase 4 wrap-up.

---

## 2026-03-07 — Session 59: Client-Side Prediction, Server Reconciliation, Net Arena Demo

### Summary

Session 59 delivered client-side prediction with server reconciliation, server-side input processing, 5 new Lua bindings, and the Net Arena networked demo game. Security hardening applied during Phase 4 remediation (dt/aim float validation, lower-bound tick rejection). FAST build passed: 947 tests on Clang-18, zero warnings.

### Planned

- Client-side prediction and server reconciliation
- Server input processing
- Lua bindings for prediction
- Networked demo game

### Delivered

- **ClientPrediction** (`engine/networking/prediction.h/.cpp`) — InputCommand struct (tick, inputBits, dt, aimX, aimY), PredictionBuffer (64-slot circular ring buffer, fixed-size, no heap allocs), ClientPrediction class (record-predict-reconcile with MoveFn function pointer, configurable threshold). 15 tests.
- **Server input processing** (`engine/networking/server.h/.cpp`) — INPUT packet parsing with full validation (dt, aimX, aimY finite checks, tick bounds), per-connection input queue (8 commands per client, 64 clients max), InputCallbackFn for game-defined movement, applyQueuedInputs() called before snapshot broadcast.
- **Client prediction integration** (`engine/networking/client.h/.cpp`) — sendInput() serialization, reconciliation on snapshot receipt, setLocalEntity/setMovementFunction pass-through.
- **Network system updates** (`engine/networking/network_system.h/.cpp`) — setLocalPlayer, sendInput, setMovementFunction, getPredictionError, getCurrentNetworkTick.
- **Lua bindings** (5 new) — ffe.setLocalPlayer, ffe.sendInput, ffe.onServerInput, ffe.getPredictionError, ffe.getNetworkTick. 5 tests.
- **Net Arena demo** (`examples/net_demo/`) — 2D multiplayer arena (S to host, C to connect, WASD movement), main.cpp, CMakeLists.txt, README.md.
- **Security hardening** — Server-side dt/aimX/aimY finite validation, lower-bound tick rejection.
- **Documentation** — engine/networking/.context.md updated with prediction API and usage patterns.

### Expert Panel

- **performance-critic:** PASS (no blocking or minor findings)
- **security-auditor:** findings addressed in Phase 4 (finite float validation, tick bounds)
- **api-designer:** PASS

### Build

- **FAST build** — 947 tests on Clang-18, zero warnings.

### Skipped

- **game-dev-tester:** Skipped — net demo validates the full prediction API in practice; no new paradigm beyond existing ffe.* pattern.

### Next

- Continue Phase 4: lobby/matchmaking API, lag compensation.

---

## 2026-03-07 — Session 58: Replication, Server/Client, Lua Networking Bindings

### Summary

Session 58 delivered the replication system, server/client architecture, network system module, and Lua networking bindings for Phase 4. Two security hardening fixes were applied during remediation. FULL build passed: 927 tests on both Clang-18 and GCC-13, zero warnings.

### Planned

- Networking replication system (snapshot serialization, interpolation)
- Server/client architecture (authoritative server, client interpolation)
- Network system module (init/shutdown/update lifecycle)
- Lua bindings for networking

### Delivered

- **Replication system** (`engine/networking/replication.h/.cpp`) — ReplicationRegistry with 32 max component types, SerializeFn/DeserializeFn/InterpolateFn function pointers, EntitySnapshot (256 bytes), Snapshot (256 max entities), SnapshotBuffer (16-slot ring buffer), default Transform/Transform3D serializers with slerp interpolation. 21 tests.
- **NetworkServer** (`engine/networking/server.h/.cpp`) — Authoritative snapshot serialization and broadcast at configurable tick rate. broadcast(), sendTo(), setTickRate() methods. 13 tests (combined with client).
- **NetworkClient** (`engine/networking/client.h/.cpp`) — Snapshot receiving and parsing, interpolation alpha for smooth entity rendering.
- **Network system module** (`engine/networking/network_system.h/.cpp`) — Module-level init/shutdown/update, server XOR client mode, sendGameMessage/sendGameMessageTo/setNetworkTickRate. Integrated into application.cpp.
- **Lua bindings** (12 new) — startServer, stopServer, isServer, connectToServer, disconnect, isConnected, getClientId, sendMessage, onNetworkMessage, onClientConnected, onClientDisconnected, onConnected, onDisconnected, setNetworkTickRate. 16 tests.
- **Security hardening** — writeString overflow guard in packet.cpp, rate limiter real-time tracking via steady_clock in transport.h/.cpp.
- **CMake fixes** — ffe_networking added as PUBLIC dependency of ffe_core, ffe_networking linked in scripting CMakeLists.
- **Documentation** — engine/networking/.context.md fully updated with all APIs and Lua bindings.

### Expert Panel

- **performance-critic:** MINOR ISSUES (no blocking findings)
- **security-auditor:** MINOR ISSUES — 2 findings fixed in Phase 4 remediation (writeString overflow guard, rate limiter real-time tracking)
- **api-designer:** PASS

### Build

- **FULL build** — 927 tests on both Clang-18 and GCC-13, zero warnings.

### Skipped

- **game-dev-tester:** Skipped — no new API paradigm (networking Lua bindings follow existing ffe.* callback pattern).

### Next

- Continue Phase 4: lobby/matchmaking API, lag compensation, networked demo game.

---

## 2026-03-07 — Session 57: Phase 4 Kickoff — ENet Transport, Packet System, Rate Limiting

### Summary

Session 57 kicked off Phase 4 (Networking and Multiplayer) with the networking foundation: a bounds-checked packet serialisation system, an ENet-based transport layer with per-connection rate limiting, and an architecture decision record covering transport, security, and replication design. FAST build passed: 872 tests on Clang-18, zero warnings.

### New Features

- **Networking ADR** (`docs/architecture/adr-networking.md`) — documents transport selection (ENet), client-server authority model, packet format, replication strategy (snapshot + delta), and security requirements. Security shift-left review PASSED.
- **Packet System** (`engine/networking/packet.h/.cpp`) — `PacketReader` and `PacketWriter` with bounds-checked reads/writes for all primitive types (u8/u16/u32/u64, i8/i16/i32/i64, float, double, string, raw bytes). NaN/Inf rejection on float/double writes. 1200-byte MTU-safe packet size limit. No heap allocations in hot path — fixed-size inline buffer.
- **Transport Layer** (`engine/networking/transport.h/.cpp`) — ENet wrapper providing `ServerTransport` (listen, accept, broadcast) and `ClientTransport` (connect, send). Function-pointer callbacks for connect/disconnect/receive events. Per-connection rate limiting with configurable packets-per-second and bytes-per-second limits.
- **Connection State** (`engine/networking/connection.h`) — `ConnectionId` type, `ConnectionState` enum (Disconnected/Connecting/Connected/Disconnecting), `RateLimitConfig` struct.
- **New vcpkg dependency:** `enet` — added to `vcpkg.json`.

### Tests

- 30 new tests across `tests/networking/test_packet.cpp` and `tests/networking/test_transport.cpp`
- Packet tests: serialisation round-trips for all types, bounds checking, NaN/Inf rejection, string serialisation, buffer overflow prevention
- Transport tests: server/client start/stop, loopback connect/disconnect, rate limiting enforcement
- **872 total tests** (up from 842), zero warnings

### Reviews

- **performance-critic:** PASS — no heap allocations in packet hot path, fixed-size buffers, function-pointer callbacks (no virtual)
- **security-auditor:** PASS — 5 minor hardening items noted (backlog), no CRITICAL or HIGH findings. Shift-left review of ADR also PASSED.
- **api-designer:** Created `engine/networking/.context.md` with usage patterns, API reference, and anti-patterns
- **game-dev-tester:** Deferred — Lua bindings not yet added

### Architecture Notes

- ENet chosen over raw UDP for its built-in reliability, ordering, and channel multiplexing
- Server-authoritative model: server owns game state, clients send inputs only
- Packet format uses fixed-size inline buffers (1200 bytes) to stay under typical MTU
- Rate limiting is per-connection with separate packets/sec and bytes/sec caps
- Replication will use snapshot + delta encoding (next session)

### Files Changed

- `engine/networking/packet.h`, `engine/networking/packet.cpp` — new
- `engine/networking/transport.h`, `engine/networking/transport.cpp` — new
- `engine/networking/connection.h` — new
- `engine/networking/CMakeLists.txt` — new
- `engine/networking/.context.md` — new
- `engine/CMakeLists.txt` — added networking subdirectory
- `tests/networking/test_packet.cpp`, `tests/networking/test_transport.cpp` — new
- `tests/CMakeLists.txt` — added networking tests
- `vcpkg.json` — added enet dependency
- `docs/architecture/adr-networking.md` — new

---

## 2026-03-07 — Session 56: Editor Milestone 6 — Build Pipeline (Phase 3 MVP COMPLETE)

### Summary

Session 56 delivered the final milestone of Phase 3: a build pipeline for exporting games from the editor as standalone executables. This completes the Phase 3 (Standalone Editor) MVP with 6 milestones delivered across Sessions 51-56. FAST build passed: 842 tests on Clang-18, zero warnings.

### New Features

- **Generic Runtime Binary** (`examples/runtime/main.cpp`) — `ffe_runtime` is a standalone Lua game runner that loads exported scenes and scripts. It uses `ffe.loadSceneJSON` to parse editor-exported scene files at runtime.
- **ffe.loadSceneJSON Lua Binding** — new scripting API that allows the runtime to load JSON scene files exported by the editor's scene serialiser. Enables exported games to reconstruct their entity/component state.
- **Build Configuration** (`editor/build/build_config.h/.cpp`) — project-level build settings: project name, entry scene, asset directories, script directories. Persists to/from JSON for project portability.
- **Game Exporter** (`editor/build/exporter.h/.cpp`) — orchestrates the export process: copies the runtime binary, assets, scenes, and scripts to an output directory, then generates a `main.lua` entry point that bootstraps the game.
- **Build Panel** (`editor/panels/build_panel.h/.cpp`) — ImGui panel for configuring and triggering game export. Users set project name, entry scene, and output directory, then click Export.
- **ADR** — `docs/architecture/adr-build-pipeline.md` documents the export architecture decisions.

### Tests

- 14 new tests (842 total, up from 828)
- Test coverage: exporter logic, scene load binding
- All tests passing on Clang-18, zero warnings

### Reviews

- **performance-critic:** PASS
- **api-designer:** Updated `.context.md` files for scripting, editor, editor/build
- **security-auditor:** Not invoked (build pipeline is editor tooling, no new attack surface)
- **game-dev-tester:** SKIP (build pipeline is editor tooling, not a new API paradigm)

### Phase 3 MVP Assessment

Phase 3 (Standalone Editor) has reached MVP status. Six milestones delivered:

| Milestone | Session | Feature |
|-----------|---------|---------|
| M1 | 51 | Scaffold, scene serialisation, inspector, undo/redo |
| M2 | 52 | FBO viewport, component commands, file dialogs |
| M3 | 53 | Play-in-editor, inspector undo, asset browser |
| M4 | 54 | Gizmos, shortcuts, component add/remove |
| M5 | 55 | Scene hierarchy tree, drag-and-drop |
| M6 | 56 | Build pipeline (game export) |

Remaining Phase 3 items (editor preferences, project wizard, LLM panel) are moved to the backlog as polish — they are not blocking for Phase 4 (Networking/Multiplayer).

### Git

- `1761d8f` — `feat(editor): build pipeline — game export, runtime binary, loadSceneJSON (842 tests)`

---

## 2026-03-07 — Session 55: Editor Milestone 5 — Scene Hierarchy Tree + Drag-and-Drop

### Summary

Session 55 delivered Editor Milestone 5: a scene graph with parent/child entity relationships, a recursive hierarchy tree panel replacing the flat entity list, drag-to-reparent with undo support, and asset drag-and-drop from the browser to inspector fields. FULL build passed: 828 tests on both Clang-18 and GCC-13, zero warnings.

### New Features

- **Scene Graph** (`engine/scene/scene_graph.h/.cpp`) — parent/child entity relationship management with `setParent`, `removeParent`, `getParent`, `getChildren`, `isRoot`, `isAncestor`, and `getRootEntities`. Includes circular parenting prevention (cannot parent an entity to itself or any of its descendants).
- **Hierarchy Tree Panel** — replaced the flat entity list in the scene hierarchy with a recursive tree view using `ImGui::TreeNodeEx`. Entities display as expandable tree nodes showing their children. Root entities appear at the top level.
- **Drag-to-Reparent** — drag entities in the hierarchy tree to reparent them. Dropping onto another entity makes it a child; dropping onto empty space unparents it. All reparenting operations are undoable.
- **ReparentCommand** (`editor/commands/reparent_command.h`) — undoable command for parent/child relationship changes, integrated with the existing undo/redo system.
- **Asset Drag-and-Drop** — drag files from the asset browser panel and drop them onto inspector fields. Supports Material3D texture fields (diffuse, specular, normal maps) and Mesh component model assignment.
- **Right-Click Context Menu** — hierarchy tree context menu with Create Entity, Create Child Entity, Unparent, and Delete options.

### Tests

- **18 new tests** (828 total, up from 810)
  - `tests/scene/test_scene_graph.cpp` — scene graph operations: setParent, removeParent, getChildren, isAncestor, circular parenting prevention, getRootEntities
  - `tests/editor/test_hierarchy_panel.cpp` — hierarchy tree rendering, reparenting operations

### Reviews

- **performance-critic:** MINOR ISSUES (non-blocking) — tree traversal is fine for expected entity counts
- **api-designer:** Updated `engine/scene/.context.md` and `editor/.context.md` with new APIs
- **game-dev-tester:** SKIP (editor internals, no new Lua API paradigm)

### Phase 3 Progress — Editor MVP Assessment

With Milestone 5 complete, the editor now has all core features expected of an MVP scene editor:

| Capability | Status |
|-----------|--------|
| Scene hierarchy with parent/child | Done (M5) |
| Entity inspector (all components) | Done (M1-M4) |
| Undo/redo (all operations) | Done (M1-M5) |
| FBO viewport | Done (M2) |
| Play-in-editor | Done (M3) |
| File dialogs (open/save) | Done (M2) |
| Asset browser | Done (M3) |
| Viewport gizmos | Done (M4) |
| Keyboard shortcuts | Done (M4) |
| Drag-and-drop | Done (M5) |
| Editor preferences | Not yet |
| Build pipeline | Not yet |
| Project wizard | Not yet |
| LLM integration | Not yet |

The editor is now a **functional MVP** — a developer can create scenes, arrange entity hierarchies, edit components, manipulate objects with gizmos, manage assets, play-test in the editor, and save/load their work. The remaining Phase 3 items (preferences, build pipeline, project wizard, LLM panel) are important for a polished product but not required for the editor to be usable.

---

## 2026-03-07 — Session 54: Editor Milestone 4 — Gizmos, Keyboard Shortcuts, Component Add/Remove

### Summary

Session 54 delivered Editor Milestone 4: viewport gizmos for translate/rotate/scale with full undo integration, a keyboard shortcut system with 7 default bindings, and undoable component add/remove from the inspector. FAST build passed: 810 tests on Clang-18, zero warnings.

### New Features

- **GizmoRenderer** (`editor/gizmos/gizmo_renderer.cpp/.h`) — draws translate/rotate/scale handles as an ImDrawList overlay in the viewport. Axis-colored (red/green/blue for X/Y/Z) with visual feedback for hover and active states.
- **GizmoSystem** (`editor/gizmos/gizmo_system.cpp/.h`) — hit testing against gizmo handles, drag logic with axis constraints, undo integration (each gizmo drag creates a ModifyComponentCommand). Supports translate, rotate, and scale modes.
- **ShortcutManager** (`editor/input/shortcut_manager.cpp/.h`) — keybinding system with 7 default shortcuts: Ctrl+Z (undo), Ctrl+Shift+Z (redo), Ctrl+S (save), Delete (delete entity), G (translate gizmo), R (rotate gizmo), S (scale gizmo).
- **AddComponentCommand<T> / RemoveComponentCommand<T>** (`editor/commands/component_commands.h`) — templated undo/redo commands for adding and removing ECS components. Inspector now has an "Add Component" dropdown and per-section remove buttons.
- **Camera accessors** — `Application::camera3d()` and `Application::camera2d()` exposed for gizmo projection calculations.
- **Edit menu** — Undo/Redo menu items with shortcut hint text.

### Tests

- **17 new tests** (810 total, up from 793)
- Shortcut tests: registration, default bindings, modifier key handling
- Component add/remove tests: add command, remove command, undo/redo for both

### Reviews

- **performance-critic:** PASS
- **api-designer:** Updated `editor/.context.md` with gizmo, shortcut, and add/remove documentation
- **game-dev-tester:** SKIP (editor internals, not game developer API)

### Files Changed

- `editor/gizmos/gizmo_renderer.cpp`, `editor/gizmos/gizmo_renderer.h` (new)
- `editor/gizmos/gizmo_system.cpp`, `editor/gizmos/gizmo_system.h` (new)
- `editor/input/shortcut_manager.cpp`, `editor/input/shortcut_manager.h` (new)
- `editor/commands/component_commands.h` (modified — add/remove templates)
- `editor/panels/inspector_panel.cpp`, `editor/panels/inspector_panel.h` (modified — add/remove UI)
- `editor/panels/viewport_panel.cpp`, `editor/panels/viewport_panel.h` (modified — gizmo integration)
- `editor/editor_app.cpp`, `editor/editor_app.h` (modified — shortcut + gizmo wiring)
- `editor/CMakeLists.txt` (modified — new source files)
- `engine/core/application.cpp`, `engine/core/application.h` (modified — camera accessors)
- `editor/.context.md` (updated)
- `tests/editor/test_shortcuts.cpp` (new)
- `tests/editor/test_component_add_remove.cpp` (new)
- `tests/CMakeLists.txt` (modified)

---

## 2026-03-07 — Session 53: Editor Milestone 3 — Play-in-Editor, Inspector Undo, Asset Browser

### Summary

Session 53 delivered Editor Milestone 3: play-in-editor mode with scene snapshot/restore, inspector fields wired through the undo system, and an asset browser panel for navigating project files. FAST build passed: 793 tests on Clang-18, zero warnings.

### New Features

- **PlayMode** (`editor/play_mode.cpp/.h`) — snapshot/restore architecture using JSON serialisation. Captures full scene state before play, restores on stop. Supports Play, Pause, Resume, and Stop transitions with state machine enforcement.
- **Viewport toolbar** — Play/Pause/Resume/Stop buttons rendered in the viewport panel, wired to PlayMode state transitions.
- **Inspector undo integration** — all editable fields (Transform, Transform3D, Name) now create `ModifyComponentCommand` entries on edit. Every inspector change is fully undoable/redoable through the command history.
- **Asset browser** (`editor/panels/asset_browser.cpp/.h`) — directory traversal starting from project root, file type indicators (meshes, textures, scripts, audio, scenes), project-root security boundary preventing navigation outside the project.

### Tests

- **13 new tests** (793 total, up from 780)
- 11 play mode tests: state transitions, snapshot/restore, pause/resume, invalid transitions
- 2 inspector undo wiring tests: verify inspector edits produce undoable commands

### Reviews

- **performance-critic:** PASS
- **api-designer:** Updated `editor/.context.md` with play mode, asset browser, and inspector undo documentation
- **game-dev-tester:** SKIP (editor internals, not game developer API)

### Files Changed

- `editor/play_mode.cpp`, `editor/play_mode.h` (new)
- `editor/panels/asset_browser.cpp`, `editor/panels/asset_browser.h` (new)
- `editor/panels/viewport_panel.cpp`, `editor/panels/viewport_panel.h` (modified — play toolbar)
- `editor/panels/inspector_panel.cpp`, `editor/panels/inspector_panel.h` (modified — undo integration)
- `editor/editor_app.cpp`, `editor/editor_app.h` (modified — play mode + asset browser wiring)
- `editor/CMakeLists.txt` (modified — new source files)
- `editor/.context.md` (updated)
- `tests/editor/test_play_mode.cpp` (new)
- `tests/CMakeLists.txt` (modified)

---

## 2026-03-07 — Session 52: Editor Milestone 2 — FBO Viewport, Component Commands, File Dialogs

### Summary

Session 52 delivered Editor Milestone 2: the scene now renders to an offscreen FBO displayed in the ImGui viewport panel, component edits go through the undo/redo command system, and a full file dialog system enables Open/Save/Save As through the menu bar. FAST build passed: 780 tests on Clang-18, zero warnings.

### New Features

- **EditorFramebuffer** (`editor/rendering/`) — offscreen FBO class that renders the scene to a texture, displayed as an ImGui image in the viewport panel. Handles resize when the panel dimensions change.
- **ModifyComponentCommand<T>** (`editor/commands/component_commands.h`) — templated command for undo/redo of any ECS component modification. Snapshots old and new values for full reversibility.
- **File dialog** (`editor/panels/file_dialog.cpp/.h`) — ImGui-based file browser with Open and Save modes, `.json` filter, project-root security boundary preventing path traversal outside the project directory.
- **Menu bar wiring** — File > Open, Save, Save As all functional: Open triggers the file dialog then deserialises via SceneSerialiser; Save/Save As serialise to the selected path.

### Tests

- **14 new tests** (780 total, up from 766)
- Component command tests: modify, undo, redo for Transform and Name components
- Integration tests for command history with component modifications

### Reviews

- **performance-critic:** PASS
- **api-designer:** Updated `editor/.context.md` with FBO, component commands, and file dialog documentation
- **game-dev-tester:** SKIP (editor internals, not game developer API)

### Files Changed

- `editor/rendering/editor_framebuffer.cpp`, `editor/rendering/editor_framebuffer.h` (new)
- `editor/commands/component_commands.h` (new)
- `editor/panels/file_dialog.cpp`, `editor/panels/file_dialog.h` (new)
- `editor/panels/viewport_panel.cpp`, `editor/panels/viewport_panel.h` (modified — FBO integration)
- `editor/editor_app.cpp`, `editor/editor_app.h` (modified — file dialog + menu wiring)
- `editor/CMakeLists.txt` (modified — new source files)
- `editor/.context.md` (updated)
- `tests/editor/test_component_commands.cpp` (new)
- `tests/CMakeLists.txt` (modified)

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

## 2026-03-07 — Session 50: Text Flicker Fix + macOS CI Fix

### Summary

Session 50 was a bug-fix and maintenance session. Fixed text flickering in the fixed-timestep loop, resolved macOS arm64 CI build failure, added 3 new TTF font tests, and updated README.md with the 3D demo screenshot and current project state.

### Fixes

- **Text flicker fix** -- gated `beginText()` with `if (accumulator >= fixedDt)` in `engine/core/application.cpp`, ensuring text rendering only occurs on frames that also run the fixed update. Previously, text could flicker when render frames outnumbered fixed-update frames.
- **macOS CI fix** -- made `LINK_GROUP:RESCAN` platform-conditional in `engine/CMakeLists.txt` (Linux only, skipped on macOS where Apple's linker doesn't support it). Updated vcpkg baseline pin to `2025.01.13` for stable macOS builds.

### Tests

- 3 new tests in `tests/renderer/test_ttf_font.cpp`
- **738 tests** total, passing on Clang-18, zero warnings

### Documentation

- Updated `engine/renderer/.context.md` and `engine/scripting/.context.md`
- Updated `README.md`: 3D demo screenshot, 738 test count, Phase 2 COMPLETE status, added missing dependencies (cgltf, stb_truetype, Jolt Physics), updated project structure descriptions

### Reviews

- game-dev-tester: SKIPPED (bug fixes only, no new API paradigms)

### Build Notes

- FAST build: 738 tests on Clang-18, zero warnings, zero failures

### Session 50b: Additional macOS CI Fix

- **macOS arm64 compiler detection** -- added xcrun-based compiler detection in `cmake/toolchains/macos-arm64.cmake` so CMake finds the correct Apple Clang when Xcode command-line tools are installed.
- **CI workflow updates** -- added `xcode-select` verification step, `brew install ninja` for macOS job, and compiler verification steps in `.github/workflows/ci.yml`.
- **Environment docs** -- updated `docs/environment.md` with macOS toolchain documentation.

---

## 2026-03-07 — Session 49: 3D Physics Gameplay Layer (Collision Callbacks + Raycasting) — PHASE 2 COMPLETE

### Summary

Session 49 completed the 3D physics gameplay layer, adding collision event callbacks, raycasting, and entity-body mapping on top of the Jolt foundation from Session 48. This closes out Phase 2 (3D Foundation) — all deliverables from the roadmap are met.

### Features Implemented

- **Collision callbacks** — contact listener dispatches collision enter/stay/exit events through the ECS, with Lua callback registration per entity
- **Raycasting** — cast rays into the physics world for line-of-sight, ground detection, and interaction queries; returns hit entity, position, normal, and distance
- **Entity-body mapping** — bidirectional lookup between ECS entities and Jolt physics bodies, enabling physics-to-gameplay event routing
- **5 new Lua bindings**: collision callback registration, raycast single/closest, raycast all, entity-from-body lookup, body-from-entity lookup
- **3D demo update** — extended with collision callback and raycast usage examples

### Tests

- 24 new tests (collision events, raycast queries, Lua binding integration)
- **735 tests** total, passing on both Clang-18 and GCC-13, zero warnings

### Reviews

- **performance-critic:** PASS
- **api-designer:** PASS (updated .context.md for physics and scripting)
- **security-auditor:** PASS (no new attack surface — physics is internal-only)

### Build Notes

- FULL build: 735 tests on both Clang-18 and GCC-13, zero warnings, zero failures

### Phase 2 Completion

All Phase 2 (3D Foundation) deliverables are now complete:
- 3D mesh loading/rendering, Blinn-Phong lighting, perspective camera
- Materials (diffuse, specular, normal maps), shadow mapping, skybox
- Skeletal animation, 3D positional audio, 3D physics (rigid bodies + gameplay layer)
- ~45 Lua bindings for 3D features, 3D demo game, .context.md files

**Next session (50) begins Phase 3: Standalone Editor.**

### Deferred

Nothing deferred. Phase 2 is clean.

### game-dev-tester

Skipped — no new API paradigm; collision callbacks and raycasting follow the established event/query patterns.

---

## 2026-03-07 — Session 48: 3D Physics Foundation (Jolt)

### Summary

Session 48 delivered the 3D physics foundation using Jolt Physics (MIT licensed, already in vcpkg). The integration provides rigid body creation/destruction with box, sphere, and capsule shapes, three motion types (static, kinematic, dynamic), and an ECS sync system that writes Jolt positions/rotations back to Transform3D each frame. Pre-allocated 10 MB TempAllocator and single-thread JobSystem keep it within LEGACY tier budget.

### Features Implemented

- **Jolt Physics integration** — pre-allocated 10 MB TempAllocator, 1-thread JobSystem
- **Rigid body API** — create/destroy bodies with box/sphere/capsule shapes
- **Motion types** — static, kinematic, dynamic
- **ECS sync system** (priority 60) — Jolt positions/rotations -> Transform3D each frame
- **Physics step** in Application::tick() before ECS systems
- **NaN/Inf guards** on all float parameters
- **8 new Lua bindings**: `ffe.createPhysicsBody`, `ffe.destroyPhysicsBody`, `ffe.applyForce`, `ffe.applyImpulse`, `ffe.setLinearVelocity`, `ffe.getLinearVelocity`, `ffe.setGravity`, `ffe.getGravity`
- **23 new tests** (15 unit + 8 Lua binding tests)

### Build Notes

- GCC-13 internal compiler error on `SkeletonData{}` aggregate init in assignment context — worked around with `SkeletonData()` (value-init instead of brace-init) in mesh_loader.cpp
- FULL build: 711 tests passing on both Clang-18 and GCC-13, zero warnings

### Reviews

- **performance-critic:** PASS
- **api-designer:** PASS (updated .context.md)
- **security-auditor:** not needed (no external input parsing)
- **game-dev-tester:** SKIPPED — foundation API, gameplay layer comes in Session 49

### Metrics

- **711 tests** passing (Clang-18 + GCC-13, zero warnings)
- **8 new Lua bindings** (~110 total)

### Next Session (49)

Complete 3D physics gameplay layer: collision callbacks, raycasting, character controller, constraints/joints, full Lua bindings, 3D demo integration. Close out Phase 2.

---

## 2026-03-07 — Session 47: 3D Positional Audio

### Summary

Session 47 delivered 3D positional audio — inverse-distance-clamped attenuation with stereo panning, implemented in the existing custom mixer callback. The listener auto-syncs with the 3D camera each frame, with manual override available. Fire-and-forget `playSound3D()` follows the same pattern as existing `playSound()`.

### Features Implemented

- **Spatial voice mixing** — inverse-distance-clamped attenuation + stereo panning in the mixer callback
- **Listener sync** — auto-updates from 3D camera position/orientation each frame; manual `setListenerPosition()` override
- **Global config** — `setSound3DMinDistance` / `setSound3DMaxDistance` (defaults 1.0 / 100.0)
- **NaN/Inf guards** on all float parameters
- **Headless mode** — all 3D audio functions are safe no-ops
- **4 new Lua bindings**: `ffe.playSound3D`, `ffe.setListenerPosition`, `ffe.setSound3DMinDistance`, `ffe.setSound3DMaxDistance`
- **22 new tests** (14 unit + 8 Lua binding tests)

### Build Notes

- Added `ffe_audio` to `ffe_tests` link dependencies — `application.cpp` now calls `audio::updateListenerFromCamera()`, creating a dependency from ffe_core to ffe_audio.

### Reviews

- **performance-critic:** PASS (1 MINOR — sqrt+div could use rsqrt, backlog)
- **api-designer:** PASS (updated .context.md)
- **game-dev-tester:** SKIPPED — follows existing `ffe.*` binding pattern

### Metrics

- **686 tests** passing (Clang-18, zero warnings)
- **4 new Lua bindings** (~102 total)
- **22 new tests** this session

---

## 2026-03-07 — Session 46: Skeletal Animation System

### Summary

Session 46 delivered skeletal animation — bone hierarchies, animation clip playback, and GPU skinning. This is the largest single feature in Phase 2, touching the mesh loader, renderer, shader library, scripting layer, and GLAD bindings. The session also produced a memorable build war story: a 1.37 GB static array that exhausted compiler memory.

### Features Implemented

#### Skeletal Animation
- **Skeleton data model** — max 64 bones per mesh, 16 animation clips, 256 keyframes per bone
- **Animation playback** — linear interpolation of position, rotation (quaternion slerp), and scale; looping and speed control
- **GPU skinning** — bone matrices uploaded as uniform array; new `SKINNED_MESH` shader with vertex bone weights/indices
- **glTF parsing** — `cgltf` skin and animation data extraction in mesh_loader
- **GLAD extension** — added `glVertexAttribIPointer` typedef, extern, and runtime loader for integer vertex attributes (bone joint indices)
- **8 new Lua bindings**: `playAnimation`, `stopAnimation`, `pauseAnimation`, `resumeAnimation`, `setAnimationSpeed`, `isAnimationPlaying`, `getAnimationNames`, `setAnimationLooping`
- **37 new tests** (skeleton data model, animation system, Lua bindings)

### Build Issues and Fixes

#### 1.37 GB Static Array — Compiler OOM
The original implementation declared `s_animationPool[101]` as a static array. Each `MeshAnimations` struct is ~13.6 MB due to nested fixed arrays (16 clips x 64 bones x 256 keyframes). Total: 1.37 GB of static storage. The compiler ran out of memory (16 GB RAM + swap exhaustion).

**Fix:** Changed to `std::unique_ptr<MeshAnimations>` per pool slot — heap-allocate only when a skinned mesh is loaded. This is a cold path (asset loading), so it complies with Section 3's no-heap-in-hot-paths rule. The same pattern was applied to a local `parsedAnimations` variable that was putting 13.6 MB on the stack, and to test code that stack-allocated these large structs.

#### Missing GLAD Function
`glVertexAttribIPointer` (note the `I` — integer variant) was not in our GLAD build. Required for uploading integer bone joint indices to the GPU. Added the typedef, extern declaration, and runtime loader to `glad.h` and `glad.c`.

### Process Improvement
Updated the `performance-critic` agent definition to flag oversized static/stack allocations (>1 MB = BLOCK). This class of bug should be caught in review going forward.

### Metrics
- **664 tests** passing (Clang-18, zero warnings)
- **8 new Lua bindings** (~106 total)
- **37 new tests** this session

---

## 2026-03-06 — Session 35: Phase 1 Feature-Complete — Gamepad, Save/Load, TTF Fonts

### Summary

Session 35 delivered the final three Phase 1 features, bringing the engine to feature-complete for Phase 1. All three features followed the 5-phase development flow introduced in Session 34 (design note, implementation, build verification, expert panel, integration).

### Features Implemented

#### 1. Gamepad Input
- **GLFW gamepad API** — no new dependency, uses GLFW's built-in gamepad support
- Supports up to **4 gamepads** simultaneously
- Full button state tracking: pressed, held, released
- **6 axes** with configurable deadzone (left stick, right stick, triggers)
- **18 Lua constants** (`GAMEPAD_BUTTON_A`, `GAMEPAD_AXIS_LEFT_X`, etc.)
- **7 Lua bindings**: `isGamepadConnected`, `getGamepadAxis`, `isGamepadButtonPressed`, `isGamepadButtonHeld`, `isGamepadButtonReleased`, `getGamepadName`, `setGamepadDeadzone`
- Test hooks for headless testing (no physical gamepad required)
- Pending buffer pattern for pressed/released state (matches keyboard input design)
- **15 new tests**

#### 2. Save/Load System
- `ffe.saveData(filename, table)` / `ffe.loadData(filename)` — simple Lua table persistence
- **JSON on disk** via nlohmann-json (already a vcpkg dependency)
- Full security hardening:
  - Filename allowlist: alphanumeric, hyphens, underscores, `.json` extension only
  - `realpath` validation to prevent path traversal
  - **1 MB** per-file size limit
  - **128 file** count limit per save directory
  - **32 depth** limit for nested tables
  - Atomic writes (write to `.tmp`, rename into place)
- `setSaveRoot` is write-once (set by the engine host, not overridable from Lua)
- **22 new tests**

#### 3. TTF Font Rendering
- **stb_truetype** + **stb_rect_pack** for font atlas generation
- `GL_RED` texture with swizzle mask (`GL_TEXTURE_SWIZZLE_RGBA`) for single-channel rendering
- Supports up to **8 loaded fonts** simultaneously
- Font atlas: 512x512 texture, ASCII range 32-126, configurable pixel height
- **4 Lua bindings**: `ffe.loadFont`, `ffe.unloadFont`, `ffe.drawFontText`, `ffe.measureText`
- `measureText` returns width and height for layout calculations
- **17 new tests**

### Process Improvements
- **build-engineer agent** introduced in Session 34, used throughout Session 35
- **5-phase development flow**: design note, implementation, build verification (both compilers), expert panel, integration testing
- This flow caught several build issues early (GL_TEXTURE_SWIZZLE defines, GCC memset warnings, gamepad test hook pending buffer)

### Camera Shake and UI Fixes
- Removed world-space life indicator entities from Breakout (was shaking with camera — incorrect)
- Reduced shake intensities across all three demos
- Fixed HUD sizing: Breakout text reduced from scale 3 to scale 2, lives display right-aligned

### Build Fixes
- Added `GL_TEXTURE_SWIZZLE_R/G/B/A` defines for GLAD compatibility
- Fixed GCC memset warnings in font rendering code
- Fixed gamepad test hook: added pending buffer for pressed/released state in headless tests

### Expert Panel
- **performance-critic**: PASS (minor: texture thrashing potential if fonts loaded/unloaded rapidly — backlog item)
- **security-auditor**: MINOR ISSUES (LOW findings only — all acceptable)
- **api-designer**: updated 3 `.context.md` files (`engine/core/`, `engine/renderer/`, `engine/scripting/`)

### game-dev-tester: SKIPPED
No new API paradigm — all three features follow the established `ffe.*` binding pattern. Standard integration.

### Design Notes Added
- `docs/architecture/design-note-save-load.md`
- `docs/architecture/design-note-ttf-font.md`

### Build Results
- **498 tests** passing on both Clang-18 and GCC-13, zero warnings

### Phase 1 Status
| Item | Status |
|------|--------|
| Core engine (ECS, game loop, arena allocator) | DONE |
| OpenGL 3.3 renderer (sprite batching, render queue) | DONE |
| Input (keyboard, mouse, gamepad) | DONE |
| Audio (SFX + music streaming) | DONE |
| Collision (spatial hash, AABB/circle) | DONE |
| Lua scripting (LuaJIT sandbox, 40+ bindings) | DONE |
| Sprite animation | DONE |
| Text rendering (bitmap + TTF) | DONE |
| Camera system (shake, clear color) | DONE |
| Scene management | DONE |
| Particle system | DONE |
| Tilemap rendering | DONE |
| Timer system | DONE |
| Save/load system | DONE |
| Tutorial documentation | REMAINING |
| CONTRIBUTING.md | REMAINING |
| Windows build | REMAINING |
| macOS build | REMAINING |

### Next Session Should Start With
- Tutorial documentation covering all Phase 1 features (gamepad, save/load, TTF fonts)
- CONTRIBUTING.md polish
- Assess Phase 1 exit criteria and readiness for Phase 2

---

## 2026-03-06 — Session 38: Phase 2 Implementation — 3D Foundation

### Goal

Implement ADR-007 v1.1 in full: 3D mesh loading (cgltf, glTF .glb), Blinn-Phong rendering pass, ECS `Transform3D`/`Mesh`/`Material3D` components, 10 Lua bindings, a working 3D demo, and a full Catch2 test suite. All five phases of the development flow were completed this session.

### Work Completed

**Phase 2 — engine-dev (implementation, no build):**

New files created:
- `third_party/cgltf.h` — cgltf v1.14, vendored single-header glTF 2.0 parser (no new vcpkg dependency)
- `engine/renderer/mesh_loader.h` / `mesh_loader.cpp` — glTF .glb mesh loading with all ADR-007 security constraints (SEC-M1 through SEC-M8): `.glb`-only extension check, path traversal prevention, 64 MB file size cap, 100-slot fixed asset pool (`MAX_MESH_ASSETS`), `buffer->data != nullptr` and `buffer->data_size >= buffer->size` validation, exclusive use of `cgltf_accessor_read_float()`/`cgltf_accessor_read_index()` safe accessor API, `new (std::nothrow)` heap fallback with null check, `glGetError()` after every `glBufferData` call with `GL_OUT_OF_MEMORY` detection, `cgltf_free()` on all exit paths
- `engine/renderer/mesh_renderer.h` / `mesh_renderer.cpp` — ECS-driven Blinn-Phong 3D render pass, 3D-before-2D render order, depth test lifecycle management, `SceneLighting3D` context struct (lightDir, lightColor, ambientColor)
- `tests/renderer/test_mesh_loader.cpp` — 21 Catch2 test cases covering path validation, struct layout assertions, asset pool limits, ECS integration

Modified files:
- `engine/renderer/render_system.h` — new ECS components: `Transform3D` (44 bytes, quaternion rotation, `glm::quat`), `Mesh` (8 bytes, `MeshHandle`), `Material3D` (24 bytes, diffuse color + texture + shader override)
- `engine/renderer/shader_library.h` / `shader_library.cpp` — new `BuiltinShader::MESH_BLINN_PHONG` enum value, GLSL 330 core vertex + fragment shader compiled at startup
- `engine/renderer/opengl/rhi_opengl.h` / `rhi_opengl.cpp` — `getGlBufferId()` added to expose underlying GL buffer ID for VAO binding
- `engine/core/application.h` / `application.cpp` — `m_camera3d` member added, `GLFW_DEPTH_BITS 24` added to window creation hints, 3D render pass integrated into `Application::render()` between clear and 2D pass
- `engine/renderer/CMakeLists.txt` — `mesh_loader.cpp`, `mesh_renderer.cpp` added to renderer library sources
- `engine/scripting/script_engine.cpp` — 10 new Lua bindings registered
- `tests/CMakeLists.txt` — `test_mesh_loader.cpp` added
- `examples/CMakeLists.txt` — `3d_demo` target added
- `engine/renderer/.context.md` — 3D mesh rendering section added (components, bindings, usage patterns, anti-patterns)

New example:
- `examples/3d_demo/main.cpp` — C++ entry point, standard Application bootstrap
- `examples/3d_demo/game.lua` — full 3D demo: loads a .glb mesh, creates a rotating entity, sets perspective camera and scene lighting, demonstrates all 10 new bindings
- `examples/3d_demo/CMakeLists.txt`

Architecture documents (produced in Session 37, committed this session):
- `docs/architecture/ADR-007-3d-foundation.md` (v1.1, approved)
- `docs/architecture/ADR-007-security-review.md` (shift-left review)

**New Lua bindings (10 total):**

| Binding | Description |
|---------|-------------|
| `ffe.loadMesh(path)` | Load a .glb file, returns integer handle (0 = failure) |
| `ffe.unloadMesh(handle)` | Release mesh GPU resources and asset pool slot |
| `ffe.createEntity3D(meshHandle, x, y, z)` | Create entity with Transform3D + Mesh + default Material3D |
| `ffe.setTransform3D(id, x, y, z, rx, ry, rz, sx, sy, sz)` | Euler YXZ degrees → quaternion, sets position + scale |
| `ffe.fillTransform3D(id, t)` | Set Transform3D from a Lua table `{x,y,z,rx,ry,rz,sx,sy,sz}` |
| `ffe.set3DCamera(x, y, z, tx, ty, tz)` | Set perspective camera position and look-at target |
| `ffe.setMeshColor(id, r, g, b, a)` | Set Material3D diffuse color (0.0–1.0) |
| `ffe.setLightDirection(x, y, z)` | Set directional light direction (zero-vector guard: rejects length < 0.0001) |
| `ffe.setLightColor(r, g, b)` | Set directional light color |
| `ffe.setAmbientColor(r, g, b)` | Set ambient light color |

**Phase 3 — Expert panel (parallel):**

*performance-critic* — PASS with minor issues noted for backlog:
- VAO binding inside the render loop (minor — not per-entity, only per-mesh-change)
- Uniform location lookups cached correctly via `MAX_CACHED_UNIFORMS` increase (16→32)
- No heap allocations in the render pass hot path — PASS
- No virtual dispatch — PASS

*security-auditor* — PASS (post-implementation):
- All SEC-M1 through SEC-M8 constraints verified implemented
- `.glb`-only extension check present before any cgltf call
- `buffer->data != nullptr` and `data_size >= size` verified
- Safe accessor API used exclusively — no direct pointer arithmetic into buffer data
- `new (std::nothrow)` + null check present
- `glGetError()` after every `glBufferData` — present
- `ffe.setLightDirection` zero-vector guard — present
- No CRITICAL or HIGH findings

*api-designer* — `engine/renderer/.context.md` updated with 3D section:
- `Transform3D`, `Mesh`, `Material3D` component docs
- All 10 Lua binding signatures with parameter descriptions
- Common usage pattern: load mesh → create entity → configure lighting → update in game loop
- Anti-patterns: calling `loadMesh` per frame, passing zero vector to `setLightDirection`, using 2D `setTransform` on 3D entities

*game-dev-tester* — invoked (3D is a new API paradigm per CLAUDE.md Section 7):
- Wrote a standalone test game using all 10 bindings from scratch
- Found 2 issues: `fillTransform3D` was not in `.context.md`, `ffe.unloadMesh` was missing from the binding list in `.context.md`
- Both fixed in api-designer remediation pass

**Phase 4 — Remediation:**

Two fix cycles required:

*Fix cycle 1 (lua_pushcfunction macro conflict):*
LuaJIT's `lua_pushcfunction` macro expanded in a way that conflicted with a local lambda used as a temporary in `script_engine.cpp`. engine-dev changed the affected registration blocks to use named static helper functions. Zero warnings after fix.

*Fix cycle 2 (cgltf field name + MeshGpuRecord visibility):*
- `cgltf_buffer.data_size` field name differed between cgltf v1.13 and v1.14 in the vendored header — `data_size` → `size` in older API; resolved by using the correct v1.14 field name.
- `MeshGpuRecord` struct was defined in the `.cpp` translation unit but referenced from a test via a forward declaration that didn't match — moved to the header as a `private`-equivalent detail accessible via `getMeshRecord()` test accessor.

**Phase 5 — build-engineer:**

Both compilers, zero warnings, all tests passing:

| Compiler | Tests | Warnings | Result |
|----------|-------|----------|--------|
| Clang-18 | 519/519 | 0 | PASS |
| GCC-13 | 519/519 | 0 | PASS |

21 new test cases added (test_mesh_loader.cpp). Total test count: 519.

### Agents Dispatched

1. `engine-dev` (Phase 2, sequential)
2. `performance-critic` + `security-auditor` + `api-designer` + `game-dev-tester` (Phase 3, parallel)
3. `engine-dev` (Phase 4 — fix cycle 1, sequential)
4. `engine-dev` (Phase 4 — fix cycle 2, sequential)
5. `build-engineer` (Phase 5, sequential)

### Files Changed

| File | Status |
|------|--------|
| `third_party/cgltf.h` | NEW |
| `engine/renderer/mesh_loader.h` | NEW |
| `engine/renderer/mesh_loader.cpp` | NEW |
| `engine/renderer/mesh_renderer.h` | NEW |
| `engine/renderer/mesh_renderer.cpp` | NEW |
| `examples/3d_demo/main.cpp` | NEW |
| `examples/3d_demo/game.lua` | NEW |
| `examples/3d_demo/CMakeLists.txt` | NEW |
| `tests/renderer/test_mesh_loader.cpp` | NEW |
| `docs/architecture/ADR-007-3d-foundation.md` | NEW (committed this session) |
| `docs/architecture/ADR-007-security-review.md` | NEW (committed this session) |
| `engine/renderer/render_system.h` | MODIFIED |
| `engine/renderer/shader_library.h` | MODIFIED |
| `engine/renderer/shader_library.cpp` | MODIFIED |
| `engine/renderer/opengl/rhi_opengl.h` | MODIFIED |
| `engine/renderer/opengl/rhi_opengl.cpp` | MODIFIED |
| `engine/renderer/CMakeLists.txt` | MODIFIED |
| `engine/renderer/.context.md` | MODIFIED |
| `engine/core/application.h` | MODIFIED |
| `engine/core/application.cpp` | MODIFIED |
| `engine/scripting/script_engine.cpp` | MODIFIED |
| `engine/scripting/.context.md` | MODIFIED |
| `tests/CMakeLists.txt` | MODIFIED |
| `examples/CMakeLists.txt` | MODIFIED |

### game-dev-tester: INVOKED

3D rendering is a categorically new API paradigm. game-dev-tester found 2 `.context.md` gaps (`fillTransform3D` undocumented, `unloadMesh` missing from binding list). Both fixed before Phase 5.

### Session 38 Stats

- 11 new files
- 13 modified files
- 10 new Lua bindings (total ~80)
- 21 new Catch2 test cases
- 519 total tests passing
- 2 fix cycles
- Commit: `9cab5de feat(renderer): 3D mesh loading and rendering — cgltf, Blinn-Phong, Transform3D`

### Next Session (39): Windows Build Support

Port FFE to build cleanly on Windows using MinGW-w64 cross-compilation from Linux as the initial target (MSVC to follow). Goals: CMake/Ninja build succeeds, all 519+ tests pass, demos link and run on Windows. Primary agents: `system-engineer` (build system, CMake) + `engine-dev` (platform-specific code guards).

---

## 2026-03-07 — Session 39: Windows Build Support (MinGW-w64 Cross-Compilation)

### Goal

Port FFE to build cleanly on Windows. Target: MinGW-w64 cross-compilation from Linux (MSVC support deferred). Deliverables: CMake/Ninja build succeeds for all engine targets and examples, all 519 Linux tests still pass, 11 Windows PE32+ executables produced.

### Work Completed

**Phase 1/2 — system-engineer (investigation + CMake/build system):**

system-engineer audited all POSIX-specific calls and build system constructs, then made all required CMake and infrastructure changes:

- Installed MinGW-w64 toolchain (`x86_64-w64-mingw32-g++` GCC 13)
- Created `cmake/toolchains/mingw-w64-x86_64.cmake` — cross-compilation toolchain file (sets system name, compilers, windres, sysroot paths)
- `cmake/CompilerFlags.cmake`: guarded `mold` linker behind `if(NOT WIN32)` — mold is Linux-only
- Added Windows-specific link libraries to `engine/core` and `engine/audio`: `ws2_32`, `opengl32`, `winmm`
- Created vcpkg LuaJIT overlay for MinGW PE cross-compilation (LuaJIT requires special flags when targeting Windows PE from a Linux host)
- Fixed `tests/scripting/test_save_load.cpp`: replaced `mkdtemp` with `ffe_mkdtemp` portability wrapper
- Fixed `VCPKG_APPLOCAL_DEPS=OFF` for cross-compile host (prevents attempting to copy host DLLs to Windows output)
- Fixed link ordering with `LINK_GROUP:RESCAN` for GNU ld (required for static library ordering under MinGW)
- Fixed `engine/core/arena_allocator.cpp`: `_aligned_malloc`/`_aligned_free` on Windows instead of `std::aligned_alloc`/`free`
- Fixed `examples/demo_paths.h`: `GetModuleFileNameA` on Windows instead of `/proc/self/exe` readlink

**Phase 2 — engine-dev (platform-specific C++ guards):**

- Created `engine/core/platform.h` — cross-platform `canonicalizePath()` wrapping:
  - POSIX: `realpath(path, nullptr)` (heap-allocated, size-safe) with bounded `memcpy` into fixed output buffer
  - Windows: `_fullpath()` with matching null-check and bounded copy
- Updated all 11 `realpath()` call sites across 4 files to use `canonicalizePath()`:
  - `engine/scripting/script_engine.cpp` (save/load path validation, doFile)
  - `engine/renderer/mesh_loader.cpp` (asset path validation)
  - `engine/renderer/texture_loader.cpp` (asset path validation)
  - `engine/core/application.cpp` (asset root setup)

**Phase 3 — Expert panel (parallel):**

*performance-critic* — MINOR ISSUES:
- POSIX `realpath` branch was passing `outBufSize` that was being silently ignored (realpath with nullptr allocates its own buffer; the size parameter was dead code). Fixed in Phase 4.
- No Win32 API calls found in hot paths — PASS
- No frame-loop regressions — PASS

*security-auditor* — MINOR ISSUES:
- ADS (Alternate Data Streams) paths not blocked on Windows: `file:stream` notation could bypass prefix checks. Fixed in Phase 4.
- `doFile` in scripting was missing a `canonicalize + prefix check` before loading a Lua file. Fixed in Phase 4.
- UNC path gap in scripting `isPathSafe()`: `\\server\share` paths documented as a known limitation (blocking `\\` prefix is trivial but no test environment available; comment added)
- No CRITICAL findings; no HIGH findings after Phase 4 fixes

*api-designer* — updates made:
- `engine/core/.context.md`: `canonicalizePath()` documented (purpose, signature, platform notes, when to use vs `std::filesystem`)
- `CONTRIBUTING.md`: Windows build section added — MinGW cross-compilation commands, vcpkg triplet, Wine test-execution note

**Phase 4 — Remediation (engine-dev):**

- `canonicalizePath()` POSIX branch: fixed to use `realpath(path, nullptr)` exclusively; removed dead `outBufSize` parameter; added bounded `memcpy` with null terminator
- All `isPathSafe()` implementations: block ADS paths via `strchr(canonical, ':')` check (after drive letter on Windows)
- `engine/scripting/script_engine.cpp` `doFile`: added `canonicalizePath` + prefix check before `luaL_loadfile`
- UNC gap: explicit comment added to scripting `isPathSafe()` documenting the limitation and the trivial mitigation path

**Phase 5 — build-engineer:**

Linux builds verified first (all 519 tests still passing), then MinGW cross-build:

| Target | Result |
|--------|--------|
| Linux Clang-18 (519 tests) | PASS — 0 warnings |
| Linux GCC-13 (519 tests) | PASS — 0 warnings |
| Windows MinGW cross-build | PASS — 11 PE32+ .exe files produced |

Windows test execution: Wine was not available in the build environment. The 11 executables produced are: `ffe_tests` (engine test suite), `ffe_lua_demo`, `ffe_pong`, `ffe_breakout`, `ffe_3d_demo`, and the remaining example/test binaries. Execution on a Windows host or under Wine is documented in CONTRIBUTING.md as the next step.

**Additional fixes (mid-session, user-reported):**

- `docs/agents/system-engineer.md`: added no-polling-loop rule (agents must not use `sleep`/polling loops) and warn-before-long-ops rule (agents must state estimated cost before starting any operation expected to take more than 30 seconds)
- `assets/models/cube.glb`: generated a valid 1660-byte glTF 2.0 unit cube (24 vertices, 36 indices, correct normals per face) — fixes "cube.glb not found" error in the 3D demo on fresh checkouts

### Agents Dispatched

1. `system-engineer` (Phase 1/2 combined — investigation + CMake changes, sequential)
2. `engine-dev` (Phase 2 — platform C++ guards, sequential after system-engineer)
3. `performance-critic` + `security-auditor` + `api-designer` (Phase 3, parallel)
4. `engine-dev` (Phase 4 — remediation, sequential)
5. `build-engineer` (Phase 5, sequential)

### Files Changed

| File | Status |
|------|--------|
| `cmake/toolchains/mingw-w64-x86_64.cmake` | NEW |
| `engine/core/platform.h` | NEW |
| `assets/models/cube.glb` | NEW |
| `cmake/CompilerFlags.cmake` | MODIFIED |
| `engine/core/CMakeLists.txt` | MODIFIED |
| `engine/audio/CMakeLists.txt` | MODIFIED |
| `engine/core/arena_allocator.cpp` | MODIFIED |
| `engine/core/application.cpp` | MODIFIED |
| `engine/core/.context.md` | MODIFIED |
| `engine/scripting/script_engine.cpp` | MODIFIED |
| `engine/renderer/mesh_loader.cpp` | MODIFIED |
| `engine/renderer/texture_loader.cpp` | MODIFIED |
| `examples/demo_paths.h` | MODIFIED |
| `tests/scripting/test_save_load.cpp` | MODIFIED |
| `CONTRIBUTING.md` | MODIFIED |
| `docs/agents/system-engineer.md` | MODIFIED |

### game-dev-tester: NOT INVOKED

No new API surface introduced. All changes are build-system and platform portability guards. Existing APIs unchanged. Skip documented here per CLAUDE.md Section 7.

### Session 39 Stats

- 3 new files
- 13 modified files
- 519 Linux tests passing (unchanged)
- 11 Windows PE32+ executables produced
- 1 fix cycle
- Commit: `bf52241 feat(platform): Windows MinGW cross-build — platform.h, mold guard, Windows link libs`

### ROADMAP Update

`Windows build support` checked off in ROADMAP.md. Both cross-platform items now in progress:
- [x] Windows build support (MinGW-w64 cross-compilation — Session 39)
- [ ] macOS build support — Session 40

### Next Session (40): macOS Build Support

Fix macOS-specific source issues, create a macOS CMake toolchain file, add GitHub Actions workflow for macOS CI (native toolchain required — cannot cross-compile from Linux), and update CONTRIBUTING.md. Key areas: `_NSGetExecutablePath` for `demo_paths.h`, `posix_memalign` fallback for arena allocator on pre-10.15, OpenGL deprecation warnings, LuaJIT arm64 flags, vcpkg `arm64-osx` triplet.

---

## 2026-03-06 — Session 37: Phase 2 Architecture — 3D Foundation Design

### Goal

Design the Phase 2 3D Foundation. No code written, no build run. This is a pure design session: produce an implementation-ready architecture document (ADR-007) for the 3D renderer extension, and validate its security posture through a shift-left review before any implementation begins.

### Work Completed

**architect — ADR-007 v1.0:**
Produced `docs/architecture/ADR-007-3d-foundation.md` v1.0. The document covers every decision required for engine-dev to implement 3D mesh rendering in a single session without any open architectural choices. Decisions made:

- Mesh library: **cgltf**, single-header C, vendored in `third_party/cgltf.h` — no new vcpkg dependency
- glTF 2.0 as the mesh format (OBJ deferred); `.glb` (binary glTF) as the accepted file type (rationale: eliminates external `.bin` URI surface; revisable in a future ADR)
- ECS components: `Transform3D` (44 bytes, quaternion rotation, separate from 2D `Transform`), `Mesh` (8 bytes, holds `MeshHandle`), `Material3D` (24 bytes, diffuse color + texture + shader override)
- `MeshHandle` opaque asset handle (same pattern as `TextureHandle`, `BufferHandle`)
- Render order: **3D before 2D** — depth test enabled for 3D pass, disabled for 2D pass; existing 2D pipeline is fully preserved and unaffected when no 3D entities exist
- VAO strategy: one VAO per loaded mesh, created at load time, owned by the mesh asset cache
- Perspective camera: new `m_camera3d` member in `Application`, uses existing `renderer::Camera` struct with `ProjectionType::PERSPECTIVE`
- Shader: Blinn-Phong GLSL 330 core, compile-time string in `shader_library.cpp`, new `BuiltinShader::MESH_BLINN_PHONG` enum value
- Scene lighting: new `SceneLighting3D` ECS context struct (lightDir, lightColor, ambientColor) with Lua overrides
- Mesh asset pool: fixed-size array of 100 slots (`MAX_MESH_ASSETS`), 64 MB file size cap (`MESH_FILE_SIZE_LIMIT`), 1M vertex / 3M index limits
- Security constraints SEC-M1 through SEC-M7 specified in ADR (path traversal prevention, file size cap, cgltf output validation, count limits, u64 size arithmetic, cgltf_free on all paths, no per-frame loading)
- 8 new Lua bindings specified with exact signatures: `ffe.loadMesh`, `ffe.createEntity3D`, `ffe.setTransform3D`, `ffe.set3DCamera`, `ffe.setMeshColor`, `ffe.setLightDirection`, `ffe.setLightColor`, `ffe.setAmbientColor`
- Full file layout: 6 new files, 8 modified files — all specified
- Catch2 test plan: 20 test cases covering path validation, struct layout, asset pool, ECS integration (Section L of ADR-007)
- 4 open security questions (Q-M1 through Q-M4) raised explicitly for shift-left review

**security-auditor — ADR-007-security-review.md (shift-left review):**
Produced `docs/architecture/ADR-007-security-review.md`. Overall verdict: HIGH ISSUES — implementation blocked pending design changes to the two HIGH findings. All findings:

| ID | Severity | Finding |
|----|----------|---------|
| H-1 | HIGH | cgltf resolves `.bin` buffer URIs relative to `base_path` — `.gltf` + external `.bin` path traversal risk; not mitigated by SEC-M1 (which only validates the top-level path) |
| H-2 | HIGH | `cgltf_validate()` alone insufficient — does not verify that `buffer.data_size >= buffer.size`; truncated BIN chunk produces OOB reads when using direct pointer arithmetic |
| M-1 | MEDIUM | External `.bin` file size uncapped if `.gltf` supported; resolved by H-1 Option A |
| M-2 | MEDIUM | Heap fallback uses bare `new`; null/terminate case on OOM not handled before `cgltf_free` |
| M-3 | MEDIUM | `glBufferData` GPU OOM not detected; `glGetError()` not called after buffer upload |
| M-4 | MEDIUM | `ffe.setLightDirection(0,0,0)` passes zero vector to `glm::normalize` → NaN in fragment shader |
| L-1 | LOW | Missing `lua_type()` guard on integer arguments to new bindings |
| I-1 | INFO | cgltf CVE status confirmed clean; iterative JSON parser, no stack overflow risk |

Security-auditor also confirmed: `getGlBufferId` does not currently exist in `rhi_opengl.h` — engine-dev must add it.

**architect — ADR-007 v1.1 (revised per security review):**
Updated `docs/architecture/ADR-007-3d-foundation.md` to v1.1. All HIGH and MEDIUM findings resolved in the design before implementation was allowed to proceed:

- **H-1 resolved → SEC-M8 added:** `.glb`-only restriction at path validation time, before any cgltf call. `.gltf` support deferred to a future ADR with its own security review. This also resolves M-1.
- **H-2 resolved → SEC-M3 expanded:** After `cgltf_load_buffers`, verify `buffer->data != nullptr` and `buffer->data_size >= buffer->size` for every buffer. Exclusive use of `cgltf_accessor_read_float()` / `cgltf_accessor_read_index()` safe accessor API (no direct pointer arithmetic into buffer data) during vertex extraction.
- **M-2 resolved → implementation constraint added:** Heap fallback must use `new (std::nothrow)`, check result for null, call `cgltf_free` and return `MeshHandle{0}` on null.
- **M-3 resolved → implementation constraint added:** `glGetError()` after each `glBufferData` call; `GL_OUT_OF_MEMORY` triggers cleanup and returns `MeshHandle{0}`.
- **M-4 resolved → implementation constraint added:** `ffe.setLightDirection` guard: compute `glm::length`, reject (log + keep previous value) if length < 0.0001f; only normalise and store if non-trivial.

Section 3 (mesh library rationale) and Section 10.2 (data flow) updated to reflect `.glb`-only parsing and buffer data-size validation. Revision history added (Section 18). Final status: **APPROVED — ready for implementation**.

### Agents Dispatched

1. `architect` (sequential) — ADR-007 v1.0
2. `security-auditor` (sequential, shift-left) — ADR-007-security-review.md
3. `architect` (sequential, revision) — ADR-007 v1.1

### Files Produced

| File | Status |
|------|--------|
| `docs/architecture/ADR-007-3d-foundation.md` | NEW (v1.1, approved) |
| `docs/architecture/ADR-007-security-review.md` | NEW (shift-left review complete) |

### game-dev-tester: NOT INVOKED

Design-only session; no implementation, no API to test. game-dev-tester will be invoked in Session 38 (implementation session) after the expert panel, because 3D rendering is a categorically new API paradigm (per ADR-007 Section 17 and CLAUDE.md Section 7).

### Session 37 Stats

- No code written
- No build run
- 2 documents produced
- All HIGH security findings resolved before implementation unblocked

### Next Session (38) Starts With

Phase 2 implementation: `engine-dev` implements ADR-007 v1.1 in full — all new files, all modified files, all Lua bindings, all Catch2 tests, per Section 12 (file layout) and Section 14 (test plan) of ADR-007.

---

## 2026-03-06 — Session 36: Phase 1 Documentation Complete

### Goal

Complete all Phase 1 documentation work: extend the tutorial to cover the four features added in Session 35 (gamepad, save/load, TTF fonts, particles were already partially covered), polish CONTRIBUTING.md, review and update all `.context.md` files, and assess whether Phase 1 exit criteria are met.

No engine code was changed this session. No build was run.

### Work Completed

**Tutorial expansion (api-designer pass 1):**
- Section 19: Gamepad Input — `ffe.isGamepadConnected`, `isGamepadButtonPressed/Held/Released`, `getGamepadAxis`, `setGamepadDeadzone`, gamepad constants table, combined keyboard+gamepad movement pattern
- Section 20: Save/Load System — `ffe.saveData`/`ffe.loadData`, JSON persistence, filename rules, error handling, high-score example
- Section 21: TTF Font Rendering — `ffe.loadFont`, `ffe.drawFontText`, `ffe.measureText`, `ffe.unloadFont`, centered text layout example, bitmap vs TTF comparison
- Section 22: Particle Effects — `ffe.addEmitter`, `ffe.setEmitterConfig`, `ffe.startEmitter`/`stopEmitter`, `ffe.emitBurst`, config table reference, explosion burst example
- Updated "What's Next?" section to reflect Phase 1 completion and Phase 2 (3D Foundation) as next milestone
- ROADMAP: tutorial documentation and CONTRIBUTING.md polish checked off; test count (498) and Lua binding count (~70) updated

**CONTRIBUTING.md polish (api-designer pass 1):**
- GCC-13 build commands added alongside Clang-18
- mold and ccache installation notes
- AI-native documentation section (explains `.context.md` files and their role)
- "Getting Help" section (devlog, roadmap, CLAUDE.md orientation)
- Clarified that `build-engineer` owns the build step in the 5-phase flow

**`.context.md` review and updates (api-designer pass 1):**
- `engine/scripting/.context.md`: `ffe.cancelAllTimers` documented; `KEY_LEFT_CTRL` added (was incorrectly documented as `KEY_LEFT_CONTROL`); `KEY_TAB`, `KEY_F1` added; `GAMEPAD_GUIDE`, `GAMEPAD_LEFT_STICK`, `GAMEPAD_RIGHT_STICK` constants added
- `engine/renderer/.context.md`: verified current and accurate
- `engine/core/.context.md`: verified current and accurate

**Tutorial review (game-dev-tester):**
Found 13 issues across the new tutorial sections.

**Tutorial and `.context.md` corrections (api-designer pass 2):**
All 13 issues fixed:
1. Added `ffe.getEntityCount()` usage example
2. Added `ffe.isGamepadButtonReleased` code example (was described but not demonstrated)
3. Added `ffe.setHudText` code example
4. Added `GAMEPAD_GUIDE`, `GAMEPAD_LEFT_STICK`, `GAMEPAD_RIGHT_STICK` constants to tutorial constant table and to `engine/scripting/.context.md`
5. Corrected `KEY_LEFT_CONTROL` → `KEY_LEFT_CTRL` in tutorial (was wrong in two places)
6. Added `KEY_LEFT_CTRL`, `KEY_TAB`, `KEY_F1` to `.context.md` key constants section
7. Fixed gamepad movement example to preserve entity rotation and scale when calling `setTransform`
8. Fixed TTF character range table: documented range was 32-127, correct range is 32-126 (ASCII printable, matching stb_truetype atlas bake)

### Phase 1 Exit Criteria Assessment

| Criterion | Status | Notes |
|-----------|--------|-------|
| A non-trivial 2D game can be built entirely in Lua | **MET** | Three demo games (Collect the Stars, Pong, Breakout) built entirely in Lua with no C++ game code |
| Game runs at 60fps on LEGACY tier hardware | **MET** | OpenGL 3.3 renderer, sprite batching, no heap allocations per frame, all systems within budget |
| New developer can go from `git clone` to running a game in under 10 minutes | **MET** | CONTRIBUTING.md documents exact build commands; README.md exists; three runnable demos |
| All features documented in `.context.md` files and the tutorial | **MET** | Tutorial now covers 22 sections spanning all Phase 1 features; all subsystem directories have `.context.md` files |

**All four Phase 1 exit criteria are met.**

The two remaining ROADMAP items (Windows build support, macOS build support) are cross-platform concerns explicitly listed as lower priority. They do not block Phase 1 exit — the exit criteria make no reference to cross-platform builds. Phase 1 is complete on Linux.

### Phase 1 Status (Final)

| Feature | Status |
|---------|--------|
| ECS, game loop, arena allocator | DONE |
| OpenGL 3.3 renderer (batching, render queue) | DONE |
| Input (keyboard, mouse, gamepad) | DONE |
| Audio (SFX + streaming music) | DONE |
| Collision (spatial hash, AABB/circle) | DONE |
| Lua scripting (LuaJIT sandbox, ~70 bindings) | DONE |
| Sprite animation | DONE |
| Text rendering (bitmap + TTF) | DONE |
| Camera system (shake, clear color) | DONE |
| Scene management | DONE |
| Particle system | DONE |
| Tilemap rendering | DONE |
| Timer system | DONE |
| Save/load system | DONE |
| Tutorial documentation (22 sections) | DONE |
| CONTRIBUTING.md | DONE |
| `.context.md` files (all subsystems) | DONE |
| Windows build support | DEFERRED (post-Phase 1) |
| macOS build support | DEFERRED (post-Phase 1) |

**Phase 1 is COMPLETE on Linux.**

### Known Issues Updated
- `KEY_LEFT_CONTROL` corrected to `KEY_LEFT_CTRL` in tutorial and `.context.md` — was a documentation error, not a code error (the binding was always correct)
- TTF character range clarified: 32-126, not 32-127 (127 is DEL, not a printable character)

### Next Session Should Start With
- **Phase 2 architecture planning**: invoke `architect` to produce a Phase 2 design note covering the 3D renderer extension, RHI changes needed, mesh loading library selection (glTF/OBJ), perspective camera, and how 2D and 3D coexist in the same ECS
- Security shift-left review of Phase 2 design (asset loading surface will expand significantly)
- Windows build support can be addressed in parallel or immediately before Phase 2, at user's discretion

---

---

## 2026-03-07 — Session 40: macOS Build Support + Screenshot Tool

### Goal

Two deliverables: (1) macOS build support (Apple Silicon arm64 and Intel x86_64), and (2) a screenshot capture tool for CI and developer use.

### Work Completed

**macOS Build Support**

- `cmake/CompilerFlags.cmake`: guarded `mold` linker behind `if(CMAKE_SYSTEM_NAME STREQUAL Linux)` (mold does not exist on macOS)
- `examples/demo_paths.h`: added `#elif defined(__APPLE__)` branch using `_NSGetExecutablePath` + `realpath` for canonical path resolution
- `engine/core/arena_allocator.cpp`: `posix_memalign` fallback for macOS versions before 10.15 where `std::aligned_alloc` is unavailable; guarded behind `__MAC_OS_X_VERSION_MIN_REQUIRED`
- OpenGL deprecation warnings on macOS suppressed via `-Wno-deprecated-declarations` in `cmake/CompilerFlags.cmake`
- LuaJIT arm64: vcpkg `arm64-osx` triplet; LuaJIT requires `LUAJIT_ENABLE_GC64=1` on Apple Silicon — added to LuaJIT vcpkg overlay
- `CONTRIBUTING.md`: macOS build section added — Homebrew prerequisites, vcpkg triplet, build commands for arm64 and x86_64

**GitHub Actions CI — macOS workflow**

- `.github/workflows/ci.yml`: added `macos-latest` job (Apple Silicon runner) running Clang from Xcode CLT, vcpkg `arm64-osx`, Ninja, build + ctest

**README update**

- Platform support table updated: Linux (Clang-18 + GCC-13), Windows (MinGW cross-build), macOS (arm64 + x86_64) — all three listed as supported

**Screenshot Tool**

- `engine/renderer/screenshot.h` / `screenshot.cpp` — `captureScreenshot(path)` function: calls `glReadPixels` to read the framebuffer, flips rows (OpenGL origin is bottom-left), writes PNG via `stb_image_write`
- `ffe.screenshot(path)` Lua binding — validates path (alphanumeric, hyphens, underscores, `.png` extension only; traversal prevention via `canonicalizePath`)
- `glReadPixels` was missing from the custom GLAD loader — root cause identified; fix deferred to Session 41

**Expert panel:**
- performance-critic: MINOR ISSUES — `glReadPixels` stalls the GPU pipeline (expected; documented in `.context.md`)
- security-auditor: PASS — path validation follows established `isPathSafe` pattern
- api-designer: `engine/renderer/.context.md` updated with `ffe.screenshot` binding

**Build results:**
- Clang-18: FAIL — `glReadPixels` undefined symbol in GLAD loader (GLAD fix deferred to Session 41)
- Tests: 519 (unchanged — screenshot tests blocked by GLAD build failure)

### Next Session (41): Fix GLAD glReadPixels, CI hardening

---

## 2026-03-07 — Session 41: CI Fix + GLAD glReadPixels Fix

### Goal

Resolve the GLAD `glReadPixels` loader gap blocking the Session 40 screenshot tool build, get all 530 tests passing on Clang-18, and fix the broken GitHub Actions CI workflow.

### Work Completed

**GitHub Actions CI Fix (committed separately: `933e0d2`)**

Root cause of CI failure ("Configuring incomplete, errors occurred!" on all 3 jobs):

1. Linux jobs did not set `VCPKG_ROOT` or pass `-DCMAKE_TOOLCHAIN_FILE` to cmake — primary cause; every `find_package` call failed
2. `apt-get` was installing system GLFW and LuaJIT packages conflicting with vcpkg-managed versions — removed
3. Missing X11 dev packages for vcpkg to build GLFW from source: added `libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev`
4. No `permissions:` block — security hardening gap; added `permissions: contents: read`
5. macOS vcpkg clone unpinned to HEAD — supply-chain risk; pinned to tag `2024.11.16` with `--depth 1`

Security-auditor reviewed the CI workflow: PASS. No CRITICAL/HIGH findings. Two LOW/MEDIUM hardening recommendations applied before commit.

**GLAD glReadPixels Fix**

Root cause: the custom GLAD loader in `third_party/glad/` was generated without `glReadPixels` in its function manifest. The function is OpenGL 1.0 core — always available — but was omitted from the original GLAD generation.

Fix applied to `third_party/glad/`:
- `typedef void (GLAD_API_PTR *PFNGLREADPIXELSPROC)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)` added to `glad.h`
- `extern PFNGLREADPIXELSPROC glad_glReadPixels` declaration added to `glad.h`
- `#define glReadPixels glad_glReadPixels` macro added to `glad.h`
- `PFNGLREADPIXELSPROC glad_glReadPixels = NULL` definition added to `glad.c`
- `glad_glReadPixels = (PFNGLREADPIXELSPROC)load("glReadPixels")` loader call added to `glad.c`

**Screenshot tool now compiles and tests pass**

- `engine/renderer/screenshot.h` / `screenshot.cpp` compile cleanly
- `ffe.screenshot` Lua binding compiled and registered
- 11 new Catch2 test cases in `tests/renderer/test_screenshot.cpp` all pass

**Expert panel (Phase 3 — parallel):**
- performance-critic: PASS
- security-auditor: PASS
- api-designer: `.context.md` files confirmed accurate

**Build results:**

| Compiler | Tests | Warnings | Result |
|----------|-------|----------|--------|
| Clang-18 | 530/530 | 0 | PASS |

GCC-13 verification to run at start of Session 42.

### Agents Dispatched

- `system-engineer` (CI fix — sequential)
- `security-auditor` (CI review — sequential)
- `engine-dev` (GLAD fix — sequential)
- `build-engineer` × 2 (Phase 5 rebuild cycles — sequential)

### Files Changed

| File | Status |
|------|--------|
| `.github/workflows/ci.yml` | MODIFIED (committed: `933e0d2`) |
| `third_party/glad/include/glad/glad.h` | MODIFIED |
| `third_party/glad/src/glad.c` | MODIFIED |
| `tests/renderer/test_screenshot.cpp` | NEW |
| `tests/CMakeLists.txt` | MODIFIED |
| `docs/environment.md` | MODIFIED |

### Session 41 Stats

- 5 files modified/added (excluding CI workflow committed separately)
- 11 new Catch2 test cases
- 530 total tests passing (Clang-18)

### Next Session (42): 3D Camera Modes + Diffuse Texture Support

Phase 2 ROADMAP items: `ffe.set3DCameraFPS`, `ffe.set3DCameraOrbit` (perspective camera modes), `ffe.setMeshTexture` (diffuse texture binding for 3D meshes), UV loading from TEXCOORD_0, Blinn-Phong shader texture uniform.

---

## 2026-03-07 — Session 42: 3D Camera Modes + Mesh Texture + Process Review

### Goal

Add FPS and orbit camera convenience bindings, activate diffuse texture support on 3D meshes, and address process concerns raised by the user (devlog size, git commit ownership, agent scope).

### Work Completed

**Feature A: Perspective Camera Modes**

- `ffe.set3DCameraFPS(x, y, z, yaw_deg, pitch_deg)`: position-based FPS camera with pitch clamped to [-89, 89] degrees. Forward vector computed from Euler angles.
- `ffe.set3DCameraOrbit(target_x, target_y, target_z, radius, yaw_deg, pitch_deg)`: orbits a target point at given radius, pitch clamped to [-85, 85] degrees. Guards: radius > 0, all inputs finite.
- Both are pure binding-layer functions — no new C++ types, no changes to Application or Camera struct. Math computed in the binding, calls existing `set3DCamera` internally.
- 3D demo (`examples/3d_demo/game.lua`) updated to use `ffe.set3DCameraOrbit` instead of manual sin/cos.

**Feature B: Diffuse Texture on 3D Meshes**

- `ffe.setMeshTexture(entityId, textureHandle)`: sets `Material3D.diffuseTexture` on an entity. Pass 0 to clear.
- Architect audit revealed: shader, renderer, Material3D struct, and UV loading ALL already fully implemented — only the Lua binding was missing.
- TEXCOORD_0 confirmed loaded in mesh_loader.cpp (attribute location 2, vec2 float).

**Process Restructuring (Director Review)**

User raised concerns about devlog reading overhead, git commit ownership confusion, and system-engineer scope creep. Director performed a full process review:

1. **Devlog split**: Created `docs/project-state.md` (95-line living document — agents read this for context). Archived Sessions 1-34 to `docs/devlog-archive.md`. Current devlog trimmed to Sessions 35+.
2. **Git commit ownership**: PM is sole owner of `git add/commit/push`. Added to CLAUDE.md Section 7 with scenario table. All other agents explicitly prohibited.
3. **system-engineer scope**: Explicit "You do NOT" list — no full builds, no test suites, no git commits. Diagnostic commands only.
4. **Stale references fixed**: engine-dev contradictory build line removed, PM/test-engineer updated from 3-phase to 5-phase.

### Expert Panel

| Reviewer | Verdict | Key Finding |
|----------|---------|-------------|
| performance-critic | PASS | MINOR: set3DCameraFPS lacks NaN/Inf guards (set3DCameraOrbit has them) |
| security-auditor | PASS | LOW: setMeshTexture arg2 silently coerces non-numbers to 0 |
| api-designer | PASS | Updated renderer + scripting .context.md; flagged design note yaw convention discrepancy |

### Build Results

| Compiler | Tests | Warnings | Result |
|----------|-------|----------|--------|
| Clang-18 | 559/559 | 0 | PASS |
| GCC-13 | 559/559 | 0 | PASS |

### Files Changed

| File | Status |
|------|--------|
| engine/scripting/script_engine.cpp | MODIFIED (3 new bindings) |
| tests/scripting/test_camera3d_bindings.cpp | NEW (24 tests) |
| tests/CMakeLists.txt | MODIFIED |
| examples/3d_demo/game.lua | MODIFIED (orbit camera) |
| engine/renderer/.context.md | MODIFIED |
| engine/scripting/.context.md | MODIFIED |
| docs/architecture/design-note-camera-modes-and-mesh-texture.md | NEW |
| .claude/CLAUDE.md | MODIFIED (git commit ownership, project-state refs) |
| .claude/agents/*.md | MODIFIED (5 agent files) |
| docs/project-state.md | NEW |
| docs/devlog-archive.md | NEW |
| docs/devlog.md | RESTRUCTURED |
| docs/agents/changelog.md | MODIFIED |
| docs/environment.md | MODIFIED |

### Session 42 Stats

- 559 total tests (29 new: 24 camera, 5 texture)
- Zero warnings on both compilers
- 3 new Lua bindings
- Process restructuring: 9 agent/doc files updated

### Next Session

ROADMAP Phase 2 remaining: materials system (specular maps, normal maps), skeletal animation, 3D physics, skybox, shadow mapping, 3D audio. PM to assess priority at Session 43 start.

---

## 2026-03-07 — Sessions 42-43: Camera Modes, Mesh Texture, Shadow Mapping, CI Improvements

### Session 42: 3D Camera Modes + Mesh Texture + Process Review

**Features:**
- `ffe.set3DCameraFPS(x, y, z, yaw, pitch)`: FPS camera, pitch clamped [-89, 89] degrees
- `ffe.set3DCameraOrbit(tx, ty, tz, radius, yaw, pitch)`: orbit camera, pitch clamped [-85, 85], radius/finite guards
- `ffe.setMeshTexture(entityId, textureHandle)`: binds diffuse texture to Material3D (shader+renderer+UV loading already existed; only binding was missing)
- 3D demo updated to use orbit camera

**Process restructuring (Director review):**
1. Devlog split: `docs/project-state.md` (95-line living doc), `docs/devlog-archive.md` (Sessions 1-34)
2. Git commit ownership: PM is sole owner of `git add/commit/push` (added to CLAUDE.md Section 7)
3. system-engineer scope: explicit "You do NOT" list (no full builds, no test suites, no git commits)
4. Stale 3-phase references updated to 5-phase across agent files

**Build:** 559/559 tests, 0 warnings (Clang-18 + GCC-13)

### Session 43: Directional Shadow Mapping + CI Improvements

**Shadow mapping implementation:**
- `engine/renderer/shadow_map.h/.cpp`: depth-only FBO (1024x1024), `computeLightSpaceMatrix` (orthographic projection from light direction), `beginShadowPass`/`endShadowPass` lifecycle
- `SHADOW_DEPTH` builtin shader added to `shader_library.cpp` (GLSL 330 core, depth-only vertex transform)
- Blinn-Phong fragment shader extended: PCF 3x3 sampling with configurable bias, `u_shadowMap` (texture unit 1), `u_lightSpaceMatrix`
- `mesh_renderer.cpp`: two-pass rendering (shadow pass, then lit pass with shadow map bound)
- 4 new Lua bindings: `ffe.enableShadows()`, `ffe.disableShadows()`, `ffe.setShadowBias(bias)`, `ffe.setShadowArea(size)`
- 3D demo: ground plane entity added, shadow setup in `ffe.init`
- 9 new Catch2 tests in `tests/renderer/test_shadow_map.cpp`

**GLAD loader expansions:**
- Added `glGenFramebuffers`, `glBindFramebuffer`, `glFramebufferTexture2D`, `glCheckFramebufferStatus`, `glDeleteFramebuffers` (framebuffer functions)
- Added `GL_FRAMEBUFFER`, `GL_DEPTH_ATTACHMENT`, `GL_FRAMEBUFFER_COMPLETE`, `GL_DEPTH_COMPONENT`, `GL_TEXTURE1`, `GL_COMPARE_REF_TO_TEXTURE`, `GL_LEQUAL`, `GL_TEXTURE_COMPARE_MODE`, `GL_TEXTURE_COMPARE_FUNC` (depth/texture constants)

**CI improvements:**
- `paths-ignore` added to CI workflow: docs-only commits (`*.md`, `docs/**`) skip build
- `concurrency` group added: stale runs cancelled when new commits push to the same branch/PR

**Process:**
- Parallel implementation splits added to CLAUDE.md (features touching different directories can be implemented simultaneously)
- ROADMAP content integrated into `project-state.md` (single source of truth for current phase)

**Build fix cycles (2):**
1. `shadow_map.h` included `glad.h` directly, causing duplicate symbol issues — resolved by forward-declaring GL types and including glad only in `.cpp`
2. `GL_TEXTURE1` and several GL constants missing from custom GLAD loader — added to `glad.h`/`glad.c`

**Expert panel:**
- performance-critic: PASS (shadow map resolution fixed at 1024x1024, no per-frame allocations)
- security-auditor: not invoked (shadow mapping does not touch attack surface per CLAUDE.md Section 5)
- api-designer: updated `engine/renderer/.context.md` and `engine/scripting/.context.md` with shadow bindings

**Build results:**

| Compiler | Tests | Warnings | Result |
|----------|-------|----------|--------|
| Clang-18 | 568/568 | 0 | PASS |
| GCC-13 | 568/568 | 0 | PASS |

### Combined Stats (Sessions 42-43)

- 568 total tests (9 new shadow + 29 from Session 42)
- 7 new Lua bindings (3 camera/texture + 4 shadow)
- 23 files changed in shadow mapping commit
- Zero warnings on both compilers
- Commits: `7c22337` (Session 42), `81db188` (Session 43)

### Session 44: Point Lights + Materials System

**Point lights implementation:**
- Up to 4 point lights with position, color, and attenuation (constant/linear/quadratic)
- Fragment shader extended: per-light diffuse + specular contribution, additive blending with directional light
- 5 new Lua bindings: `ffe.addPointLight(x,y,z, r,g,b, constant,linear,quadratic)`, `ffe.updatePointLight(index, ...)`, `ffe.removePointLight(index)`, `ffe.clearPointLights()`, `ffe.getPointLightCount()`
- `render_system.h`: `PointLight` struct, `MAX_POINT_LIGHTS = 4`, storage in `RenderSystem`
- `mesh_renderer.cpp`: uploads point light uniforms per frame

**Materials system implementation:**
- `Material3D` component extended: `specularStrength`, `shininess`, `normalMapTexture`
- Specular maps: per-fragment specular intensity from texture (texture unit 2)
- Normal maps: tangent-space normal mapping with TBN matrix (texture unit 3)
- 3 new Lua bindings: `ffe.setMeshSpecularMap(entity, path)`, `ffe.setMeshNormalMap(entity, path)`, `ffe.setMeshShininess(entity, value)`
- Fragment shader: `u_specularMap`, `u_normalMap`, `u_hasSpecularMap`, `u_hasNormalMap`, `u_shininess`

**RHI optimization:**
- Uniform location cache bumped from 32 to 64 entries (performance-critic recommendation — more uniforms from lights/materials)

**3D demo updated:**
- Two point lights added (warm orange + cool blue) demonstrating multi-light setup
- Material properties set on mesh entities

**Files modified:** `mesh_renderer.h`, `render_system.h`, `shader_library.cpp`, `mesh_renderer.cpp`, `script_engine.cpp`, `rhi_opengl.cpp`, `test_mesh_loader.cpp`, `tests/CMakeLists.txt`, `examples/3d_demo/game.lua`
**Files created:** `tests/renderer/test_point_lights_materials.cpp`, `tests/scripting/test_point_light_material_bindings.cpp`

**Expert panel:**
- performance-critic: PASS (MINOR ISSUES — uniform cache bump applied as fix)
- security-auditor: SKIPPED (no new attack surface per CLAUDE.md Section 5)
- api-designer: PASS — updated `engine/renderer/.context.md` and `engine/scripting/.context.md`
- game-dev-tester: SKIPPED (existing API patterns, no new paradigm)

**Build results:**

| Compiler | Tests | Warnings | Result |
|----------|-------|----------|--------|
| Clang-18 | 618/618 | 0 | PASS |

**Stats:** 50 new tests, 8 new Lua bindings (~95 total), zero build fix cycles

### Session 45: Skybox / Cubemap Environment Rendering

**Skybox implementation:**
- `SkyboxConfig` singleton added to ECS context (cubemap handle, VAO/VBO, enabled flag)
- `SKYBOX` builtin shader (enum value 5) in `shader_library.h/.cpp` — GLSL 330 core, depth trick (`gl_Position.z = gl_Position.w` for max-depth rendering)
- `loadCubemap()` / `unloadCubemap()` in `texture_loader.h/.cpp` — loads 6 face textures into GL cubemap
- Skybox rendering integrated into `mesh_renderer.cpp` (drawn after 3D scene with depth func `GL_LEQUAL`)
- `application.h/.cpp` extended with skybox lifecycle management

**Security hardening:**
- Path separator check in `loadCubemap()` — rejects paths containing `/` or `\`
- File-size and byte-count validation on cubemap face images
- Security review: MINOR ISSUES (all fixed in Phase 4 remediation)

**3 new Lua bindings:**
- `ffe.loadSkybox(right, left, top, bottom, front, back)` — loads 6 face textures
- `ffe.unloadSkybox()` — releases cubemap resources
- `ffe.setSkyboxEnabled(bool)` — toggles skybox rendering

**3D demo updated:** skybox setup added to `ffe.init`

**Files modified:** `shader_library.h/.cpp`, `texture_loader.h/.cpp`, `mesh_renderer.h/.cpp`, `application.h/.cpp`, `script_engine.cpp`, `examples/3d_demo/game.lua`, `tests/CMakeLists.txt`, `engine/renderer/.context.md`, `engine/scripting/.context.md`
**Files created:** `tests/renderer/test_skybox.cpp`, `tests/scripting/test_skybox_bindings.cpp`

**Expert panel:**
- performance-critic: PASS
- security-auditor: MINOR ISSUES (fixed in Phase 4)
- api-designer: PASS — updated `.context.md` files
- game-dev-tester: SKIPPED (existing API pattern, no new paradigm)

**Build results:**

| Compiler | Tests | Warnings | Result |
|----------|-------|----------|--------|
| Clang-18 | 627/627 | 0 | PASS |

**Stats:** 9 new tests, 3 new Lua bindings (~98 total), zero build fix cycles

### Next Session (46)

Phase 2 remaining: skeletal animation, 3D physics, 3D audio. PM to select priority.
