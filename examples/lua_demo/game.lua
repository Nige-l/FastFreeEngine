-- game.lua -- "Collect the Stars" demo for FFE
--
-- Exercises every subsystem: ECS entity lifecycle, sprite rendering,
-- sprite animation, collision detection, audio (music + SFX), input,
-- texture loading, and clean shutdown from Lua.
--
-- Controls:
--   WASD       move the player
--   M          toggle background music on/off
--   UP/DOWN    adjust music volume
--   ESC        quit cleanly via ffe.requestShutdown()
--
-- The C++ host creates one player entity (white square, 48x48) and passes
-- its ID to update(entityId, dt) each tick. Stars are spawned from Lua
-- using ffe.createEntity(). When the player overlaps a star, the collision
-- callback fires: the star is destroyed, score increments, SFX plays, and
-- a new star spawns at a random position.
--
-- Coordinate system: centered origin, x: -640..640, y: -360..360.
-- Window: 1280x720.

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local PLAYER_SPEED   = 200.0
local STAR_COUNT     = 8
local HALF_W         = 640
local HALF_H         = 360
local VOLUME_STEP    = 0.05

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------
local playerEntity    = nil
local stars           = {}
local score           = 0
local sfxHandle       = nil
local musicHandle     = nil
local spritesheetTex  = nil
local playerTex       = nil
local transformBuf    = {}  -- reusable table for fillTransform (zero-alloc reads)
local musicPlaying    = false

-- ---------------------------------------------------------------------------
-- Helper: update the HUD text with current game state
-- ---------------------------------------------------------------------------
local function updateHud()
    ffe.setHudText("Score: " .. score ..
                   "  |  Stars: " .. #stars ..
                   "  |  WASD move  |  M music  |  Up/Down vol  |  ESC quit")
end

-- ---------------------------------------------------------------------------
-- Helper: random position within bounds (with margin for sprite size)
-- ---------------------------------------------------------------------------
local function randomPos()
    return (math.random() * 2 - 1) * (HALF_W - 50),
           (math.random() * 2 - 1) * (HALF_H - 50)
end

-- ---------------------------------------------------------------------------
-- Helper: spawn a star at (x, y) with animated sprite and circle collider
-- ---------------------------------------------------------------------------
local function spawnStar(x, y)
    local id = ffe.createEntity()
    if not id then return nil end

    local rot = math.random() * 2 * math.pi  -- random rotation for visual variety
    ffe.addTransform(id, x, y, rot, 1, 1)
    ffe.addSprite(id, spritesheetTex or 1, 32, 32, 1, 1, 1, 1, 3)
    ffe.addPreviousTransform(id)
    ffe.addSpriteAnimation(id, 8, 4, 0.12, true)
    ffe.playAnimation(id)
    ffe.addCollider(id, "circle", 16, 0)  -- circle collider, radius 16

    return id
end

-- ---------------------------------------------------------------------------
-- Collision callback: detect player-star overlaps
-- ---------------------------------------------------------------------------
ffe.setCollisionCallback(function(entityA, entityB)
    for i, starId in ipairs(stars) do
        if (entityA == playerEntity and entityB == starId) or
           (entityB == playerEntity and entityA == starId) then
            -- Pickup!
            score = score + 1
            ffe.log("Score: " .. tostring(score))
            updateHud()

            if score % 10 == 0 then
                ffe.log("Amazing! Score: " .. score)
            end

            if sfxHandle then ffe.playSound(sfxHandle, 0.8) end

            ffe.destroyEntity(starId)

            -- Respawn at a new random position
            local nx, ny = randomPos()
            stars[i] = spawnStar(nx, ny)
            return
        end
    end
end)

-- ---------------------------------------------------------------------------
-- Scene init (runs once at load time)
-- ---------------------------------------------------------------------------

-- Load assets
spritesheetTex = ffe.loadTexture("textures/spritesheet.png")
playerTex      = ffe.loadTexture("textures/checkerboard.png")
musicHandle    = ffe.loadMusic("audio/music_pixelcrown.ogg") or ffe.loadMusic("audio/music.ogg")
sfxHandle      = ffe.loadSound("audio/sfx.wav")

