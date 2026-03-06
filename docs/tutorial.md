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

-- Query how many entities are alive (useful for debug HUD or spawn limits)
local count = ffe.getEntityCount()
```

**Coordinate system:** World coordinates use a centered origin — (0,0) is the center of the screen. X ranges from -640 to 640, Y from -360 to 360 (1280x720 window). Screen-space functions like `ffe.drawText` and `ffe.drawRect` use a different convention: top-left origin, with (0,0) at the top-left corner.

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

**Tinting and resizing sprites at runtime:**

After creating a sprite with `ffe.addSprite`, you can change its color tint and size without recreating the component:

```lua
-- Tint a sprite red (e.g., damage flash)
ffe.setSpriteColor(entity, 1, 0.3, 0.3, 1)

-- Reset to white (original texture colors)
ffe.setSpriteColor(entity, 1, 1, 1, 1)

-- Resize a sprite (e.g., power-up effect)
ffe.setSpriteSize(entity, 48, 48)
```

Both functions are safe to call every frame and are no-ops if the entity has no Sprite component.

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

There is also `ffe.getTransform(entityId)` which returns a new table with the same fields (plus `.z`, `.scaleZ`), but `fillTransform` is preferred in `update()` because it does not allocate.

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

**Available key constants:** `KEY_A` through `KEY_Z`, `KEY_0` through `KEY_9`, `KEY_SPACE`, `KEY_ESCAPE`, `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`, `KEY_LEFT_SHIFT`, `KEY_LEFT_CTRL`, `KEY_ENTER`, `KEY_TAB`, `KEY_M`, `KEY_F1`.

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

To remove a collider from an entity (e.g., making it pass-through after being collected), use `ffe.removeCollider(entityId)`.

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

## 9. Drawing HUD Text and Rectangles

Draw text anywhere on screen using `ffe.drawText()`. Coordinates are in screen pixels (top-left is 0,0):

```lua
function update(entityId, dt)
    -- Draw white text at position (10, 10)
    ffe.drawText("Score: " .. score, 10, 10)

    -- Draw colored text with custom scale
    -- drawText(text, x, y [, scale, r, g, b, a])
    ffe.drawText("GAME OVER", 500, 300, 3.0, 1, 0, 0, 1)  -- big red text

    -- Center text using screen dimensions
    local sw = ffe.getScreenWidth()
    local sh = ffe.getScreenHeight()
    local title = "MY GAME"
    local titleWidth = #title * 8 * 2  -- 8px per char * scale
    ffe.drawText(title, (sw - titleWidth) / 2, sh / 2, 2.0)
end
```

Draw filled rectangles for HUD panels and backgrounds:

```lua
-- Draw a semi-transparent background behind HUD text
-- drawRect(x, y, width, height [, r, g, b, a])
ffe.drawRect(5, 5, 200, 30, 0, 0, 0, 0.7)
ffe.drawText("Score: " .. score, 10, 10)
```

The built-in font is 8x8 pixels per character. Multiply by the scale factor to calculate text width.

**`ffe.setHudText(text)`** is a simpler alternative that shows persistent text top-center with a semi-transparent background — useful for debug info or global status:

```lua
-- Called once (or whenever the value changes) — not inside update()
ffe.setHudText("Score: " .. score .. "  Lives: " .. lives)

-- Clear the HUD text
ffe.setHudText("")
```

Unlike `drawText`, `setHudText` persists between frames without being called every frame. `drawText` gives you full control over position, color, and scale and must be called each frame to remain visible.

---

## 10. Camera Effects

Trigger screen shake for impact moments:

```lua
-- Small shake on brick hit (0.3 intensity, 0.04 seconds)
ffe.cameraShake(0.3, 0.04)

