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
- [x] Client-server architecture (authoritative server model)
- [x] ECS state replication (sync entity components across network)
- [x] Network transport (ENet — UDP with reliability layer)
- [x] Lobby/matchmaking API
- [x] Network prediction and interpolation (client-side prediction, server reconciliation)
- [x] Lua bindings for networking (30 bindings)
- [x] Lag compensation
- [x] Network security (packet validation, rate limiting)
- [x] At least one networked demo game (Net Arena)
- [ ] NAT traversal / relay server support — **deferred to backlog** (relay is infrastructure/ops, not engine library code; ENet direct connect covers LAN and public IP scenarios)

### Architecture Constraints
- Networking must be designed for the ECS — replicate components, not objects
- Server can run headless (no renderer, no audio)
- Network code is a primary attack surface — security-auditor reviews all of it
- Must work on LEGACY tier (low bandwidth, single-threaded network loop is fine)

---

## Phase 5: Website and Learning Platform — COMPLETE (Sessions 62-65)

**Goal:** A documentation and training website that gets young people into game development and engineering.

### Delivered (Sessions 62-65)
- [x] Documentation site (MkDocs + Material theme, API reference generated from .context.md files)
- [x] Getting Started guide (install, build, first game in 15 minutes)
- [x] Tutorial series (first 2D game, first 3D game, multiplayer basics)
- [x] "How It Works" deep dives (ECS internals, renderer architecture, networking)
- [x] "Build Your Own Engine" learning track (first installment: Build an ECS from Scratch)
- [x] Community showcase (all 5 official demo games)
- [x] GitHub Pages deployment (auto-deploy on push to main)

### Deferred to Backlog
- [ ] Video/interactive content — requires WASM tooling and video production infrastructure (different skillset/tooling from engine development)
- [ ] Asset library — requires curation infrastructure and hosting beyond the documentation site
- [ ] Forum or Discord integration — community operations, not engine development work

### Architecture Constraints
- Site must be fast and accessible (no heavy JS frameworks, works on old browsers)
- Content must be maintainable — generated from source where possible
- Tutorials must be tested against the current engine version (CI validates examples)

---

## Phase 6: Showcase Game — COMPLETE (Session 66)

**Goal:** Build a non-trivial 3D game that stress-tests the engine across all subsystems and proves FFE can ship a real, playable experience.

### Delivered
- [x] "Echoes of the Ancients" — 3-level 3D showcase game with main menu and victory flow
- [x] Real CC0 3D models from Khronos glTF sample library (not placeholder cubes)
- [x] Crystal collection puzzles (per-level tracking, particle feedback)
- [x] Push-block puzzles (grid-based movement, pressure plates, state tracking)
- [x] Timed platforms (cycling on/off with visual feedback)
- [x] Boss fights (projectile dodging, hit-point tracking, attack patterns)
- [x] Gamepad support with dead-zone handling and dynamic HUD (icons swap keyboard/gamepad)
- [x] Fog rendering system (ffe.setFog / ffe.disableFog, linear depth fog in fragment shaders)
- [x] Editor ImGui key migration fix (GLFW key codes to ImGuiKey enum)

### Architecture Notes
- Fog system integrated into the forward renderer at LEGACY tier (OpenGL 3.3)
- Gamepad dead-zones configurable per-axis, applied before action mapping
- Showcase game runs entirely in Lua — validates the scripting API completeness for 3D games

---

## Phase 7: Rendering Pipeline Modernization

**Goal:** Upgrade the renderer from basic Blinn-Phong to a modern PBR pipeline with post-processing, while keeping LEGACY tier support for non-PBR paths.

### Milestones

#### M1: PBR Materials
- [ ] Metallic-roughness workflow (albedo, metallic, roughness, AO, emissive maps)
- [ ] Cook-Torrance BRDF (GGX normal distribution, Smith geometry, Fresnel-Schlick)
- [ ] Image-Based Lighting (IBL) — irradiance and prefiltered environment maps
- [ ] Lua bindings for PBR material properties
- [ ] Fallback to Blinn-Phong on LEGACY tier

#### M2: Post-Processing Pipeline
- [ ] Framebuffer-based post-processing stack (render to FBO, apply fullscreen passes)
- [ ] Bloom (bright-pass extraction, Gaussian blur, additive blend)
- [ ] Tone mapping (Reinhard, ACES, configurable from Lua)
- [ ] Gamma correction (linear workflow internally, sRGB output)

