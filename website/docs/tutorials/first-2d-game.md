# Your First 2D Game

In this tutorial, you will build **Collect the Stars** -- a simple game where you move a player around the screen, pick up stars, and rack up a score. Along the way you will learn the fundamentals of making games with FastFreeEngine: creating entities, drawing sprites, handling input, detecting collisions, and playing audio.

**What the finished game looks like:**

- A dark background with a colored player square you control with WASD
- 5 stars scattered across the screen
- Walk into a star to collect it -- your score goes up and a sound plays
- When all stars are collected, a new batch spawns
- Background music loops while you play
- Press ESC to quit

**Features you will learn:**

- Setting a background color
- Creating entities with sprites
- Reading keyboard input and moving the player
- Spawning entities at random positions
- Collision detection and callbacks
- Drawing text on the HUD
- Loading and playing sound effects
- Loading and playing background music

---

## Prerequisites

Before starting this tutorial, make sure you have:

1. Built FastFreeEngine from source (see the [Getting Started](../getting-started.md) guide)
2. Run the included demo to confirm everything works: `./build/FastFreeEngine`

You will be writing a single Lua file called `game.lua`. The engine loads this file automatically from your game's asset directory.

!!! info "No C++ required"
    All game logic in this tutorial is written in Lua. You do not need to touch any C++ code. FastFreeEngine handles the window, rendering, and engine loop -- you just write the gameplay.

---

## Step 1: Empty Game with a Background Color

Let's start with the simplest possible game script. Create a file called `game.lua` and add this:

```lua
-- game.lua -- Collect the Stars tutorial

ffe.setBackgroundColor(0.08, 0.06, 0.12)
ffe.log("Game started!")

function update(entityId, dt)
    -- We will add game logic here soon
end
```

**What is happening here:**

- `ffe.setBackgroundColor(r, g, b)` sets the screen clear color. The values are between 0 and 1. We are using a dark purple-blue, which looks great as a night sky.
- `ffe.log(message)` prints a message to the engine's log output. Useful for debugging.
- `update(entityId, dt)` is a special function that the engine calls every frame. `entityId` is the ID of the player entity (the engine creates one for you), and `dt` is the time in seconds since the last frame. We will fill this in soon.

Run the game to see your changes. You should see a dark purple screen -- that is your game world, ready for action.

---

## Step 2: Create a Player

Now let's put something on screen. The engine creates one entity and passes its ID to `update()`. We need to give that entity a visible sprite.

Add these lines to the top of your file (after the background color), and update the `update` function:

```lua
-- game.lua -- Collect the Stars tutorial

-- Load a texture for the player
local playerTex = ffe.loadTexture("textures/checkerboard.png")

-- Set the background color
ffe.setBackgroundColor(0.08, 0.06, 0.12)
ffe.log("Game started!")

-- Track the player entity
local playerEntity = nil

function update(entityId, dt)
    -- First frame: set up the player's sprite
    if playerEntity == nil then
        playerEntity = entityId
        ffe.addSprite(entityId, playerTex, 48, 48, 0.7, 0.85, 1.0, 1.0, 1)
        ffe.log("Player created!")
    end
end
```

**What is happening here:**

- `ffe.loadTexture(path)` loads an image file from your assets folder and returns a handle (a number). We store it in `playerTex` so we can use it later.
- `ffe.addSprite(entityId, texture, width, height, r, g, b, a, layer)` gives the entity a visible sprite. The width and height are 48 pixels. The color `(0.7, 0.85, 1.0, 1.0)` tints the sprite light blue. The last number is the render layer -- higher layers draw on top.
- We use `playerEntity == nil` to detect the first frame and only set up the sprite once.

!!! tip "Why check for nil?"
    The `update` function is called every single frame (60 times per second). We only want to set up the sprite once, on the very first frame. After that, `playerEntity` is no longer `nil`, so the setup code is skipped.

Run the game to see your changes. You should see a light-blue square in the center of the screen. That is your player!

---

## Step 3: Player Movement

A game is not much fun if you cannot move. Let's make the player respond to WASD keys.

