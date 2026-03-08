# FFE Examples Map

Quick reference for demo/example games. All files under `examples/` are owned by the Implementer agent.

---

## Directory Structure

```
examples/
  lua_demo/          game.lua, main.cpp, CMakeLists.txt
  pong/              pong.lua, main.cpp, CMakeLists.txt
  breakout/          breakout.lua, main.cpp, CMakeLists.txt
  3d_demo/           game.lua, main.cpp, CMakeLists.txt
  net_demo/          net_arena.lua, main.cpp, CMakeLists.txt, README.md
  showcase/          game.lua, main.cpp, CMakeLists.txt, ASSETS.md
    levels/          level1.lua, level2.lua, level3.lua, test_level.lua
    lib/             player.lua, camera.lua, hud.lua, combat.lua, ai.lua, menus.lua
    assets/          models/ (cube.glb, fox.glb, duck.glb, damaged_helmet.glb,
                              cesium_man.glb, rigged_figure.glb, rigged_simple.glb,
                              box_vertex_colors.glb)
                     audio/ (sfx_collect.wav, sfx_hit.wav, sfx_gate.wav,
                             music_pixelcrown.ogg, BattleMusic.mp3)
                     terrain/ (courtyard_height.png, summit_height.png)
                     fonts/ (PressStart2P.ttf)
    tools/           generate_heightmaps.py
  hello_sprites/     main.cpp, CMakeLists.txt   [C++ only, no Lua]
  headless_test/     main.cpp, CMakeLists.txt   [C++ only, no Lua]
  interactive_demo/  main.cpp, CMakeLists.txt   [C++ only, no Lua]
  runtime/           main.cpp, CMakeLists.txt   [ffe_runtime generic Lua runner]
  demo_paths.h       shared asset path constants
```

---

## Per-Demo Summary

| Demo | Binary | Entry point | Key ffe.* bindings | Screenshot name |
|------|--------|-------------|-------------------|-----------------|
| collect_stars | `ffe_lua_demo` | `lua_demo/game.lua` | createEntity, setTexture, setPosition, onCollision, loadSound, playSound, loadMusic, playMusic, drawText, drawRect, isKeyHeld | collect_stars |
| pong | `ffe_pong` | `pong/pong.lua` | createEntity, setTexture, setPosition, getPosition, drawText, drawRect, isKeyHeld, loadSound, playSound, loadMusic | pong |
| breakout | `ffe_breakout` | `breakout/breakout.lua` | createEntity, destroyEntity, setPosition, getPosition, drawText, drawRect, isKeyHeld, loadSound, playSound, getScreenWidth/Height | breakout |
| 3d_demo | `ffe_3d_demo` | `3d_demo/game.lua` | loadMesh, createEntity3D, setTransform3D, setMeshColor, setMeshSpecular, set3DCameraOrbit, setLightDirection, setLightColor, setAmbientColor, addPointLight, setPointLightPosition, removePointLight, createPhysicsBody, onCollision3D, castRay, applyImpulse, drawText, drawRect | 3d_demo |
| net_arena | `ffe_net_demo` | `net_demo/net_arena.lua` | hostServer, connectToServer, disconnectFromServer, sendInput, onPlayerJoin, onPlayerLeave, onGameMessage, sendGameMessage, createEntity, setPosition, destroyEntity, drawText, drawRect | net_arena |
| showcase | `ffe_showcase` (or `ffe_runtime`) | `showcase/game.lua` | See Showcase Deep-Dive below | showcase_menu, showcase_level1, showcase_level2, showcase_level3 |

**Known issues (non-blocking):**
- All demos: `cube.glb` and other models degrade to fallback if assets missing — graceful, logged
- net_arena: requires two processes; automated screenshot shows menu only (server/client not testable headlessly)
- 3d_demo: binary is `ffe_3d_demo`, screenshot uses 4-second wait

---

## Showcase Deep-Dive ("Echoes of the Ancients")