#### M3: GPU Instancing
- [ ] Instanced draw calls for repeated meshes (trees, rocks, particles)
- [ ] Instance buffer management (per-instance transforms, colors)
- [ ] Automatic batching of identical mesh+material pairs

#### M4: Skeletal Animation Completion
- [ ] Animation blending (crossfade between clips, layered blending)
- [ ] Animation state machine (states, transitions, conditions)
- [ ] Lua API for state machine control
- [ ] Root motion extraction

#### M5: Anti-Aliasing
- [ ] MSAA (multisample anti-aliasing, configurable 2x/4x/8x)
- [ ] FXAA post-process pass (LEGACY-friendly, low cost)
- [ ] Configurable from Lua and editor settings

#### M6: SSAO (STANDARD+ Tier)
- [ ] Screen-Space Ambient Occlusion (hemisphere sampling, blur pass)
- [ ] STANDARD tier minimum (requires depth buffer access and multiple passes)
- [ ] Disabled on LEGACY/RETRO tiers — no silent degradation

#### M7: Sprite Batching 2.0
- [ ] Texture atlas auto-packing (runtime atlas generation for 2D sprites)
- [ ] Reduced draw calls for multi-texture 2D scenes
- [ ] Backward-compatible with existing sprite API

#### M8: Phase Close + Showcase Update
- [ ] Update "Echoes of the Ancients" to use PBR materials and post-processing
- [ ] Performance validation on LEGACY tier (non-PBR path stays 60fps)
- [ ] Full build verification (Clang-18 + GCC-13)
- [ ] Documentation and .context.md updates for all new systems

### Architecture Constraints
- PBR and post-processing are STANDARD+ by default; LEGACY falls back to Blinn-Phong
- Post-processing stack must be composable — developers enable/disable passes individually
- No new dependencies without explicit approval (shader-only implementations preferred)
- All new rendering features must declare their tier in .context.md

---

## Phase 8: Vulkan Backend

**Goal:** Add a Vulkan rendering backend alongside OpenGL, targeting STANDARD and MODERN tiers for significantly improved draw call throughput and GPU utilization.

### Deliverables (planned)
- [ ] Vulkan RHI implementation behind existing abstraction layer
- [ ] Runtime backend selection (OpenGL or Vulkan)
- [ ] Validation layer integration for development builds
- [ ] Vulkan-specific optimizations (pipeline caching, descriptor sets)
- [ ] MODERN tier features (compute shaders, ray tracing hooks)

---

## Phase 9: Terrain and Open World

**Goal:** Large-scale outdoor environment support with terrain rendering, LOD, and streaming.

### Deliverables (planned)
- [ ] Heightmap terrain rendering (chunked, LOD)
- [ ] Terrain texturing (splat maps, triplanar projection)
- [ ] Vegetation system (grass, trees with billboarding)
- [ ] World streaming (load/unload chunks based on camera position)
- [ ] Water rendering (reflections, refractions on STANDARD+ tier)

---

## Phase 10: Advanced Editor

**Goal:** Bring the editor to feature parity with commercial engines for common workflows.

### Deliverables (planned)
- [ ] Visual scripting (node-based graph editor as alternative to Lua)
- [ ] Prefab system (reusable entity templates with overrides)
- [ ] LLM integration panel (connect AI assistant, generate code, explain systems)
- [ ] Editor preferences and project wizard
- [ ] Installer / easy setup wizard (install FFE, connect AI model, start making games without build complexity)
- [ ] Animation editor (timeline, keyframes, state machine visualization)

---

## Phase 11: Cross-Platform Native Builds

**Goal:** Native compilation on all major platforms without cross-compilation.

### Deliverables (planned)
- [ ] MSVC build support (Visual Studio 2022+ on Windows)
- [ ] Xcode build support (native macOS and iOS)
- [ ] AppImage packaging for Linux distribution
- [ ] Mobile targets (Android NDK, iOS — stretch goal)
- [ ] CI pipelines for all native platforms

---

## Phase 12: Asset Pipeline and Plugin System

**Goal:** A robust asset import/export pipeline and a plugin architecture for extending the engine.

### Deliverables (planned)
- [ ] Asset import pipeline (model conversion, texture compression, audio transcoding)
- [ ] Asset caching and hot-reload (detect changes, reimport automatically)
- [ ] Plugin API (C++ shared libraries, versioned ABI)
- [ ] Lua plugin distribution (package format, dependency resolution)
- [ ] Asset store integration (browse, download, import community assets)

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