Add a speed constant at the top of your file, and a reusable table for reading transforms. Then update the `update` function:

```lua
-- game.lua -- Collect the Stars tutorial

local PLAYER_SPEED = 200.0

-- Load a texture for the player
local playerTex = ffe.loadTexture("textures/checkerboard.png")

-- Set the background color
ffe.setBackgroundColor(0.08, 0.06, 0.12)
ffe.log("Game started!")

-- Track the player entity
local playerEntity = nil

-- Reusable table for reading transform data (avoids creating garbage)
local transformBuf = {}

function update(entityId, dt)
    -- First frame: set up the player's sprite
    if playerEntity == nil then
        playerEntity = entityId
        ffe.addSprite(entityId, playerTex, 48, 48, 0.7, 0.85, 1.0, 1.0, 1)
    end

    -- Read the player's current position
    if not ffe.fillTransform(entityId, transformBuf) then return end

    -- Calculate movement from keyboard input
    local dx, dy = 0, 0
    if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - PLAYER_SPEED * dt end

    -- Apply the movement
    local newX = transformBuf.x + dx
    local newY = transformBuf.y + dy

    -- Keep the player on screen (window is 1280x720, centered at 0,0)
    newX = math.max(-616, math.min(616, newX))
    newY = math.max(-336, math.min(336, newY))

    ffe.setTransform(entityId, newX, newY,
                     transformBuf.rotation,
                     transformBuf.scaleX, transformBuf.scaleY)
end
```

**What is happening here:**

- `PLAYER_SPEED = 200.0` means the player moves 200 pixels per second.
- `ffe.fillTransform(entityId, transformBuf)` reads the entity's current position into our reusable table. The table gets fields like `transformBuf.x`, `transformBuf.y`, `transformBuf.rotation`, `transformBuf.scaleX`, and `transformBuf.scaleY`.
- `ffe.isKeyHeld(keyCode)` returns `true` while a key is being held down. We multiply the speed by `dt` so movement is smooth regardless of frame rate.
- `ffe.setTransform(...)` writes the new position back to the entity.
- The `math.max` and `math.min` calls clamp the position so the player cannot leave the screen. The coordinate system is centered at (0, 0), so the screen goes from -640 to 640 horizontally and -360 to 360 vertically (with some margin for the sprite size).

!!! info "What is dt?"
    `dt` stands for "delta time" -- the number of seconds since the last frame. If the game runs at 60 FPS, `dt` is about 0.0167. Multiplying speed by `dt` makes movement frame-rate independent: the player moves the same distance per second whether the game runs at 30 FPS or 120 FPS.

Run the game to see your changes. Use WASD to move your player around the screen. Great job -- your player can move now!

---

## Step 4: Spawn Collectibles

Now let's add some stars to collect. We will create 5 star entities at random positions.

Add these sections to your file, before the `update` function:

```lua
-- Constants
local PLAYER_SPEED = 200.0
local STAR_COUNT   = 5
local HALF_W       = 640
local HALF_H       = 360

-- Load textures
local playerTex = ffe.loadTexture("textures/checkerboard.png")
local starTex   = ffe.loadTexture("textures/white.png")

-- Set the background color
ffe.setBackgroundColor(0.08, 0.06, 0.12)

-- Game state
local playerEntity = nil
local transformBuf = {}
local stars        = {}
local score        = 0

-- Helper: pick a random position on screen (with margin)
local function randomPos()
    return (math.random() * 2 - 1) * (HALF_W - 50),
           (math.random() * 2 - 1) * (HALF_H - 50)
end

-- Helper: create a star entity at position (x, y)
local function spawnStar(x, y)
    local id = ffe.createEntity()
    if not id then return nil end

    ffe.addTransform(id, x, y, 0, 1, 1)
    ffe.addSprite(id, starTex, 24, 24, 1.0, 0.9, 0.3, 1.0, 2)

    return id
end

-- Spawn the initial batch of stars
for i = 1, STAR_COUNT do
    local x, y = randomPos()
    stars[i] = spawnStar(x, y)
end

ffe.log("Collect the Stars! WASD to move.")
```

**What is happening here:**