**Entry:** `showcase/game.lua`
**State machine:** MENU -> PLAYING -> PAUSED -> LEVEL_COMPLETE -> GAME_OVER -> VICTORY
**Module loading:** uses `ffe.loadScene()` (not `require`) to load `lib/` modules into shared Lua state
**Modules loaded at startup:** player.lua, camera.lua, hud.lua, combat.lua, ai.lua, menus.lua
**Globals set by modules:** `Player`, `Camera`, `HUD`, `Combat`, `AI`, `Menus`
**Level transition:** `loadLevel(n)` calls `ffe.destroyAllEntities()`, unloads terrain, disables post-processing, removes 8 point lights, unloads skybox, stops music, then `ffe.loadScene(levels/levelN.lua)`
**Victory tracking:** cumulative `totalArtifactsAll` and `totalPlayTime` across all 3 levels
**Controls (menu):** ENTER/Gamepad START/A to start, ESC to quit
**Controls (gameplay):** WASD move, Space/A jump, LMB/F/X attack, E/Y interact, ESC/P pause

### Level 1 — "The Courtyard"

| Property | Value |
|----------|-------|
| Terrain file | `assets/terrain/courtyard_height.png` |
| Theme | Overgrown stone courtyard with crumbling walls, fountain, torches |
| Lighting | Angled sunlight (0.3, -0.8, 0.5), 4 point lights (wall torches) |
| Post-processing | bloom(0.8, 0.3), ACES tone mapping, SSAO, FXAA — skipped on software renderer |
| Shadows | Enabled (not software renderer) |
| Music | `audio/music_pixelcrown.ogg`, volume 0.35 |
| Player spawn | Determined by level script (above terrain, with `ffe.getTerrainHeight`) |
| Enemies | 2 guardian enemies (AI state machine: IDLE/PATROL/CHASE/ATTACK/DEAD) |
| Puzzle | Push-block puzzle |
| Collectibles | 1 artifact (triggers level complete when collected) |
| Meshes used | cube.glb, damaged_helmet.glb, fox.glb, duck.glb, rigged_figure.glb, cesium_man.glb |
| SFX | sfx_collect.wav, sfx_hit.wav, sfx_gate.wav |
| Entity budget | ~55 mesh entities, 4 point lights, ~25 physics bodies |
| Known issues | cube.glb missing = all geometry missing; Fallback to cubeMesh for all character meshes |

### Level 2 — "The Temple"

| Property | Value |
|----------|-------|
| Terrain | None (underground — geometry-only) |
| Theme | Dark underground temple, lava pit, narrow bridges, glowing crystals |
| Lighting | Overhead (0, -1, 0.2), warm orange torch-lit, very dark ambient; 4 point lights |
| Fog | Dark reddish-brown, short range (oppressive underground feel) |
| Post-processing | bloom(1.0, 0.2), ACES, SSAO, FXAA — skipped on software renderer |
| Shadows | Enabled at 512px, bias 0.005, area 35x35 (not software renderer) |
| Music | `audio/music_pixelcrown.ogg`, volume 0.3 |
| Puzzle | Crystal activation sequence: blue->green->purple->cyan |
| Timed platforms | East/west bridges appear/disappear on 3-second cycle |
| Boss | Upgraded guardian at central altar |
| Victory condition | All 4 crystals activated -> portal opens -> walk to portal |
| Meshes used | cube.glb only (all geometry from cube primitives) |
| Entity budget | ~60 mesh entities, 4 point lights, ~33 physics bodies |

### Level 3 — "The Summit"

| Property | Value |
|----------|-------|
| Terrain file | `assets/terrain/summit_height.png` |
| Theme | Open-air tower summit above clouds, floating platforms, sunset |
| Lighting | Low sun (-0.2, -0.6, 0.4), golden hour, 3 point lights |
| Post-processing | bloom(0.8, 0.3), ACES, SSAO, FXAA — skipped on software renderer |
| Music | `audio/BattleMusic.mp3`, volume 0.4 |
| Platforming | Jump across floating stepping stones |
| Moving platforms | 3 platforms oscillating via sine/cosine |
| Enemies | 4 guardians on separate floating platforms (one per quadrant) |
| Victory condition | Collect the final artifact on the central altar |
| Meshes used | cube.glb, fox.glb, damaged_helmet.glb, duck.glb, cesium_man.glb |
| Entity budget | ~60 mesh entities, 3 point lights, ~30 physics bodies |

