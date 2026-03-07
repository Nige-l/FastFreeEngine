# FastFreeEngine Roadmap

> **Archival reference.** Agents should NOT read this file every session. For current-phase status and remaining deliverables, read `docs/project-state.md` instead. This file is the full historical record of all phases and is only consulted when planning a phase transition or reviewing the overall project arc.

This document defines the phased development plan for FFE. Each phase builds on the previous one. Phases are not calendar-bound — they are milestone-bound. A phase is complete when all its deliverables are working, tested, and documented.

---

## Phase 1: 2D Engine (Current)

**Goal:** A complete, polished 2D game engine that can ship real games.

### Delivered
- [x] ECS (EnTT wrapper, function-pointer systems)
- [x] 2D sprite rendering (OpenGL 3.3, batching, render queue)
- [x] Sprite animation (grid atlas, configurable timing)
- [x] Audio (miniaudio, WAV/OGG, SFX + streaming music)
- [x] 2D collision detection (spatial hash, AABB/circle, Lua callbacks)
- [x] Input system (keyboard + mouse, pressed/held/released)
- [x] Lua scripting sandbox (LuaJIT, 1M instruction budget, ~70 bindings)
- [x] Bitmap text rendering (8x8 font, screen-space HUD)
- [x] Texture loading (stb_image, path traversal prevention)
- [x] Arena allocator (per-frame, zero hot-path heap allocations)
- [x] Camera system (ortho, shake effects)
- [x] 3 playable demo games (Collect the Stars, Pong, Breakout)
- [x] AI-native documentation (.context.md files)
- [x] 498 Catch2 tests, zero warnings on Clang-18 and GCC-13

### Remaining
- [x] Sprite rotation in render pipeline (Transform has rotation, DrawCommand passes it through)
- [x] Sprite flipping (horizontal/vertical, for character facing direction)
- [x] Tilemap rendering (efficient batch rendering of tile grids)
- [x] Scene management (load/unload scenes, transitions)
- [x] Timer/scheduler API from Lua (ffe.after, ffe.every)
- [x] Gamepad input (GLFW gamepad API, 4 pads, 15 buttons, 6 axes, deadzone)
- [x] Particle system (engine-side, not Lua entity hacks)
- [x] TTF font rendering (stb_truetype, scalable text, 8 font slots)
- [x] Save/load system (JSON on disk, path security, atomic writes, 128 file limit)
- [x] Tutorial documentation covering all features
- [x] CONTRIBUTING.md polish
- [x] Windows build support (MinGW-w64 cross-compilation from Linux)
- [x] macOS build support (Apple Silicon arm64 and Intel x86_64)

### Phase 1 Exit Criteria
- A non-trivial 2D game (beyond demos) can be built entirely in Lua
- The game runs at 60fps on LEGACY tier hardware
- A new developer can go from `git clone` to running a game in under 10 minutes
- All features documented in .context.md files and the tutorial

---

## Phase 2: 3D Foundation

**Goal:** Extend the renderer to support 3D games while keeping full 2D capability.

### Deliverables
- [ ] 3D mesh loading (glTF or OBJ via a vendored/vcpkg library)
- [ ] 3D mesh rendering (vertex buffers, index buffers, basic materials)
- [ ] Perspective camera (FPS and orbit modes)
- [ ] Basic lighting (directional + point lights, Phong or Blinn-Phong)
- [ ] Materials system (diffuse, specular, normal maps)
- [ ] Skeletal animation (bone hierarchy, skinning)
- [ ] 3D physics integration (likely a vendored library — Bullet, Jolt, or similar)
- [ ] Skybox / environment rendering
- [ ] Shadow mapping (at least one technique on STANDARD+ tiers)
- [ ] 3D audio (positional sound sources)
- [ ] Lua bindings for all 3D features
- [ ] At least one 3D demo game
- [ ] .context.md files for all new subsystems

### Architecture Constraints
- 2D and 3D share the same ECS, input, audio, scripting, and networking layers
- The RHI abstraction must support both 2D and 3D rendering paths
- 3D features that require STANDARD or MODERN tier must be clearly gated
- LEGACY tier games remain 2D-only — no silent degradation

