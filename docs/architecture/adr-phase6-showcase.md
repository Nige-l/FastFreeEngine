# ADR: Phase 6 — Showcase & Polish ("Echoes of the Ancients")

**Status:** PROPOSED
**Author:** architect
**Date:** 2026-03-07
**Tiers:** LEGACY (primary) — the showcase must run at 60 fps on OpenGL 3.3 / 1 GB VRAM
**Security Review Required:** NO — no new attack surface (Lua sandbox, asset loading, and save/load are already hardened)

---

## 1. Purpose

All five roadmap phases are complete. The engine has 991 tests, ~152 Lua bindings, 3D rendering with Blinn-Phong lighting, shadows, skybox, Jolt physics, skeletal animation, spatial audio, particles, save/load, and a standalone editor. But the best existing demo is spinning cubes with falling physics objects. That is not what goes on a README.

Phase 6 delivers a **multi-level 3D showcase game** that:

1. Proves FFE can ship a real, playable 3D game — not a tech demo
2. Produces screenshots worthy of the project README and website
3. Exercises every major engine subsystem in a single cohesive experience
4. Serves as a reference implementation for developers learning the engine
5. Stress-tests the Lua scripting layer at game-scale complexity

The game must be impressive enough that someone seeing a screenshot thinks "that engine can make real games" and simple enough that we can build it with freely available CC0/MIT assets and Lua scripting.

---

## 2. Game Concept: "Echoes of the Ancients"

### 2.1 Elevator Pitch

A **third-person action-exploration game** across 3 distinct levels. The player navigates ancient ruins, solves physics-based puzzles, collects artifacts, and defeats guardians to progress. Think a miniature Zelda dungeon crawl — exploration, light combat, environmental puzzles, and atmosphere.

### 2.2 Why This Concept

| Requirement | How This Concept Delivers |
|---|---|
| Visually impressive screenshots | Atmospheric ruins with dramatic lighting, shadows, skybox, particles (dust, fire, sparks) |
| Multi-level | 3 distinct environments: outdoor courtyard, underground temple, tower summit |
| Exercises 3D renderer | Mesh rendering, Blinn-Phong, point lights, directional shadows, skybox, materials (specular, normal maps) |
| Exercises physics | Pushable blocks, falling platforms, physics projectiles, rigid body guardians, raycasting for line-of-sight |
| Exercises audio | Spatial audio for ambient sounds (wind, water, fire), positional SFX (footsteps, impacts), music per level |
| Exercises scripting | All game logic in Lua — entity management, state machines, puzzle logic, AI, HUD, scene transitions |
| Exercises save/load | Checkpoint system using `ffe.saveData` / `ffe.loadData` |
| Exercises particles | Torch fire, dust motes, impact sparks, guardian death effects, artifact glow |
| Exercises camera | Orbit camera with collision avoidance (raycast behind player to prevent wall clipping) |
| Exercises scene serialization | Each level is a scene JSON loaded with `ffe.loadScene` |
| Runs on LEGACY tier | Low-poly assets, limited draw calls, no features beyond OpenGL 3.3 |

### 2.3 Core Mechanics

**Movement:** Third-person orbit camera. WASD movement relative to camera facing. Space to jump (physics impulse). Gamepad supported.

**Combat:** Simple melee attack (raycast + collision check in a cone in front of the player). Guardians have health, patrol waypoints, and chase-on-sight AI. No complex animation blending needed — mesh color flash on hit is sufficient.

**Puzzles:** Physics-based. Push stone blocks onto pressure plates. Shoot projectiles to hit targets. Timed sequences (cross a bridge before it collapses). These all use existing Jolt physics + raycasting + timers.

**Progression:** Collect 1 artifact per level to unlock the exit portal. 3 levels, each ~3-5 minutes of gameplay. Total playtime: 10-15 minutes.

**HUD:** Health bar, artifact count, interaction prompts, minimap (2D overlay using `ffe.drawRect`). All rendered with existing `drawText` / `drawRect` / `drawFontText` APIs.

---

## 3. Level Design

### 3.1 Level 1 — "The Courtyard" (Outdoor)

**Setting:** An overgrown stone courtyard with crumbling walls, archways, and a central fountain.

