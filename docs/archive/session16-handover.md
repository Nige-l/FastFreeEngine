# Session 16 Handover — Demo Game Showcase

## Context

All major engine subsystems are now implemented and tested (341 tests, zero warnings, both compilers). The user wants something to show people next week — a playable demo game is the best showcase. Session 16 is about proving the engine works end-to-end by building a real game on top of it.

## Subsystems Available

| Subsystem | Status | Key APIs |
|-----------|--------|----------|
| ECS | Stable | `ffe.createEntity()`, `ffe.destroyEntity()` |
| Rendering | Stable | Sprite batching, UV passthrough, spritesheet support |
| Input | Stable | `ffe.isKeyHeld()`, `ffe.isKeyPressed()`, `KEY_*` constants |
| Sprite Animation | Stable | `ffe.addSpriteAnimation()`, `ffe.playAnimation()` |
| Audio | Stable | `ffe.loadSound()`, `ffe.playSound()`, streaming music |
| 2D Collision | Stable | `ffe.addCollider()`, `ffe.setCollisionCallback()` |
| Editor Overlay | Stable | F1 toggle, entity inspector, perf panel (Dear ImGui) |
| Lua Scripting | Stable | `init()`, `update(dt)`, `shutdown()`, `ffe.*` bindings |
| Transform | Stable | `ffe.getTransform()`, `ffe.setTransform()`, `ffe.fillTransform()` |

## Session 16 Plan

### P0: Collect-the-Items Demo Game

Build a complete mini-game in Lua that exercises every subsystem:

**Game Design:**
- Player character controlled with WASD, rendered as a sprite
- Collectible items scattered on screen (animated sprites using spritesheet)
- Collision detection triggers pickup (item destroyed, score incremented)
- SFX plays on each pickup (`sfx_beep.wav`)
- Background music loops (`music_loop.ogg`)
- Score displayed via editor overlay or on-screen text
- ESC to quit (`ffe.requestShutdown()`)
- All game logic in a single `game.lua` file

**Subsystem Coverage Checklist:**
- [ ] ECS — createEntity/destroyEntity for player and collectibles
- [ ] Rendering — sprites for player and items
- [ ] Input — WASD movement via isKeyHeld
- [ ] Sprite Animation — animated collectibles using spritesheet
- [ ] Audio — SFX on pickup, background music
- [ ] 2D Collision — AABB or circle colliders on player and items
- [ ] Collision Callback — Lua callback triggers pickup logic
- [ ] Transform — setTransform for movement, getTransform for positions
- [ ] Editor Overlay — F1 to inspect entities and see perf stats
- [ ] Scripting — init/update/shutdown lifecycle, all logic in Lua

**Agent Assignments:**
1. `game-dev-tester` — writes the demo game Lua script and reports any API friction
2. `engine-dev` — fixes any integration issues found during demo development
3. `api-designer` — reviews any API gaps or friction points reported
4. `test-engineer` — ensures existing tests still pass after any fixes

### P1: deliverCollisionEvents Integration Verification

Verify that `Application::tick()` correctly calls `deliverCollisionEvents()` after systems run. This was implemented in Session 15 but should be tested end-to-end with the demo game. If the collision callback does not fire in the demo, debug and fix the integration.

### P2: Demo Polish

- Ensure the demo runs smoothly at 60 fps on LEGACY tier
- Add a simple spawn/respawn mechanic for collectibles so the game doesn't end
- Make sure the demo is a good visual showcase (colors, movement feel, audio timing)
- Update README.md with instructions for running the demo

## Files of Interest

| File | Purpose |
|------|---------|
| `engine/core/application.cpp` | tick() loop — verify deliverCollisionEvents() call |
| `engine/physics/collision_system.h` | collisionSystem registration |
| `engine/physics/collider2d.h` | Collider2D component definition |
| `engine/physics/narrow_phase.h` | AABB/Circle intersection tests |
| `engine/scripting/script_engine.h` | deliverCollisionEvents(), all Lua bindings |
| `engine/scripting/.context.md` | Full Lua API reference for game-dev-tester |
| `engine/physics/.context.md` | Physics API reference |
| `assets/textures/spritesheet.png` | 128x64, 8 frames for animation |
| `assets/textures/white.png` | Solid white texture for colored sprites |
| `assets/audio/sfx_beep.wav` | Pickup SFX |
| `assets/audio/music_loop.ogg` | Background music |

## Known Issues to Watch

- **Asset root:** Currently `assets/textures/` — audio files may need copies there or a unified root
- **Shape string match:** `addCollider` uses string comparison for shape type ("aabb"/"circle") — works but slightly fragile
- **Score display:** No text rendering system exists; use editor overlay (ImGui) or Lua print to console as fallback
- **getTransform GC pressure (M-1):** Use `fillTransform()` in the update loop to avoid per-frame table allocation

## Success Criteria

Session 16 is successful when:
1. A playable demo game runs showing player movement, collectible pickups, collision detection, animation, audio, and score tracking
2. All game logic lives in Lua — no C++ changes needed for game mechanics
3. All 341+ existing tests still pass
4. The demo is something the user can show people as a proof of the engine working
