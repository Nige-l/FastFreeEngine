# FFE Quick-Start Tutorial: Build a Game in Lua

This tutorial walks you through building a simple game with FastFreeEngine. All game logic lives in Lua — no C++ required for gameplay code.

---

## Prerequisites

1. Build the engine (see [README.md](../README.md) for full instructions):

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build
```

2. You'll write a Lua script that the engine loads. The engine provides the `ffe.*` API table — your script uses it to create entities, handle input, play sounds, and more.

---

## 1. Script Structure

Every FFE Lua game script has this basic structure:

```lua
-- Scene init (runs once when the script is loaded)
-- Create entities, load assets, set up state here.

function update(entityId, dt)
    -- Called every frame (60 times per second).
    -- entityId: the player entity created by C++
    -- dt: time step (1/60 for 60 Hz)
end

function shutdown()
    -- Called once before the engine closes.
    -- Unload textures, sounds, and clean up here.
end
```

The `update()` function is where your game logic goes. The `shutdown()` function is optional but recommended for resource cleanup.

---

## 2. Creating Entities

Entities are the building blocks of your game. Each entity can have components: Transform (position/rotation/scale), Sprite (visual), Collider, and SpriteAnimation.

```lua
-- Create an entity
local entity = ffe.createEntity()

-- Give it a position (x=100, y=50, rotation=0, scaleX=1, scaleY=1)
ffe.addTransform(entity, 100, 50, 0, 1, 1)

-- Give it a visual (textureHandle, width, height, r, g, b, a, layer)
ffe.addSprite(entity, texHandle, 32, 32, 1, 1, 1, 1, 1)

-- Enable smooth movement interpolation
ffe.addPreviousTransform(entity)
```

**Coordinate system:** Centered origin. X ranges from -640 to 640, Y from -360 to 360 (1280x720 window).

---

## 3. Loading Textures

Load textures from the `assets/textures/` directory:

```lua
local tex = ffe.loadTexture("my_sprite.png")
if tex then
    ffe.log("Texture loaded: " .. tostring(tex))
else
    ffe.log("Failed to load texture!")
end
```

Unload textures in `shutdown()`:

```lua
function shutdown()
    if tex then
        ffe.unloadTexture(tex)
        tex = nil
    end
end
```

Supported formats: PNG (recommended), JPG, BMP, TGA.

---

## 4. Moving Things Around

Read and write entity positions using transforms:

```lua
-- Read position (zero-allocation method — use this in update())
local buf = {}  -- create once, reuse every frame
function update(entityId, dt)
    if ffe.fillTransform(entityId, buf) then
        local newX = buf.x + 100 * dt  -- move right at 100 pixels/sec
        ffe.setTransform(entityId, newX, buf.y, buf.rotation, buf.scaleX, buf.scaleY)
    end
end
```

The `fillTransform` function fills a reusable table with `.x`, `.y`, `.rotation`, `.scaleX`, `.scaleY` fields. This avoids creating a new table every frame (important for performance).

---

## 5. Handling Input

Check keyboard state every frame:

```lua
function update(entityId, dt)
    -- isKeyHeld: true while the key is held down
    if ffe.isKeyHeld(ffe.KEY_D) then
        -- move right
    end
    if ffe.isKeyHeld(ffe.KEY_A) then
        -- move left
    end

    -- isKeyPressed: true only on the frame the key first goes down
    if ffe.isKeyPressed(ffe.KEY_SPACE) then
        -- jump / shoot / interact
    end

    -- isKeyReleased: true only on the frame the key goes up
    if ffe.isKeyReleased(ffe.KEY_E) then
        -- end action
    end
end
```

**Available key constants:** `KEY_A` through `KEY_Z`, `KEY_0` through `KEY_9`, `KEY_SPACE`, `KEY_ESCAPE`, `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`, `KEY_LEFT_SHIFT`, `KEY_LEFT_CONTROL`, `KEY_ENTER`, `KEY_TAB`, `KEY_M`, `KEY_F1`.

Mouse: `ffe.getMouseX()`, `ffe.getMouseY()`.

---

## 6. Playing Audio

Load and play sounds:

```lua
-- Load sounds (WAV or OGG format)
local sfx = ffe.loadSound("sfx.wav")       -- fully decoded for playSound()
local music = ffe.loadMusic("music.ogg")    -- streaming-only for playMusic()