**Atmosphere:** Warm golden-hour directional light, blue-grey ambient fill, skybox with clouds. Particles: floating dust motes, torch fire on wall sconces.

**Puzzle:** Push two stone blocks onto pressure plates to open the gate. Teaches the player the push mechanic.

**Enemies:** 2 stone guardian statues that activate when the player approaches. Simple patrol + chase AI.

**Artifact:** Hidden behind a destructible wall (hit with 3 melee attacks).

**Engine features exercised:** Skybox, directional shadows, point lights (torches), physics (pushable blocks, pressure plates via collision callbacks), particles (fire, dust), spatial audio (fountain water, wind).

### 3.2 Level 2 — "The Temple" (Underground)

**Setting:** A dark underground temple with pillars, a lava pit, narrow bridges, and glowing crystals.

**Atmosphere:** No skybox (dark void or dark solid color). Minimal ambient light. Point lights from crystals and lava glow. Dramatic shadow contrast.

**Puzzle:** Timed bridge crossing (platforms fall 2 seconds after stepping on them — use `ffe.after` timers + physics). Light 4 crystal pedestals by pushing reflector blocks into correct positions (line-of-sight via raycasting).

**Enemies:** 3 guardians, faster than Level 1. One "boss" guardian that is larger and takes more hits.

**Artifact:** Revealed when all 4 crystals are lit.

**Engine features exercised:** Dark lighting (point-light-dominant scene), more physics interactions, raycasting for puzzle mechanics, timer system, more particle effects (lava glow, crystal sparkle), tenser audio atmosphere.

### 3.3 Level 3 — "The Summit" (Tower Top)

**Setting:** An open-air tower summit above the clouds. Floating stone platforms. A dramatic skybox (sunset/dawn).

**Atmosphere:** Vivid skybox, strong directional light from low angle (long shadows), wind particle effects, volumetric feel from dust/cloud particles.

**Puzzle:** Platforming across floating islands. Some platforms orbit on paths (Lua-driven sine/cosine movement like the existing 3D demo). A final puzzle combining block-pushing and timed jumping.

**Enemies:** 4 guardians on separate platforms. The player must defeat all to activate the final artifact pedestal.

**Artifact:** The final artifact triggers a "victory" sequence — camera pulls back, particles burst, congratulations HUD.

**Engine features exercised:** Large skybox showcase, dramatic shadow angles, moving platforms (scripted transform updates), particle burst effects, camera animation, full HUD for victory screen.

### 3.4 Level Definition Format

Each level is defined by two files:

1. **Scene JSON** — entity transforms, mesh assignments, material properties, physics body definitions. Created in the FFE editor or hand-authored. Loaded via `ffe.loadScene("levels/level1.json")`.
2. **Level Lua script** — game logic: enemy AI, puzzle state machines, triggers, HUD, music, transitions. Loaded as the game script. References entities by tag (stored in scene JSON metadata or by convention via entity ordering).

A master `game.lua` handles the main menu, level sequencing, save/load, and global state. It calls `ffe.loadScene` to swap levels and delegates per-level logic to level-specific Lua modules.

**Entity tagging strategy:** Since the ECS does not have a built-in tag/name system, we use a convention: a Lua table maps string names to entity IDs, populated at scene load time by iterating entities and matching known positions or by using a deterministic spawn order documented per level. This is pure Lua — no engine changes needed.

---

## 4. Engine Feature Gap Analysis

### 4.1 Fully Covered by Existing Systems (No Changes)