---

## Phase 3: Standalone Editor

**Goal:** A graphical application for building games with FFE, like Unity or Unreal Editor.

### Deliverables
- [ ] Standalone editor application (separate binary from the game runtime)
- [ ] Scene view with 2D and 3D viewport
- [ ] Entity inspector (create, modify, delete entities and components)
- [ ] Asset browser (textures, audio, scripts, meshes)
- [ ] Scene serialisation (save/load scene files)
- [ ] Play-in-editor (run the game inside the editor viewport)
- [ ] Build pipeline (export game as standalone executable)
- [ ] Undo/redo system
- [ ] Project creation wizard
- [ ] LLM integration panel (connect AI assistant, generate code, explain systems)

### Architecture Constraints
- Editor uses the engine as a library — same code paths as the runtime
- Editor is a separate CMake target, not compiled into game builds
- Editor should work on LEGACY tier (the editor itself must run on old hardware too)
- Scene file format must be human-readable (JSON or similar) and version-controlled

---

## Phase 4: Networking and Multiplayer

**Goal:** Built-in multiplayer support for both 2D and 3D games.

### Deliverables
- [ ] Client-server architecture (authoritative server model)
- [ ] ECS state replication (sync entity components across network)
- [ ] Network transport (UDP with reliability layer, or use a library like ENet/GameNetworkingSockets)
- [ ] Lobby/matchmaking API
- [ ] Network prediction and interpolation (client-side prediction, server reconciliation)
- [ ] Lua bindings for networking (send/receive messages, RPC)
- [ ] Lag compensation
- [ ] Network security (packet validation, rate limiting, anti-cheat hooks)
- [ ] At least one networked demo game
- [ ] NAT traversal / relay server support

### Architecture Constraints
- Networking must be designed for the ECS — replicate components, not objects
- Server can run headless (no renderer, no audio)
- Network code is a primary attack surface — security-auditor reviews all of it
- Must work on LEGACY tier (low bandwidth, single-threaded network loop is fine)

---

## Phase 5: Website and Learning Platform

**Goal:** A documentation and training website that gets young people into game development and engineering.

### Deliverables
- [ ] Documentation site (API reference generated from .context.md files)
- [ ] Getting Started guide (install, build, first game in 15 minutes)
- [ ] Tutorial series (beginner to advanced, 2D and 3D)
- [ ] "How It Works" deep dives (ECS internals, renderer architecture, networking)
- [ ] Video/interactive content (embedded code editors, live examples)
- [ ] Community showcase (games built with FFE)
- [ ] Asset library (free textures, sounds, meshes for learning)
- [ ] Forum or Discord integration
- [ ] "Build Your Own Engine" learning track (understand FFE by rebuilding parts of it)

### Architecture Constraints
- Site must be fast and accessible (no heavy JS frameworks, works on old browsers)
- Content must be maintainable — generated from source where possible
- Tutorials must be tested against the current engine version (CI validates examples)

---

## Cross-Cutting Concerns (All Phases)

These apply throughout development and are never "done":

- **Performance:** 60fps on declared minimum tier, or the feature does not ship
- **Security:** All external input validated, sandbox escape-proof, network hardened
- **Testing:** Every feature has Catch2 tests, zero warnings on both compilers
- **AI documentation:** Every subsystem has a current .context.md file
- **Cross-platform:** Linux first, then Windows, then macOS
- **Accessibility:** Runs on old hardware, works with screen readers where applicable

---

## How Sessions Map to This Roadmap

1. `project-manager` reads `docs/project-state.md` at session start — it contains the current phase's remaining deliverables
2. PM only reads this full ROADMAP.md when planning a phase transition or when the user asks about long-term direction
3. When a deliverable is completed, PM updates `project-state.md` (moves item from Remaining to Delivered)
4. Phase transitions require a review: all exit criteria met, all tests passing, docs updated
5. When a phase is completed, PM updates both this file and `project-state.md`

This roadmap is a living document. Update it as priorities shift and new requirements emerge.