if spritesheetTex then
    ffe.log("spritesheet.png loaded (handle=" .. tostring(spritesheetTex) .. ")")
else
    ffe.log("WARNING: spritesheet.png not loaded -- stars will use fallback texture")
end

-- Start background music
if musicHandle then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.3)
    musicPlaying = true
    ffe.log("Music started (handle=" .. tostring(musicHandle) .. ", vol=0.3)")
    ffe.log("Music controls: M to toggle, UP/DOWN arrows to adjust volume")
else
    ffe.log("WARNING: music.ogg not loaded -- music disabled")
end

-- Spawn stars with deterministic seed for reproducibility
math.randomseed(12345)
for i = 1, STAR_COUNT do
    local x, y = randomPos()
    stars[i] = spawnStar(x, y)
end

ffe.log("Collect the Stars! WASD to move, collect all stars. Score: 0")
updateHud()

-- ---------------------------------------------------------------------------
-- update(entityId, dt) -- called per tick by the C++ host
-- entityId: the player entity created in C++ (integer)
-- dt: fixed timestep delta (1/60 for 60 Hz)
-- ---------------------------------------------------------------------------
function update(entityId, dt)
    -- First-tick: remember the player entity, tint it light blue, add collider
    if playerEntity == nil then
        playerEntity = entityId
        -- Re-add sprite with light blue tint (r=0.7, g=0.85, b=1.0) for visual distinction
        ffe.addSprite(entityId, playerTex or 1, 48, 48, 0.7, 0.85, 1.0, 1.0, 1)
        ffe.addCollider(entityId, "aabb", 24, 24)  -- 48x48 sprite = 24 half-extents
    end

    -- Read player transform (zero-alloc via fillTransform)
    if not ffe.fillTransform(entityId, transformBuf) then return end

    -- Movement
    local dx, dy = 0, 0
    if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + PLAYER_SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - PLAYER_SPEED * dt end

    -- Clamp to screen bounds (with sprite margin)
    local nx = math.max(-616, math.min(616, transformBuf.x + dx))
    local ny = math.max(-336, math.min(336, transformBuf.y + dy))
    ffe.setTransform(entityId, nx, ny, transformBuf.rotation,
                     transformBuf.scaleX, transformBuf.scaleY)

    -- Music controls: M toggles, UP/DOWN adjust volume
    if musicHandle then
        if ffe.isKeyPressed(ffe.KEY_M) then
            if musicPlaying then
                ffe.stopMusic()
                musicPlaying = false
                ffe.log("Music paused")
            else
                ffe.playMusic(musicHandle, true)
                musicPlaying = true
                ffe.log("Music resumed")
            end
        end
        if ffe.isKeyPressed(ffe.KEY_UP) then
            local vol = math.min(1.0, ffe.getMusicVolume() + VOLUME_STEP)
            ffe.setMusicVolume(vol)
            ffe.log("Music volume: " .. string.format("%.2f", vol))
        end
        if ffe.isKeyPressed(ffe.KEY_DOWN) then
            local vol = math.max(0.0, ffe.getMusicVolume() - VOLUME_STEP)
            ffe.setMusicVolume(vol)
            ffe.log("Music volume: " .. string.format("%.2f", vol))
        end
    end

    -- ESC to quit
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end

-- ---------------------------------------------------------------------------
-- shutdown() -- called by ScriptEngine before lua_close()
-- ---------------------------------------------------------------------------
function shutdown()
    ffe.log("Final Score: " .. tostring(score))

    -- Stop and unload music
    if musicHandle then
        if ffe.isMusicPlaying() then ffe.stopMusic() end
        ffe.unloadSound(musicHandle)
        musicHandle = nil
    end

    -- Unload SFX
    if sfxHandle then
        ffe.unloadSound(sfxHandle)
        sfxHandle = nil
    end

    -- Unload textures
    if spritesheetTex then
        ffe.unloadTexture(spritesheetTex)
        spritesheetTex = nil
    end
    if playerTex then
        ffe.unloadTexture(playerTex)
        playerTex = nil
    end

    ffe.log("Collect the Stars shutdown complete")
end