- `ffe.createEntity()` creates a brand new entity and returns its ID. Unlike the player (which the engine creates for you), stars are entities we create ourselves.
- `ffe.addTransform(id, x, y, rotation, scaleX, scaleY)` gives the entity a position in the world. Without a transform, the entity would have no location.
- For the star sprite, we use a white texture tinted gold with color `(1.0, 0.9, 0.3)`. The render layer is 2, which puts stars above the background but you can tweak this.
- `math.random()` returns a random number between 0 and 1. We scale it to cover the screen area with some margin so stars do not spawn right at the edges.
- The `for` loop creates 5 stars and stores their IDs in the `stars` table.

!!! tip "Why store star IDs in a table?"
    We need to track which entities are stars so we can check for collisions later and know which ones to destroy when collected. The `stars` table is our record of all active star entities.

Run the game to see your changes. You should see 5 small gold squares scattered across the screen, with your player in the center. You can move around, but nothing happens when you touch them -- yet!

---

## Step 5: Collision Detection

Now for the exciting part: let's make the player actually collect stars. We need to add colliders to entities and register a collision callback.

First, update the player setup inside `update` to add a collider:

```lua
    -- First frame: set up the player's sprite and collider
    if playerEntity == nil then
        playerEntity = entityId
        ffe.addSprite(entityId, playerTex, 48, 48, 0.7, 0.85, 1.0, 1.0, 1)
        ffe.addCollider(entityId, "aabb", 24, 24)
    end
```

Next, update `spawnStar` to give each star a collider too:

```lua
local function spawnStar(x, y)
    local id = ffe.createEntity()
    if not id then return nil end

    ffe.addTransform(id, x, y, 0, 1, 1)
    ffe.addSprite(id, starTex, 24, 24, 1.0, 0.9, 0.3, 1.0, 2)
    ffe.addCollider(id, "circle", 12, 0)

    return id
end
```

Finally, add the collision callback. Put this **after** the star spawning loop, before the `update` function:

```lua
-- Collision callback: detect player-star overlaps
ffe.setCollisionCallback(function(entityA, entityB)
    for i, starId in ipairs(stars) do
        if (entityA == playerEntity and entityB == starId) or
           (entityB == playerEntity and entityA == starId) then
            -- Destroy the collected star
            ffe.destroyEntity(starId)

            -- Increment the score
            score = score + 1
            ffe.log("Star collected! Score: " .. score)

            -- Spawn a replacement star at a new random position
            local nx, ny = randomPos()
            stars[i] = spawnStar(nx, ny)

            return
        end
    end
end)
```

**What is happening here:**

- `ffe.addCollider(entityId, shape, halfW, halfH)` gives an entity a collision shape. The player gets an `"aabb"` (axis-aligned bounding box) with half-width and half-height of 24 pixels. Stars get a `"circle"` collider with radius 12.
- `ffe.setCollisionCallback(func)` registers a function that the engine calls whenever two entities overlap. The function receives two entity IDs.
- Inside the callback, we loop through our `stars` table to check if one of the colliding entities is a star and the other is the player.
- When we find a match, we destroy the star with `ffe.destroyEntity`, increment the score, and spawn a replacement star at a new random location.

!!! warning "Collision callback timing"
    The collision callback fires after all game systems have run for the frame. It is safe to destroy entities inside the callback -- the destroyed entity simply will not appear in the next frame.

Run the game to see your changes. Move your player into a star -- it should disappear and a new one should appear somewhere else. Check the log output to see your score counting up. You are collecting stars!

---

## Step 6: Score and HUD

Checking the log for your score is not very fun. Let's display it on screen using `ffe.drawText`.

Add this to the bottom of your `update` function (after the movement code):

```lua
    -- Draw the score on screen
    ffe.drawText("SCORE: " .. tostring(score), 20, 16, 3, 1, 1, 1, 1)
```

Your `update` function should now look like this:

```lua
function update(entityId, dt)
    -- First frame: set up the player's sprite and collider
    if playerEntity == nil then
        playerEntity = entityId
        ffe.addSprite(entityId, playerTex, 48, 48, 0.7, 0.85, 1.0, 1.0, 1)
        ffe.addCollider(entityId, "aabb", 24, 24)
    end

    -- Read the player's current position
    if not ffe.fillTransform(entityId, transformBuf) then return end

    -- Calculate movement from keyboard input
    local dx, dy = 0, 0
    if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - PLAYER_SPEED * dt end

    -- Apply the movement
    local newX = math.max(-616, math.min(616, transformBuf.x + dx))
    local newY = math.max(-336, math.min(336, transformBuf.y + dy))
    ffe.setTransform(entityId, newX, newY,
                     transformBuf.rotation,
                     transformBuf.scaleX, transformBuf.scaleY)

    -- Draw the score on screen
    ffe.drawText("SCORE: " .. tostring(score), 20, 16, 3, 1, 1, 1, 1)
end
```

**What is happening here:**

- `ffe.drawText(text, x, y, scale, r, g, b, a)` draws text on the screen every frame. The position `(20, 16)` is in screen pixels, starting from the top-left corner. Scale `3` makes the text 24 pixels tall (the base font is 8 pixels, so 8 times 3 = 24). The color `(1, 1, 1, 1)` is white at full opacity.
- We call `drawText` inside `update` because HUD text must be drawn every frame -- it does not persist between frames automatically.

!!! tip "Screen coordinates vs. world coordinates"
    `ffe.drawText` uses **screen coordinates** (top-left is 0, 0). Entity positions use **world coordinates** (centered at 0, 0). These are different systems! HUD elements like score displays should use screen coordinates so they stay in a fixed position regardless of the camera.

Run the game to see your changes. You should see "SCORE: 0" in the top-left corner, and it should count up as you collect stars. Now we are getting somewhere!

---

## Step 7: Sound Effects

Games feel so much better with audio feedback. Let's add a sound that plays when you collect a star.

Add this near the top of your file, with the other texture loading:

```lua
-- Load audio
local sfxPickup = ffe.loadSound("audio/sfx.wav")
```

Then update the collision callback to play the sound when a star is collected:

```lua
ffe.setCollisionCallback(function(entityA, entityB)
    for i, starId in ipairs(stars) do
        if (entityA == playerEntity and entityB == starId) or
           (entityB == playerEntity and entityA == starId) then
            -- Destroy the collected star
            ffe.destroyEntity(starId)

            -- Increment the score
            score = score + 1

            -- Play the pickup sound
            if sfxPickup then
                ffe.playSound(sfxPickup, 0.8)
            end

            -- Spawn a replacement star
            local nx, ny = randomPos()
            stars[i] = spawnStar(nx, ny)

            return
        end
    end
end)
```

**What is happening here:**

- `ffe.loadSound(path)` loads a WAV or OGG audio file and returns a handle (a number), or `nil` if loading failed. We load it once at the top of the script, not inside `update`.
- `ffe.playSound(handle, volume)` plays a one-shot sound effect. The volume is 0.8 (80%), which keeps it from being too loud. You can use any value from 0.0 (silent) to 1.0 (full volume).
- We check `if sfxPickup then` before playing, just in case the audio file failed to load. Always good practice!

Run the game to see your changes. Collect a star and you should hear a sound. Satisfying!

---

## Step 8: Background Music

Let's add some background music to set the mood.

Add this near the top of your file, with the other audio loading:

```lua
-- Load music
local musicHandle = ffe.loadMusic("audio/music.ogg")

-- Start playing music
if musicHandle then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.3)
end
```

**What is happening here:**

- `ffe.loadMusic(path)` loads a music file for streaming playback. It works like `loadSound` but is optimized for longer tracks.
- `ffe.playMusic(handle, loop)` starts playing the music. The second argument `true` means it loops forever.
- `ffe.setMusicVolume(0.3)` sets the music to 30% volume so it does not overpower the sound effects.

!!! info "loadMusic vs. loadSound"
    Use `ffe.loadMusic` for background music tracks (longer files). Use `ffe.loadSound` for short sound effects. Music tracks are streamed from disk and do not have the 10 MB size limit that sound effects have.