| Game Requirement | Engine Subsystem | Binding |
|---|---|---|
| 3D mesh rendering | mesh_renderer | `ffe.loadMesh`, `ffe.createEntity3D`, `ffe.setTransform3D` |
| Materials (color, specular) | mesh_renderer | `ffe.setMeshColor`, `ffe.setMeshSpecular`, `ffe.setMeshTexture` |
| Normal maps | mesh_renderer | `ffe.setMeshNormalMap` |
| Directional light + shadows | shadow_map | `ffe.enableShadows`, `ffe.setShadowBias`, `ffe.setShadowArea` |
| Point lights (torches, crystals) | mesh_renderer | `ffe.addPointLight` (4 max) |
| Skybox | skybox | `ffe.loadSkybox`, `ffe.setSkyboxEnabled` |
| Physics rigid bodies | Jolt wrapper | `ffe.createPhysicsBody` (static, dynamic, box/sphere) |
| Physics impulse/force | Jolt wrapper | `ffe.applyImpulse`, `ffe.applyForce` |
| Collision callbacks | Jolt wrapper | `ffe.onCollision3D` (enter/persist/exit) |
| Raycasting | Jolt wrapper | `ffe.castRay`, `ffe.castRayAll` |
| Velocity control | Jolt wrapper | `ffe.setLinearVelocity`, `ffe.getLinearVelocity` |
| Gravity control | Jolt wrapper | `ffe.setGravity` |
| Orbit camera | camera | `ffe.set3DCameraOrbit` |
| FPS camera | camera | `ffe.set3DCameraFPS` |
| Particle effects | particles | `ffe.addEmitter`, `ffe.setEmitterConfig`, `ffe.emitBurst` |
| Spatial audio | audio | `ffe.playSound3D`, `ffe.setListenerPosition` |
| Music playback | audio | `ffe.playMusic`, `ffe.stopMusic`, `ffe.setMusicVolume` |
| Sound effects | audio | `ffe.playSound`, `ffe.loadSound` |
| Timers | timers | `ffe.after`, `ffe.every`, `ffe.cancelTimer` |
| Save/load game state | save/load | `ffe.saveData`, `ffe.loadData` |
| Scene transitions | scene management | `ffe.loadScene`, `ffe.destroyAllEntities`, `ffe.cancelAllTimers` |
| HUD rendering | text/drawing | `ffe.drawText`, `ffe.drawRect`, `ffe.drawFontText` |
| TTF fonts | text | `ffe.loadFont`, `ffe.drawFontText`, `ffe.measureText` |
| Input (keyboard + gamepad) | input | `ffe.isKeyHeld/Pressed`, `ffe.isGamepadButtonHeld`, `ffe.getGamepadAxis` |
| Screenshot | screenshot | `ffe.screenshot` |
| Camera shake | camera | `ffe.cameraShake` |
| Skeletal animation | animation_system | `ffe.playAnimation3D`, `ffe.stopAnimation3D` |

### 4.2 Gaps Requiring Engine Work

| Gap | Severity | Effort | Description |
|---|---|---|---|
| **Entity tagging / naming** | LOW | 0 sessions | Pure Lua convention — no engine change. Use a Lua table mapping names to entity IDs. |
| **3D text / billboard text** | LOW | 0 sessions | Use existing 2D `drawFontText` for all HUD. No world-space text needed for the showcase. |
| **Fog / distance fade** | NICE-TO-HAVE | 1 session | Linear fog in the fragment shader would improve atmosphere, especially for the underground level. A uniform for fog color + start/end distance, applied in the Blinn-Phong shader. Simple and LEGACY-safe. **Recommended but not blocking.** |
| **Multiple mesh loading** | LOW | 0 sessions | `ffe.loadMesh` already supports loading multiple .glb files. Each returns a handle. No gap. |
| **Trigger volumes (non-physical)** | LOW | 0 sessions | Use Jolt "sensor" bodies (kinematic + collision callback with no physics response). Already supported via `createPhysicsBody` with `motion = "kinematic"`. |

### 4.3 Recommended Engine Enhancement: Linear Fog

**What:** Add a `ffe.setFog(r, g, b, nearDist, farDist)` / `ffe.disableFog()` binding. In the MESH_BLINN_PHONG fragment shader, mix the fragment color toward the fog color based on fragment distance from the camera. This is a ~20-line shader change, a uniform upload, and a Lua binding.

**Why:** Fog hides the far-clip pop-in, adds atmosphere to the underground level, and makes the outdoor levels feel more expansive. It is the single highest-impact visual improvement for the effort.

**Tier:** LEGACY-safe. Linear fog is OpenGL 1.0-era math.

**Effort:** Half a session for renderer-specialist (shader change + uniform) + engine-dev (Lua binding + test).

**Verdict:** Implement fog in the first showcase session as a foundation feature before level work begins.

---

## 5. Asset Requirements

All assets must be MIT, CC0, or public domain. No exceptions.