---

## Lib Module Registry

### `lib/player.lua` — Player controller

**Globals:** `Player` table
**Constants:** `MOVE_SPEED=8.0`, `JUMP_IMPULSE=6.0`, `GROUND_RAY_DIST=1.2`, `MAX_HEALTH=100`, `DAMAGE_COOLDOWN=0.5`, `X_ROT_CORRECTION=-90` (CesiumMan Y-up fix)
**Public API:** `Player.create(x,y,z,cubeMeshHandle)`, `Player.update(dt)`, `Player.getPosition()→x,y,z`, `Player.getEntity()→id`, `Player.takeDamage(amount)`, `Player.getHealth()→n`, `Player.getMaxHealth()→100`, `Player.isAlive()→bool`, `Player.isInteracting()→bool` (E key / Gamepad Y), `Player.cleanup()`
**Bindings used:** createEntity3D, setTransform3D, setMeshColor, setMeshSpecular, createPhysicsBody, fillTransform3D, castRay, getLinearVelocity, setLinearVelocity, applyImpulse, getAnimationCount3D, playAnimation3D, setAnimationSpeed3D, destroyPhysicsBody, destroyEntity, isKeyHeld, isKeyPressed, isMousePressed, isGamepadConnected, getGamepadAxis, isGamepadButtonPressed, cameraShake, after
**Known issues:** Spawn invulnerability = 1.0s (Bug 3 fix). Attack fallback uses F key (isKeyHeld) because cursor capture may not always deliver LMB events correctly.

### `lib/camera.lua` — Third-person orbit camera

**Globals:** `Camera` table
**Constants:** `ORBIT_RADIUS=8.0`, `ORBIT_MIN_RADIUS=3.0`, `ORBIT_MAX_RADIUS=15.0`, `PITCH_MIN=-30`, `PITCH_MAX=60`, `MOUSE_SENSITIVITY=0.17`, `GAMEPAD_SENSITIVITY=120`, `FOLLOW_SPEED=8.0`, `CAMERA_HEIGHT_OFFSET=1.5`
**State:** yaw, pitch, radius, smoothed target position
**Features:** FPS-style cursor-captured mouse look, gamepad right stick fallback, smooth lerp follow, collision avoidance raycast, `FLIP_BOTH=true` for Wayland cursor delta sign fix
**Public API:** `Camera.update(dt)`, `Camera.getYaw()→degrees`
**Bindings used:** getMouseDeltaX/Y, getGamepadAxis, set3DCameraOrbit, castRay, fillTransform3D

### `lib/combat.lua` — Melee attack system

**Globals:** `Combat` table
**Constants:** `ATTACK_RANGE=3.0`, `ATTACK_COOLDOWN=0.5`, `ATTACK_DAMAGE=25`
**Public API:** `Combat.update(dt)`, `Combat.attack(playerPos, playerForward)→hitEntityId|nil`, `Combat.isSwinging()→bool`, `Combat.canAttack()→bool`, `Combat.getAttackDamage()→25`
**Bindings used:** castRay, cameraShake
**Notes:** Calls `AI.isEnemy(hitEntity)` and `AI.damageEnemy(hitEntity, damage)` on hit. `swingTimer=0.15` for HUD crosshair flash.

### `lib/ai.lua` — Guardian AI