-- Bigger shake on losing a life (1.2 intensity, 0.10 seconds)
ffe.cameraShake(1.2, 0.10)
```

The `intensity` parameter sets the base pixel offset (clamped to [0, 100] on input). The shake decays exponentially (punchy start, smooth fade), and the **effective on-screen offset is hard-capped at 3 pixels** regardless of the intensity value. This keeps the effect readable on all screen sizes. In practice, values between 0.2 and 2.0 cover the useful range. A new shake replaces any active one.

---

## 11. Background Color

Set a custom background (clear) color for your game:

```lua
-- Set a dark blue background (called once at init)
ffe.setBackgroundColor(0.05, 0.05, 0.15)
```

RGB values are 0.0 to 1.0. Call this at the top of your script or in `update()`.

---

## 12. Mouse Input

Check mouse button state and position:

```lua
function update(entityId, dt)
    -- Mouse position in screen pixels
    local mx = ffe.getMouseX()
    local my = ffe.getMouseY()

    -- Mouse buttons: MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE
    if ffe.isMousePressed(ffe.MOUSE_LEFT) then
        -- clicked this frame
    end
    if ffe.isMouseHeld(ffe.MOUSE_LEFT) then
        -- held down
    end
    if ffe.isMouseReleased(ffe.MOUSE_LEFT) then
        -- released this frame
    end
end
```

---

## 13. Sprite Rotation and Flipping

Rotate sprites by setting the rotation parameter in `setTransform` (radians, counter-clockwise):

```lua
local angle = 0
function update(entityId, dt)
    angle = angle + 2.0 * dt  -- rotate ~2 radians/sec
    ffe.setTransform(entityId, x, y, angle, 1, 1)
end
```

Flip sprites horizontally or vertically (useful for character facing direction):

```lua
-- Flip horizontally when moving left
if dx < 0 then
    ffe.setSpriteFlip(entityId, true, false)
elseif dx > 0 then
    ffe.setSpriteFlip(entityId, false, false)
end
```

Flipping works correctly with sprite animations — the flip is applied at render time.

---

## 14. Title Screen Pattern

Most games need a title screen. Use a state variable to switch between screens:

```lua
local STATE_TITLE = 0
local STATE_PLAYING = 1
local STATE_GAMEOVER = 2
local state = STATE_TITLE

function update(entityId, dt)
    if state == STATE_TITLE then
        local sw = ffe.getScreenWidth()
        ffe.drawText("MY GAME", sw/2 - 56, 200, 2.0, 1, 0.8, 0, 1)
        ffe.drawText("Press SPACE to start", sw/2 - 80, 350)

        if ffe.isKeyPressed(ffe.KEY_SPACE) then
            state = STATE_PLAYING
        end

    elseif state == STATE_PLAYING then
        -- game logic here
        if lives <= 0 then state = STATE_GAMEOVER end

    elseif state == STATE_GAMEOVER then
        ffe.drawText("GAME OVER", 500, 300, 3.0, 1, 0, 0, 1)
        ffe.drawText("Press SPACE to restart", 480, 380)

        if ffe.isKeyPressed(ffe.KEY_SPACE) then
            state = STATE_TITLE
            -- reset game state...
        end
    end

    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end
```

See the Pong and Breakout demos for polished examples of this pattern with animated title screens.

---

## 15. Tilemaps

Render tile-based levels efficiently using tilemaps:

```lua
-- Load a tileset atlas (e.g., 4 columns x 4 rows = 16 tiles, each 16x16)
local tileset = ffe.loadTexture("tileset.png")

-- Create a tilemap entity
local map = ffe.createEntity()
ffe.addTransform(map, -200, 200, 0, 1, 1)  -- top-left corner position

-- Create tilemap: 20 wide, 15 tall, 16x16px tiles, 4 columns, 16 total tiles
ffe.addTilemap(map, 20, 15, 16, 16, tileset, 4, 16, 0)

-- Fill in tiles (index 0 = empty, 1+ = atlas frame)
for x = 0, 19 do
    ffe.setTile(map, x, 14, 1)   -- ground row (tile 1)
end
ffe.setTile(map, 5, 12, 3)       -- a platform block
ffe.setTile(map, 6, 12, 3)

-- Read a tile back
local t = ffe.getTile(map, 5, 12)  -- returns 3
```

**Rules:**
- Tile index 0 is always empty/transparent
- Indices 1+ map to your tileset atlas (1-based, left-to-right, top-to-bottom)
- Maximum grid size: 1024x1024
- Tilemaps bypass the render queue for efficiency — they render directly into the sprite batch
- The entity's Transform positions the top-left corner of the tilemap

---

## 16. Timers and Delays

Schedule actions to happen after a delay or at regular intervals:

```lua
-- One-shot: flash the screen red after 2 seconds
ffe.after(2.0, function()
    ffe.setBackgroundColor(1, 0, 0)
end)