-- Play a one-shot sound effect (handle, volume 0.0-1.0)
ffe.playSound(sfx, 0.8)

-- Play looping background music
ffe.playMusic(music, true)       -- true = loop
ffe.setMusicVolume(0.3)          -- 0.0 to 1.0

-- Control music
ffe.stopMusic()
local vol = ffe.getMusicVolume()
local playing = ffe.isMusicPlaying()

-- Master volume for all SFX
ffe.setMasterVolume(0.5)
```

Always unload sounds in `shutdown()`:

```lua
function shutdown()
    if music then
        if ffe.isMusicPlaying() then ffe.stopMusic() end
        ffe.unloadSound(music)
    end
    if sfx then ffe.unloadSound(sfx) end
end
```

---

## 7. Collision Detection

Add colliders and set a callback:

```lua
-- Add an AABB collider (halfWidth, halfHeight)
ffe.addCollider(player, "aabb", 24, 24)

-- Add a circle collider (radius, unused)
ffe.addCollider(star, "circle", 16, 0)

-- Set a callback that fires when any two colliders overlap
ffe.setCollisionCallback(function(entityA, entityB)
    if entityA == player and entityB == star then
        ffe.destroyEntity(star)
        score = score + 1
    end
end)
```

Supported shapes: `"aabb"` (axis-aligned bounding box) and `"circle"`.

---

## 8. Sprite Animation

Animate sprites using a spritesheet (grid of frames):

```lua
local sheet = ffe.loadTexture("spritesheet.png")

local entity = ffe.createEntity()
ffe.addTransform(entity, 0, 0, 0, 1, 1)
ffe.addSprite(entity, sheet, 32, 32, 1, 1, 1, 1, 1)
ffe.addPreviousTransform(entity)

-- Add animation: frameCount, columns, frameDuration, looping
ffe.addSpriteAnimation(entity, 8, 4, 0.12, true)
ffe.playAnimation(entity)

-- Control animation
ffe.stopAnimation(entity)
ffe.setAnimationFrame(entity, 3)  -- jump to frame 3
local playing = ffe.isAnimationPlaying(entity)
```

The spritesheet should be a grid. The engine calculates UV coordinates automatically based on `frameCount` and `columns`.

---

## 9. Displaying Score / HUD Text

Show text on screen (top-center, always visible):

```lua
ffe.setHudText("Score: " .. score .. "  |  WASD to move")
```

Call this whenever the text should change. Pass `""` to clear it.

---

## 10. Camera Effects

Trigger screen shake for impact moments:

```lua
-- Small shake on hit (4 pixels, 0.12 seconds)
ffe.cameraShake(4, 0.12)

-- Big shake on explosion (12 pixels, 0.3 seconds)
ffe.cameraShake(12, 0.3)
```

The shake decays linearly. A new shake replaces any active one.

---

## 11. Quitting Cleanly

```lua
if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
    ffe.requestShutdown()
end
```

This tells the engine to stop the game loop after the current frame. Your `shutdown()` function will be called automatically.

---

## 12. Editor Overlay

Press **F1** at any time to toggle the debug editor:

- **Performance panel** — FPS, frame time, entity count, audio voices
- **Entity inspector** — select entities, edit positions/rotations live
- **Console** — scrollable log viewer with color-coded messages

The editor is only available in Debug builds.

---

## Complete Example

Here's a minimal but complete game script:

```lua
-- Minimal FFE game: move a square with WASD

local speed = 200
local tex = ffe.loadTexture("white.png")
local buf = {}

function update(entityId, dt)
    if not ffe.fillTransform(entityId, buf) then return end

    local dx, dy = 0, 0
    if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + speed * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - speed * dt end
    if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + speed * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - speed * dt end

    ffe.setTransform(entityId, buf.x + dx, buf.y + dy, 0, 1, 1)

    ffe.setHudText("WASD to move | ESC to quit")

    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end

function shutdown()
    if tex then ffe.unloadTexture(tex) end
end
```

---

## What's Next?

- Look at `examples/lua_demo/game.lua` — "Collect the Stars" demo using all engine features
- Look at `examples/pong/pong.lua` — classic Pong with visual effects and camera shake
- Look at `examples/breakout/breakout.lua` — Breakout with particles, ball trail, and screen shake
- Read `engine/scripting/.context.md` for the complete API reference
- Check out the other `.context.md` files in each engine subdirectory for detailed system documentation