**Globals:** `AI` table
**Constants:** `DETECTION_RANGE=12.0`, `ATTACK_RANGE=2.5`, `PATROL_SPEED=2.0`, `CHASE_SPEED=4.0`, `ATTACK_COOLDOWN=1.5`, `ATTACK_DAMAGE=15`, `WAYPOINT_THRESHOLD=1.0`, `DEFAULT_HEALTH=100`
**State machine:** IDLE -> PATROL -> CHASE -> ATTACK -> DEAD (per entity)
**Grace timer:** 3.0s after `AI.reset()` — enemies only patrol, no detection (Bug 3 fix)
**Public API:** `AI.updateAll(dt)`, `AI.addEnemy(entityId, waypointList, health)`, `AI.isEnemy(entityId)→bool`, `AI.damageEnemy(entityId, amount)`, `AI.reset()`
**Bindings used:** fillTransform3D, setLinearVelocity, setTransform3D (facing)

### `lib/hud.lua` — On-screen HUD

**Globals:** `HUD` table
**Constants:** `BAR_X=16`, `BAR_Y=50`, `BAR_WIDTH=200`, `BAR_HEIGHT=20`, `ARTIFACT_X/Y=16/80`, `LEVEL_X/Y=16/108`, `PROMPT_OFFSET_Y=100`
**Public API:** `HUD.draw(playerHealth, maxHealth, artifactCount, totalArtifacts, levelName, dt)`, `HUD.showPrompt(text, duration)`, `HUD.clearPrompt()`
**Features:** Red/green health bar, artifact count, level name, timed interaction prompt
**Bindings used:** drawRect, drawText, getScreenWidth, getScreenHeight

### `lib/menus.lua` — Title and pause menus

**Globals:** `Menus` table
**Public API:** `Menus.resetTitle()`, `Menus.drawTitle(dt)`, `Menus.resetPause()`, `Menus.drawPause(dt, selected)`, `Menus.handlePauseInput(dt)→"resume"|"restart"|"quit"|nil`, `Menus.getPauseSelected()→int`

---

## Common Fix Patterns

**Adding a mesh entity to a level:**
```lua
local e = ffe.createEntity3D(meshHandle, x, y, z)
ffe.setTransform3D(e, x, y, z, rx, ry, rz, sx, sy, sz)
ffe.setMeshColor(e, r, g, b, a)
```

**Correct terrain spawn height** (avoid spawning inside terrain):
```lua
local groundY = ffe.getTerrainHeight(x, z)
Player.create(x, groundY + 1.5, z, cubeMeshHandle)
```

**Enabling post-processing with software renderer guard:**
```lua
local softwareRenderer = ffe.isSoftwareRenderer()
if not softwareRenderer then
    ffe.enablePostProcessing()
    ffe.enableBloom(0.8, 0.3)
    ffe.setToneMapping(2)  -- ACES
    ffe.enableSSAO()
    ffe.setAntiAliasing(2)  -- FXAA
end
```

**Adding a point light (max 8 slots, indices 0-7):**
```lua
ffe.addPointLight(slotIndex, x, y, z, r, g, b, intensity, radius)
-- Remember to call ffe.removePointLight(slotIndex) in level cleanup
```

**Level cleanup sequence (order matters):**
```lua
ffe.cancelAllTimers()
ffe.unloadTerrain()           -- before destroyAllEntities
ffe.disablePostProcessing()
ffe.disableSSAO()
ffe.destroyAllEntities()
ffe.disableFog()
ffe.disableShadows()
for i = 0, 7 do ffe.removePointLight(i) end
ffe.unloadSkybox()
ffe.stopMusic()
```

**Module pattern (loading lib files):**
```lua
-- Modules use ffe.loadScene() not require (sandbox blocks require)
ffe.loadScene("lib/player.lua")  -- sets global Player table
Player.create(x, y, z, meshHandle)
```

**Screenshot targets for build-engineer:**
- showcase_menu: `ffe_runtime examples/showcase/game.lua` (wait 5s, shows title screen)
- showcase_level1/2/3: no direct launcher — must load via menu or test_level.lua
- Single-level test: `ffe_runtime examples/showcase/levels/test_level.lua` if it exists