-- Repeating: spawn an enemy every 3 seconds
local spawnTimer = ffe.every(3.0, function()
    local enemy = ffe.createEntity()
    ffe.addTransform(enemy, math.random(-600, 600), 400, 0, 1, 1)
    ffe.addSprite(enemy, enemyTex, 32, 32, 1, 0, 0, 1, 1)
end)

-- Cancel a timer when you no longer need it
ffe.cancelTimer(spawnTimer)
```

**Rules:**
- `ffe.after(seconds, callback)` fires once, then auto-cancels
- `ffe.every(seconds, callback)` fires repeatedly until cancelled
- Both return a timer ID (integer) you can pass to `ffe.cancelTimer()`
- Maximum 256 concurrent timers
- If a callback errors, the timer is auto-cancelled (no error spam)

---

## 17. Quitting Cleanly

```lua
if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
    ffe.requestShutdown()
end
```

This tells the engine to stop the game loop after the current frame. Your `shutdown()` function will be called automatically.

---

## 18. Scene Transitions

For larger games, you can split your game into multiple Lua files -- one per scene (title screen, gameplay, game over, etc.). Use `ffe.destroyAllEntities()` to wipe the current scene and `ffe.loadScene()` to load the next one.

```lua
-- title.lua
local tex = ffe.loadTexture("textures/title.png")
local bg = ffe.createEntity()
ffe.addTransform(bg, 640, 360, 0, 1, 1)
ffe.addSprite(bg, tex, 1280, 720, 1, 1, 1, 1, 0)

function update(entityId, dt)
    ffe.drawText("PRESS ENTER", 500, 500, 3, 1, 1, 1, 1)
    if ffe.isKeyPressed(ffe.KEY_ENTER) then
        ffe.unloadTexture(tex)  -- clean up title assets
        ffe.destroyAllEntities()  -- wipe all entities, timers, collision callback
        ffe.loadScene("gameplay.lua")  -- load and execute the next scene
    end
end
```

**What `ffe.destroyAllEntities()` does:**
- Destroys all entities and components
- Cancels all timers (`ffe.after` / `ffe.every`)
- Clears the collision callback

**What it does NOT do:**
- Unload textures or sounds (call `ffe.unloadTexture()` / `ffe.unloadSound()` yourself)
- Clear Lua globals (the new scene's `update()` replaces the old one)
- Stop music playback

You can also cancel timers without touching entities using `ffe.cancelAllTimers()`.

**Note:** Your C++ `main.cpp` must call `scriptEngine.setScriptRoot(path)` before `ffe.loadScene()` will work. All three demo games already do this.

---

## 19. Gamepad Input

FFE supports up to 4 gamepads simultaneously. Gamepad state follows the same Pressed/Held/Released pattern as keyboard input.

**Checking connection and reading input:**

```lua
function update(entityId, dt)
    -- Check if the first gamepad (index 0) is connected
    if ffe.isGamepadConnected(0) then
        -- Analog stick movement (deadzone already applied)
        local stickX = ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_LEFT_X)
        local stickY = ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_LEFT_Y)

        -- Apply movement
        local speed = 200
        local dx = stickX * speed * dt
        local dy = stickY * speed * dt

        -- Face buttons
        if ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_A) then
            -- jump (fires once on press)
        end
        if ffe.isGamepadButtonHeld(0, ffe.GAMEPAD_B) then
            -- sprint (continuous while held)
        end
        if ffe.isGamepadButtonReleased(0, ffe.GAMEPAD_X) then
            -- action on release (fires once on release)
        end

        -- D-pad
        if ffe.isGamepadButtonHeld(0, ffe.GAMEPAD_DPAD_LEFT) then
            dx = dx - speed * dt
        end
        if ffe.isGamepadButtonHeld(0, ffe.GAMEPAD_DPAD_RIGHT) then
            dx = dx + speed * dt
        end
    end
end
```

**Dual-input pattern (keyboard fallback + gamepad):**

```lua
local speed = 200
local buf = {}