Run the game to see your changes. You should hear background music as soon as the game starts. The stars are starting to feel like a real game now!

---

## Step 9: Polish -- Quit and Respawn

Let's add two finishing touches: pressing ESC to quit, and spawning a new batch of stars when all have been collected.

Add this to the bottom of your `update` function:

```lua
    -- ESC to quit
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
```

Then update the collision callback to check if all stars have been collected and respawn them:

```lua
ffe.setCollisionCallback(function(entityA, entityB)
    for i, starId in ipairs(stars) do
        if (entityA == playerEntity and entityB == starId) or
           (entityB == playerEntity and entityA == starId) then
            ffe.destroyEntity(starId)
            score = score + 1

            if sfxPickup then
                ffe.playSound(sfxPickup, 0.8)
            end

            -- Remove the star from the table
            table.remove(stars, i)

            -- If all stars are gone, spawn a new batch
            if #stars == 0 then
                ffe.log("All stars collected! Spawning more...")
                for j = 1, STAR_COUNT do
                    local x, y = randomPos()
                    stars[j] = spawnStar(x, y)
                end
            end

            return
        end
    end
end)
```

**What is happening here:**

- `ffe.isKeyPressed(keyCode)` returns `true` only on the exact frame a key is first pressed down (unlike `isKeyHeld` which is true every frame the key is held). Perfect for one-shot actions like quitting.
- `ffe.requestShutdown()` tells the engine to close cleanly after the current frame.
- Instead of immediately replacing each collected star, we now use `table.remove` to shrink the `stars` table. When the table is empty (`#stars == 0`), we spawn a whole new batch. This creates a satisfying "wave" rhythm.

!!! tip "Adding a shutdown function"
    You can also add a `shutdown()` function that the engine calls when the game closes. This is a good place to unload textures and sounds to free memory:

    ```lua
    function shutdown()
        if sfxPickup then ffe.unloadSound(sfxPickup) end
        if musicHandle then
            ffe.stopMusic()
            ffe.unloadSound(musicHandle)
        end
        if playerTex then ffe.unloadTexture(playerTex) end
        if starTex then ffe.unloadTexture(starTex) end
        ffe.log("Thanks for playing! Final score: " .. score)
    end
    ```

Run the game to see your changes. Collect all 5 stars -- a new batch should appear. Press ESC to quit cleanly. Congratulations, you have built a complete game!

---

## Complete Code

Here is the full `game.lua` with everything from this tutorial. Copy this into your game's asset directory and run the engine.

