-- game.lua -- "Collect the Stars" demo for FFE
--
-- Exercises every subsystem: ECS entity lifecycle, sprite rendering,
-- sprite animation, collision detection, audio (music + SFX), input,
-- texture loading, and clean shutdown from Lua.
--
-- Controls:
--   WASD   move the player
--   ESC    quit cleanly via ffe.requestShutdown()
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
local PLAYER_SPEED = 200.0
local STAR_COUNT   = 8
local HALF_W       = 640
local HALF_H       = 360

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

    ffe.addTransform(id, x, y, 0, 1, 1)
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
spritesheetTex = ffe.loadTexture("spritesheet.png")
playerTex      = ffe.loadTexture("checkerboard.png")
musicHandle    = ffe.loadSound("music_pixelcrown.ogg") or ffe.loadSound("music.ogg")
sfxHandle      = ffe.loadSound("sfx.wav")

if spritesheetTex then
    ffe.log("spritesheet.png loaded (handle=" .. tostring(spritesheetTex) .. ")")
else
    ffe.log("WARNING: spritesheet.png not loaded -- stars will use fallback texture")
end

-- Start background music
if musicHandle then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.3)
    ffe.log("Music started (handle=" .. tostring(musicHandle) .. ", vol=0.3)")
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

-- ---------------------------------------------------------------------------
-- update(entityId, dt) -- called per tick by the C++ host
-- entityId: the player entity created in C++ (integer)
-- dt: fixed timestep delta (1/60 for 60 Hz)
-- ---------------------------------------------------------------------------
function update(entityId, dt)
    -- First-tick: remember the player entity and add a collider to it
    if playerEntity == nil then
        playerEntity = entityId
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