function update(entityId, dt)
    if not ffe.fillTransform(entityId, buf) then return end

    local dx, dy = 0, 0

    -- Keyboard input
    if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + speed * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - speed * dt end
    if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + speed * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - speed * dt end

    -- Gamepad input (additive -- works alongside keyboard)
    if ffe.isGamepadConnected(0) then
        dx = dx + ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_LEFT_X) * speed * dt
        dy = dy + ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_LEFT_Y) * speed * dt
    end

    ffe.setTransform(entityId, buf.x + dx, buf.y + dy, buf.rotation, buf.scaleX, buf.scaleY)

    -- Accept either keyboard or gamepad to start
    local startPressed = ffe.isKeyPressed(ffe.KEY_SPACE)
    if ffe.isGamepadConnected(0) then
        startPressed = startPressed or ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_A)
    end
end
```

**Gamepad button constants:**

| Constant | Description |
|----------|-------------|
| `ffe.GAMEPAD_A` | South face button (Xbox A / PS Cross) |
| `ffe.GAMEPAD_B` | East face button (Xbox B / PS Circle) |
| `ffe.GAMEPAD_X` | West face button (Xbox X / PS Square) |
| `ffe.GAMEPAD_Y` | North face button (Xbox Y / PS Triangle) |
| `ffe.GAMEPAD_LEFT_BUMPER` | Left shoulder button (LB / L1) |
| `ffe.GAMEPAD_RIGHT_BUMPER` | Right shoulder button (RB / R1) |
| `ffe.GAMEPAD_BACK` | Back / Select / Share |
| `ffe.GAMEPAD_START` | Start / Menu / Options |
| `ffe.GAMEPAD_GUIDE` | Home / Guide / PS button (value 8) |
| `ffe.GAMEPAD_LEFT_STICK` | Left stick click — L3 (value 9) |
| `ffe.GAMEPAD_RIGHT_STICK` | Right stick click — R3 (value 10) |
| `ffe.GAMEPAD_DPAD_UP` | D-pad up |
| `ffe.GAMEPAD_DPAD_DOWN` | D-pad down |
| `ffe.GAMEPAD_DPAD_LEFT` | D-pad left |
| `ffe.GAMEPAD_DPAD_RIGHT` | D-pad right |

**Gamepad axis constants:**

| Constant | Range | Description |
|----------|-------|-------------|
| `ffe.GAMEPAD_AXIS_LEFT_X` | -1.0 to 1.0 | Left stick horizontal (negative = left) |
| `ffe.GAMEPAD_AXIS_LEFT_Y` | -1.0 to 1.0 | Left stick vertical (negative = up) |
| `ffe.GAMEPAD_AXIS_RIGHT_X` | -1.0 to 1.0 | Right stick horizontal |
| `ffe.GAMEPAD_AXIS_RIGHT_Y` | -1.0 to 1.0 | Right stick vertical |
| `ffe.GAMEPAD_AXIS_LEFT_TRIGGER` | -1.0 to 1.0 | Left trigger (-1.0 = released, 1.0 = fully pressed) |
| `ffe.GAMEPAD_AXIS_RIGHT_TRIGGER` | -1.0 to 1.0 | Right trigger (-1.0 = released, 1.0 = fully pressed) |

**Deadzone:** All axis values have a deadzone applied automatically -- small stick movements near the center are clamped to 0. You can adjust the deadzone threshold with `ffe.setGamepadDeadzone(value)`. You can also query the controller name with `ffe.getGamepadName(0)`.

---

## 20. Save/Load System

Persist game data (high scores, progress, settings) to disk as JSON files:

```lua
-- Save a table to disk
local ok, err = ffe.saveData("highscores.json", {
    best = 9500,
    name = "Player1",
    level = 3
})
if not ok then
    ffe.log("Save failed: " .. tostring(err))
end

-- Load it back
local data, err = ffe.loadData("highscores.json")
if data then
    ffe.log("Best score: " .. tostring(data.best))
else
    ffe.log("Load failed: " .. tostring(err))
end
```

**Saving game progress:**

```lua
local function saveProgress()
    local ok, err = ffe.saveData("progress.json", {
        level = currentLevel,
        score = score,
        lives = lives,
        settings = {
            musicVolume = ffe.getMusicVolume(),
            sfxVolume = 0.8
        }
    })
    if not ok then
        ffe.log("Failed to save: " .. tostring(err))
    end
end