```lua
-- game.lua -- Collect the Stars
-- A complete tutorial game for FastFreeEngine.
--
-- Controls:
--   WASD    move the player
--   ESC     quit

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local PLAYER_SPEED = 200.0
local STAR_COUNT   = 5
local HALF_W       = 640
local HALF_H       = 360

-- ---------------------------------------------------------------------------
-- Load assets (runs once when the script is loaded)
-- ---------------------------------------------------------------------------
local playerTex  = ffe.loadTexture("textures/checkerboard.png")
local starTex    = ffe.loadTexture("textures/white.png")
local sfxPickup  = ffe.loadSound("audio/sfx.wav")
local musicHandle = ffe.loadMusic("audio/music.ogg")

-- ---------------------------------------------------------------------------
-- Set up the scene
-- ---------------------------------------------------------------------------
ffe.setBackgroundColor(0.08, 0.06, 0.12)

-- Start background music
if musicHandle then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.3)
end

-- ---------------------------------------------------------------------------
-- Game state
-- ---------------------------------------------------------------------------
local playerEntity = nil
local transformBuf = {}
local stars        = {}
local score        = 0

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------
local function randomPos()
    return (math.random() * 2 - 1) * (HALF_W - 50),
           (math.random() * 2 - 1) * (HALF_H - 50)
end

local function spawnStar(x, y)
    local id = ffe.createEntity()
    if not id then return nil end

    ffe.addTransform(id, x, y, 0, 1, 1)
    ffe.addSprite(id, starTex, 24, 24, 1.0, 0.9, 0.3, 1.0, 2)
    ffe.addCollider(id, "circle", 12, 0)

    return id
end

-- ---------------------------------------------------------------------------
-- Spawn the first batch of stars
-- ---------------------------------------------------------------------------
for i = 1, STAR_COUNT do
    local x, y = randomPos()
    stars[i] = spawnStar(x, y)
end

-- ---------------------------------------------------------------------------
-- Collision callback: collect stars on overlap
-- ---------------------------------------------------------------------------
ffe.setCollisionCallback(function(entityA, entityB)
    for i, starId in ipairs(stars) do
        if (entityA == playerEntity and entityB == starId) or
           (entityB == playerEntity and entityA == starId) then
            -- Collect the star
            ffe.destroyEntity(starId)
            score = score + 1

            -- Play pickup sound
            if sfxPickup then
                ffe.playSound(sfxPickup, 0.8)
            end

            -- Remove from the table
            table.remove(stars, i)

            -- If all collected, spawn a new wave
            if #stars == 0 then
                ffe.log("All stars collected! Spawning more...")
                for j = 1, STAR_COUNT do
                    local nx, ny = randomPos()
                    stars[j] = spawnStar(nx, ny)
                end
            end

            return
        end
    end
end)

ffe.log("Collect the Stars! WASD to move, ESC to quit.")

-- ---------------------------------------------------------------------------
-- update(entityId, dt) -- called every frame by the engine
-- ---------------------------------------------------------------------------
function update(entityId, dt)
    -- First frame: set up the player
    if playerEntity == nil then
        playerEntity = entityId
        ffe.addSprite(entityId, playerTex, 48, 48, 0.7, 0.85, 1.0, 1.0, 1)
        ffe.addCollider(entityId, "aabb", 24, 24)
    end

    -- Read the player's current position
    if not ffe.fillTransform(entityId, transformBuf) then return end

    -- Movement
    local dx, dy = 0, 0
    if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - PLAYER_SPEED * dt end

    -- Clamp to screen bounds
    local newX = math.max(-616, math.min(616, transformBuf.x + dx))
    local newY = math.max(-336, math.min(336, transformBuf.y + dy))
    ffe.setTransform(entityId, newX, newY,
                     transformBuf.rotation,
                     transformBuf.scaleX, transformBuf.scaleY)

    -- Draw HUD
    ffe.drawText("SCORE: " .. tostring(score), 20, 16, 3, 1, 1, 1, 1)

    -- ESC to quit
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end

-- ---------------------------------------------------------------------------
-- shutdown() -- called once when the engine closes
-- ---------------------------------------------------------------------------
function shutdown()
    -- Unload audio
    if sfxPickup then ffe.unloadSound(sfxPickup) end
    if musicHandle then
        ffe.stopMusic()
        ffe.unloadSound(musicHandle)
    end

    -- Unload textures
    if playerTex then ffe.unloadTexture(playerTex) end
    if starTex then ffe.unloadTexture(starTex) end

    ffe.log("Thanks for playing! Final score: " .. score)
end
```

---

## What's Next?

You have built a working game. Here are some ideas to take it further on your own:

- **Add a title screen** -- Show text like "PRESS SPACE TO START" using `ffe.drawText` and wait for `ffe.isKeyPressed(ffe.KEY_SPACE)` before starting the game
- **Animate the stars** -- If you have a spritesheet, use `ffe.addSpriteAnimation` and `ffe.playAnimation` to make the stars spin
- **Add a camera shake** -- Call `ffe.cameraShake(0.5, 0.06)` when collecting a star for a satisfying screen punch
- **Add particles** -- Use `ffe.addEmitter` to create a burst effect when a star is collected
- **Add a timer** -- Use `ffe.drawText` to show elapsed time and challenge yourself to collect stars faster
- **Add music controls** -- Use `ffe.isKeyPressed(ffe.KEY_M)` to toggle music on and off

!!! tip "Look at the full demo"
    The engine ships with a more advanced version of this game in `examples/lua_demo/game.lua`. It includes animated sprites, particle effects, a title screen, music controls, and more. Read through it to see how the concepts from this tutorial can be expanded.

Happy game making!