### 5.1 3D Models (.glb)

| Asset | Source Strategy | Notes |
|---|---|---|
| Stone block / cube | Kenney "Prototype Textures" pack (CC0) applied to primitives, or procedural cube (already have `cube.glb`) | Recolor/rescale for walls, floors, pillars, platforms |
| Player character | Kenney "Animated Characters" (CC0) or Quaternius low-poly characters (CC0) | Needs idle + walk animations if skeletal; otherwise a simple capsule/knight mesh |
| Guardian enemy | Same source as player, different mesh/color | Recolor to distinguish from player |
| Artifact (collectible) | Simple geometric shape — dodecahedron or crystal, available on Kenney or procedurally generated | Floating + rotating via Lua script |
| Torch / sconce | Low-poly cylinder + cone from Kenney "Furniture" or similar CC0 pack | Static mesh, particle emitter for flame |
| Archway / gate | Kenney "Castle Kit" (CC0) or modular dungeon kits from Quaternius | |
| Bridge / platform | Flat slab mesh — cube.glb scaled flat | Same as existing ground plane approach |
| Fountain | Kenney "Medieval" or simple stacked cylinders | Central courtyard feature |
| Crystal | Procedural or CC0 gem model | Point light positioned inside for glow effect |

**Key principle:** Reuse a small set of meshes (8-12 total) with different scales, colors, and materials. Low-poly aesthetic is both a style choice and a LEGACY-tier performance choice. Fewer unique meshes = fewer draw calls = better performance on old GPUs.

**Mesh budget per level:** Target ~50-80 entities with meshes. At current batching, this is well within LEGACY-tier 60fps budget.

### 5.2 Textures

| Texture | Source | Notes |
|---|---|---|
| Stone / brick | Kenney "Prototype Textures" (CC0) or ambientCG (CC0) | Diffuse + normal map for walls and blocks |
| Ground / grass | ambientCG (CC0) | Outdoor courtyard floor |
| Lava | Procedural (UV scroll in Lua by swapping texture coords) or CC0 texture | Underground level pit |
| Crystal | Solid color with specular, no texture needed | |
| Skybox (day) | Kenney "Skybox Pack" (CC0) or OpenGameArt CC0 | Courtyard + Summit levels |
| Skybox (night/void) | Solid dark color or CC0 star field | Underground level |
| Player / enemy | Vertex-colored or single-color diffuse | Low-poly look, no complex UV unwrap needed |

**Texture budget:** Max 2048x2048 per texture. Most will be 512x512 or 1024x1024. Total VRAM for textures: estimated <100 MB, well within the 1 GB LEGACY minimum.

### 5.3 Audio

| Sound | Source | Notes |
|---|---|---|
| Background music (3 tracks) | OpenGameArt CC0 or Kevin MacLeod (CC-BY) | One per level — atmospheric, loopable |
| Footstep SFX | Kenney "Impact Sounds" (CC0) or freesound.org CC0 | 2-3 variants, randomized |
| Melee attack SFX | CC0 impact/whoosh | |
| Guardian hit / death SFX | CC0 stone crumble / shatter | |
| Ambient wind | CC0 wind loop | Outdoor levels |
| Ambient water | CC0 water/fountain loop | Courtyard fountain, spatial audio |
| Ambient fire | CC0 fire crackle | Torches, spatial audio |
| Artifact collect jingle | CC0 chime / pickup sound | |
| Door / gate open | CC0 stone grinding | Puzzle completion |
| Platform crumble | CC0 stone crack | Timed platforms, Level 2 |
| Victory fanfare | CC0 short triumphant tune | End of Level 3 |

### 5.4 Fonts

One TTF font for the HUD. **Kenney "Kenney Fonts"** (CC0) or **Google Fonts** (SIL Open Font License). Load via `ffe.loadFont`.

---

## 6. Agent Team Assessment

### 6.1 Do We Need New Agent Roles?

**No.** The existing agent team can deliver this showcase. Here is the reasoning:

| Proposed Role | Verdict | Rationale |
|---|---|---|
| Game Designer | NOT NEEDED | The game design is specified in this ADR. Level layouts, puzzle logic, and enemy behavior are data (Lua tables + scene JSON), not complex design requiring a specialist. PM + architect define the design; engine-dev implements it. |
| Writer / Narrative Designer | NOT NEEDED | The game has minimal narrative — collect artifacts, defeat guardians, reach the summit. Flavor text (if any) is a few strings in Lua. Not enough work to justify a specialist. |
| 3D Artist | NOT NEEDED | All 3D models come from CC0 asset packs (Kenney, Quaternius, etc.) or are procedural. No custom modeling is needed. Asset curation is part of engine-dev's task. |
| Sound Designer | NOT NEEDED | All audio comes from CC0 libraries. Asset selection and integration is engine-dev's task. |
| Level Designer Agent | NOT NEEDED | Levels are defined as Lua tables + scene JSON. The entity placement is straightforward (grid-aligned blocks and platforms). engine-dev writes the level data as part of the Lua implementation. |

### 6.2 Agent Assignments for Phase 6

| Agent | Role in Phase 6 |
|---|---|
| `architect` | This ADR. Review level designs if complexity escalates. |
| `engine-dev` | All Lua game code, level data, asset integration, entity setup, game logic, HUD, save/load. This is the primary worker. |
| `renderer-specialist` | Fog shader implementation (if approved). Any renderer-side issues. |
| `api-designer` | Update `.context.md` files if new patterns emerge. Review the showcase as a documentation reference. |
| `performance-critic` | Review each milestone for LEGACY-tier frame budget compliance. Especially important for Level 2 (many point lights in dark scene). |
| `security-auditor` | No new attack surface — skip unless asset loading paths raise concerns. |
| `build-engineer` | Standard Phase 5 build+test per session. |
| `game-dev-tester` | Validate the final game is playable, controls are discoverable, no softlocks. This is a good use of game-dev-tester — the showcase IS a game. |
| `project-manager` | Session planning, commits, devlog. |

---

## 7. Session Estimate

**Total: 8-10 sessions.**

The showcase is predominantly Lua code (game logic) plus asset curation. No major engine C++ work is needed. Each session delivers a playable increment.

---

## 8. Phase 6 Roadmap

### Milestone 1 — Foundation (Session 66) — 1 session

**Deliverables:**
- Linear fog in Blinn-Phong shader (`ffe.setFog` / `ffe.disableFog`) + Lua binding + tests
- Showcase project scaffold: `examples/showcase/` directory structure
  - `examples/showcase/game.lua` — main menu + level sequencing
  - `examples/showcase/lib/` — shared Lua modules (player controller, camera, combat, HUD, AI)
  - `examples/showcase/levels/` — per-level Lua scripts + scene JSON
  - `examples/showcase/assets/` — models, textures, sounds
- Asset acquisition: download and integrate CC0 asset packs (meshes, textures, skybox, audio)
- Player controller prototype: WASD movement, jump, orbit camera, basic collision with ground

**Why first:** Fog is the only engine change. Get it merged early so all subsequent sessions are pure Lua content work with no engine risk.

### Milestone 2 — Level 1: The Courtyard (Sessions 67-68) — 2 sessions

**Session 67:**
- Level 1 scene: courtyard geometry (walls, floor, archways, fountain, torch sconces)
- Lighting setup: directional light (golden hour), shadows, 4 point lights (torches)
- Skybox (daytime clouds)
- Particle effects: torch fire emitters, ambient dust motes
- Spatial audio: fountain water loop, wind ambient, torch crackle
- Player spawns and can walk around the environment

**Session 68:**
- Level 1 gameplay: pushable block puzzle (2 blocks, 2 pressure plates, 1 gate)
- Guardian AI: 2 enemies with patrol waypoints, chase-on-sight (raycast LOS), melee attack
- Player combat: melee attack raycast, damage dealing, health system
- Artifact placement + collection
- Level completion: gate opens, portal to Level 2, scene transition
- HUD: health bar, artifact indicator, interaction prompts
- Save checkpoint on level entry

### Milestone 3 — Level 2: The Temple (Sessions 69-70) — 2 sessions

**Session 69:**
- Level 2 scene: underground temple geometry (pillars, bridges, lava pit, crystal pedestals)
- Dark lighting: minimal ambient, point lights from crystals (colored glow), lava glow
- Fog enabled (dark red/brown, short range for oppressive atmosphere)
- Particles: lava bubbles, crystal sparkle
- Audio: dark ambient track, dripping water, rumble