local function loadProgress()
    local data, err = ffe.loadData("progress.json")
    if data then
        currentLevel = data.level or 1
        score = data.score or 0
        lives = data.lives or 3
        if data.settings then
            ffe.setMusicVolume(data.settings.musicVolume or 0.5)
        end
    end
    -- No file yet is fine -- use defaults
end
```

**Rules and limits:**
- Filenames must contain only letters, numbers, dots, hyphens, and underscores, and must end with `.json`
- No path separators (`/`, `\`) or `..` in filenames -- the engine rejects them
- Maximum file size: 1 MB
- Maximum save files: 128
- Maximum nesting depth: 32 levels
- Supported types: strings, numbers, booleans, and nested tables (functions and userdata are skipped)
- Writes are atomic (uses a temp file + rename) so a crash mid-save will not corrupt existing data

**Error handling:** `ffe.saveData` returns `true` on success, or `nil` plus an error string on failure. `ffe.loadData` returns the table on success, or `nil` plus an error string on failure. Always check the return value.

---

## 21. TTF Font Rendering

The built-in `ffe.drawText()` uses a fixed 8x8 bitmap font -- great for HUD text but not scalable. For high-quality text at any size, load a TrueType font:

```lua
-- Load a font at 24 pixels (path relative to assets/)
local titleFont = ffe.loadFont("fonts/myfont.ttf", 24)
if not titleFont then
    ffe.log("Failed to load font -- falling back to bitmap")
end

-- Load another size for body text
local bodyFont = ffe.loadFont("fonts/myfont.ttf", 14)
```

**Drawing TTF text:**

```lua
function update(entityId, dt)
    if titleFont then
        -- Draw at screen position (100, 50), white color
        ffe.drawFontText(titleFont, "My Game", 100, 50)

        -- Draw with custom scale and color (scale, r, g, b, a)
        ffe.drawFontText(titleFont, "Score: " .. score, 100, 100, 1.0, 1, 0.8, 0, 1)
    end

    -- Bitmap font still works alongside TTF
    ffe.drawText("FPS counter here", 10, 10)
end
```

**Centering text with measureText:**

```lua
local function drawCentered(fontId, text, y)
    local sw = ffe.getScreenWidth()
    local w, h = ffe.measureText(fontId, text)
    ffe.drawFontText(fontId, text, (sw - w) / 2, y)
end

function update(entityId, dt)
    if titleFont then
        drawCentered(titleFont, "GAME OVER", 300)
    end
end
```

**Bitmap vs TTF comparison:**

| Feature | `ffe.drawText` (bitmap) | `ffe.drawFontText` (TTF) |
|---------|------------------------|--------------------------|
| Font file needed | No (built-in 8x8) | Yes (`.ttf` file) |
| Scalable | Integer scale only | Any size via pixel size + scale |
| Character set | ASCII 32-126 | ASCII 32-126 |
| Best for | Debug text, simple HUD | Title screens, dialogue, polished UI |

**Rules:**
- Maximum 8 fonts loaded at once -- unload fonts you no longer need
- Pixel size range: 4 to 256
- Scale range: 0.1 to 20.0
- Always unload fonts in `shutdown()` to free GPU memory

```lua
function shutdown()
    if titleFont then ffe.unloadFont(titleFont) end
    if bodyFont then ffe.unloadFont(bodyFont) end
end
```

---

## 22. Particle Effects

Create visual effects like explosions, fire, trails, and sparks using the engine-side particle system. Each emitter manages up to 128 particles with no heap allocation.

**Explosion effect (one-shot burst):**

```lua
local function spawnExplosion(x, y)
    local fx = ffe.createEntity()
    ffe.addTransform(fx, x, y, 0, 1, 1)
    ffe.addEmitter(fx, {
        emitRate = 0,           -- no continuous emission
        lifetimeMin = 0.3,
        lifetimeMax = 0.8,
        speedMin = 50,
        speedMax = 200,
        sizeStart = 6,
        sizeEnd = 1,
        gravityY = -50,
        colorStartR = 1, colorStartG = 0.8, colorStartB = 0.2, colorStartA = 1,
        colorEndR = 1, colorEndG = 0.1, colorEndB = 0, colorEndA = 0
    })
    ffe.emitBurst(fx, 50)  -- emit 50 particles immediately

    -- Clean up the entity after particles fade
    ffe.after(1.0, function()
        ffe.destroyEntity(fx)
    end)
end
```

**Continuous trail effect (attach to a moving entity):**

```lua
-- Add a particle trail to the player entity
ffe.addEmitter(player, {
    emitRate = 20,          -- 20 particles per second
    lifetimeMin = 0.3,
    lifetimeMax = 0.6,
    speedMin = 5,
    speedMax = 15,
    angleMin = 3.14,        -- emit leftward (opposite of movement)
    angleMax = 3.14,
    sizeStart = 3,
    sizeEnd = 0,
    colorStartR = 0.3, colorStartG = 0.5, colorStartB = 1, colorStartA = 0.8,
    colorEndR = 0.1, colorEndG = 0.2, colorEndB = 0.5, colorEndA = 0,
    sortOrder = -1          -- render behind the player sprite
})
ffe.startEmitter(player)

-- Stop the trail when the player stops moving
ffe.stopEmitter(player)    -- existing particles fade out naturally
```

**Config table fields (all optional):**

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `emitRate` | number | 10 | Particles per second (continuous mode) |
| `lifetimeMin` | number | 0.5 | Minimum particle lifetime in seconds |
| `lifetimeMax` | number | 1.5 | Maximum particle lifetime in seconds |
| `speedMin` | number | 20 | Minimum initial speed |
| `speedMax` | number | 80 | Maximum initial speed |
| `angleMin` | number | 0 | Minimum emission angle in radians (0 = right) |
| `angleMax` | number | 6.28 | Maximum emission angle in radians (full circle) |
| `sizeStart` | number | 4 | Particle size at birth |
| `sizeEnd` | number | 0 | Particle size at death |
| `gravityY` | number | 0 | Gravity applied to Y velocity each tick |
| `offsetX` | number | 0 | Spawn offset from entity position (X) |
| `offsetY` | number | 0 | Spawn offset from entity position (Y) |
| `texture` | integer | (white) | Texture handle from `ffe.loadTexture()` |
| `layer` | integer | 0 | Render layer |
| `sortOrder` | integer | 0 | Sort order within the same layer (higher draws later) |
| `colorStartR/G/B/A` | number | 1,1,1,1 | Particle color at birth |
| `colorEndR/G/B/A` | number | 1,1,1,0 | Particle color at death (fades out by default) |

**Emitter functions:**
- `ffe.addEmitter(entityId [, config])` -- add an emitter (entity must have a Transform)
- `ffe.setEmitterConfig(entityId, config)` -- update emitter properties
- `ffe.startEmitter(entityId)` -- start continuous emission
- `ffe.stopEmitter(entityId)` -- stop emission (existing particles live out their lifetime)
- `ffe.emitBurst(entityId, count)` -- emit a fixed number of particles immediately (max 128)
- `ffe.removeEmitter(entityId)` -- remove the emitter component entirely

---

## Complete Example

Here's a minimal but complete game script:

```lua
-- Minimal FFE game: move a square with WASD

local speed = 200
local tex = ffe.loadTexture("white.png")
local buf = {}

ffe.setBackgroundColor(0.05, 0.05, 0.15)

function update(entityId, dt)
    if not ffe.fillTransform(entityId, buf) then return end

    local dx, dy = 0, 0
    if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + speed * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - speed * dt end
    if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + speed * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - speed * dt end

    ffe.setTransform(entityId, buf.x + dx, buf.y + dy, 0, 1, 1)

    -- HUD with background panel
    ffe.drawRect(5, 5, 250, 25, 0, 0, 0, 0.7)
    ffe.drawText("WASD to move | ESC to quit", 10, 10)

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

- Look at `examples/lua_demo/game.lua` -- "Collect the Stars" demo using all engine features
- Look at `examples/pong/pong.lua` -- classic Pong with visual effects and camera shake
- Look at `examples/breakout/breakout.lua` -- Breakout with particles, ball trail, and screen shake
- Try adding gamepad support to an existing demo (Section 19)
- Add save/load to persist high scores across sessions (Section 20)
- Load a TTF font for polished title screens (Section 21)
- Add particle effects for explosions, trails, and fire (Section 22)
- Read `engine/scripting/.context.md` for the complete API reference
- Check out the other `.context.md` files in each engine subdirectory for detailed system documentation