**Session 70:**
- Level 2 gameplay: timed collapsing bridges, crystal puzzle (push reflector blocks, raycast validation)
- 3 guardians + 1 boss guardian (larger, more HP, telegraphed attacks)
- Artifact reveal on puzzle completion
- Portal to Level 3
- Difficulty tuning: enemy speed, platform timing

### Milestone 4 — Level 3: The Summit (Sessions 71-72) — 2 sessions

**Session 71:**
- Level 3 scene: tower summit geometry (floating platforms, stone pillars, open sky)
- Dramatic skybox (sunset/dawn), long shadows from low-angle directional light
- Moving platforms (Lua sine/cosine driven, like existing 3D demo orbit pattern)
- Wind particles, cloud wisps
- Audio: soaring ambient music, wind

**Session 72:**
- Level 3 gameplay: platforming across floating islands, 4 guardians on separate platforms
- Final combined puzzle (block push + timed jump)
- Final artifact + victory sequence (camera pullback, particle burst, victory HUD)
- Portal returns to main menu
- Full save/load integration tested across all 3 levels

### Milestone 5 — Polish & Screenshots (Sessions 73-74) — 2 sessions

**Session 73:**
- Main menu: title screen with background scene, "New Game" / "Continue" / "Quit"
- Game-over / retry flow
- Gamepad support pass (all levels playable with gamepad)
- Audio balancing (volume levels, spatial attenuation tuning)
- Particle tuning (density, lifetime, colors)
- Performance profiling on LEGACY-tier budget (target: 60fps with all 3 levels)

**Session 74:**
- Screenshot capture session: set up camera angles for README-worthy shots
- README update with showcase screenshots
- Website showcase page update
- Final game-dev-tester playthrough: verify no softlocks, all puzzles solvable, controls discoverable
- FULL build (Clang-18 + GCC-13) for phase closure

### Optional Milestone 6 — Stretch Goals (Session 75) — if time permits

- Skeletal animation for player/guardian walk cycles (if suitable CC0 animated models exist)
- Minimap (2D overlay rendered with `drawRect`)
- Time-of-day cycle (slowly rotate directional light + change skybox tint)
- Leaderboard (fastest completion time, saved locally)

---

## 9. Performance Budget

### Per-Level Targets (LEGACY Tier, 60fps)

| Metric | Budget | Rationale |
|---|---|---|
| Mesh entities | ≤80 per level | Each is a draw call in the current non-instanced renderer |
| Point lights | ≤4 (engine max) | Already the hardware limit we designed for |
| Particle emitters | ≤16 active | 128 particles each = 2048 max particles, well within budget |
| Shadow map resolution | 1024x1024 | Already proven in 3D demo |
| Textures in VRAM | <100 MB | Conservative for 1 GB minimum |
| Lua update() cost | <2ms per frame | Instruction budget already capped at 1M ops |
| Physics bodies | ≤40 per level | Jolt handles this trivially |

### Key Risk: Level 2 (Dark Scene)

The underground level is point-light-dominant. With 4 point lights and minimal ambient, every fragment runs the full 4-light loop in the Blinn-Phong shader. This is the most GPU-intensive scenario. Mitigation: keep geometry count lower in Level 2 (fewer entities, rely on darkness to hide the reduced detail) and profile early.

---

## 10. What This Prevents Us From Doing Later

Nothing. This is a game built ON the engine, not a change TO the engine. The only engine change (fog) is additive and backwards-compatible. The showcase lives in `examples/showcase/` and has zero coupling to engine internals beyond the public Lua API.

If anything, this showcase will **reveal** API gaps and ergonomic issues that inform future engine work. That is one of its primary values.

---

## 11. Verdict

**APPROVED — proceed with Phase 6.**

The concept is achievable with current engine capabilities plus one small renderer enhancement (fog). All assets are freely available. No new agent roles are needed. The 8-10 session estimate is realistic given that the work is predominantly Lua scripting with no engine architecture changes.

The showcase will transform FFE's public image from "an engine with spinning cubes" to "an engine that ships games." That is worth the investment.
