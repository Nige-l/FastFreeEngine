-- game.lua -- player movement + entity lifecycle + audio demo for FFE lua_demo
--
-- This script is loaded once at startup via scriptEngine.doFile().
-- It defines a global update() function that the C++ host calls each tick.
--
-- What this demo exercises:
--   - ffe.createEntity()          spawn entities from Lua
--   - ffe.addTransform(...)       attach Transform component
--   - ffe.addSprite(...)          attach Sprite component
--   - ffe.addPreviousTransform()  opt entity into render interpolation
--   - ffe.destroyEntity()         not demonstrated here (no pickup/death yet)
--   - ffe.loadTexture(path)       load a GPU texture from the asset root (Session 9)
--   - ffe.unloadTexture(handle)   release a GPU texture; called in shutdown() (Session 10)
--   - ffe.loadSound(path)         load an audio file from the asset root (Session 12)
--   - ffe.unloadSound(handle)     release a decoded audio buffer (Session 12)
--   - ffe.playSound(handle [,vol])play a one-shot sound effect (Session 12)
--   - ffe.playMusic(handle, loop) start looping background music (Session 11)
--   - ffe.stopMusic()             stop music track (Session 11)
--   - ffe.setMusicVolume(v)       adjust music volume (Session 11)
--   - ffe.isMusicPlaying()        query music state (Session 11)
--   - ffe.fillTransform(id, tbl)  zero-alloc transform read (Session 12)
--
-- Controls:
--   WASD       move the player
--   SPACE      play SFX (if loaded) + spawn a static marker (up to 5)
--   M          toggle music on/off (if a music track was loaded)
--   +/-        increase/decrease music volume (UP/DOWN arrow keys)
--   ESC        quit cleanly via ffe.requestShutdown()
--
-- Coordinate system: centered origin, -640..640 (x), -360..360 (y).
-- Window: 1280x720.
--
-- Audio note (Session 12):
--   Music and SFX are loaded from Lua via ffe.loadSound(). The asset root is
--   shared with textures (set from C++ via renderer::setAssetRoot). Audio files
--   should be placed under that root (e.g. assets/textures/music.ogg). If no
--   audio files exist, the loadSound calls return nil and playback is skipped.
--
-- Texture handle note (Session 9 + 10):
--   On the first update tick, ffe.loadTexture("checkerboard.png") is called once.
--   Falls back to stub handle 1 on failure (headless-safe).

local SPEED          = 150.0    -- pixels per second
local FOLLOWER_SPEED = 80.0     -- pixels per second, follower chases player
local HALF_W         = 640.0    -- visible half-width
local HALF_H         = 360.0    -- visible half-height
local HALF_SPRITE    = 24.0     -- half-size of the 48px player sprite
local MAX_MARKERS    = 5        -- maximum spawned static markers
local MARKER_TEX     = 1        -- stub texture handle (headless-safe fallback)
local FOLLOWER_TEX   = 1        -- stub texture handle; replaced at first tick if loadTexture succeeds
local MUSIC_VOL_STEP = 0.1      -- volume change per keypress

ffe.log("lua_demo: script loaded. WASD=move, SPACE=marker+sfx, M=music, arrows=volume, ESC=quit")

-- ---------------------------------------------------------------------------
-- Scene-init audio: load sounds from Lua (Session 12).
-- ffe.loadSound uses the shared asset root set from C++ (renderer::setAssetRoot).
-- Returns nil if the file does not exist or audio is unavailable (headless).
-- ---------------------------------------------------------------------------
local musicHandle = ffe.loadSound("music.ogg")
if musicHandle then
    ffe.log("lua_demo: music.ogg loaded from Lua (handle=" .. tostring(musicHandle) .. ")")
else
    ffe.log("lua_demo: music.ogg not found or audio unavailable -- music disabled")
end

-- SFX handle: will work once an SFX audio file (e.g. sfx.wav) is added to the
-- asset root alongside the textures. Until then, loadSound returns nil and the
-- playSound call in update() is gracefully skipped.
local sfxHandle = ffe.loadSound("sfx.wav")
if sfxHandle then
    ffe.log("lua_demo: sfx.wav loaded from Lua (handle=" .. tostring(sfxHandle) .. ")")
else
    ffe.log("lua_demo: sfx.wav not found -- SFX on SPACE disabled (add assets/textures/sfx.wav to enable)")
end

-- ---------------------------------------------------------------------------
-- Module-level state
-- Persistent across update() calls because these are upvalues of the module
-- chunk, not locals inside update().
-- ---------------------------------------------------------------------------

-- textureHandle: loaded once on the first update() tick via ffe.loadTexture.
-- nil until loaded; remains nil if the load fails (headless / no asset root).
-- Used for the follower sprite; falls back to FOLLOWER_TEX (stub=1) on failure.
local textureHandle = nil

-- followerEntityId: created on the first update() tick, reused every frame.
local followerEntityId = nil
local markerCount = 0

-- markers: table of entity IDs for spawned static sprites.
-- Declared here so that spawn logic inside update() can append to it.
local markers = {}

-- musicStarted: true once we have called ffe.playMusic on the first tick.
-- musicHandle is loaded from Lua at script load time (above).
local musicStarted = false

-- Pre-allocated table for fillTransform (Session 12).
-- Reused every frame for the follower entity to avoid per-frame table allocation.
local followerTransformBuf = {}

-- ---------------------------------------------------------------------------
-- Internal: clamp a value into [lo, hi]
-- ---------------------------------------------------------------------------
local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

-- ---------------------------------------------------------------------------
-- Internal: spawn the follower entity (called once on first tick).
-- Returns the entity ID, or nil if creation failed.
-- ---------------------------------------------------------------------------
local function spawnFollower()
    local id = ffe.createEntity()
    if id == nil then
        ffe.log("lua_demo: ERROR -- ffe.createEntity() returned nil for follower")
        return nil
    end

    -- Place follower at bottom-right of the screen initially.
    local ok = ffe.addTransform(id, 200.0, -150.0, 0.0, 1.0, 1.0)
    if not ok then
        ffe.log("lua_demo: ERROR -- ffe.addTransform() failed for follower (id=" .. tostring(id) .. ")")
        -- Entity exists but has no components; it will be invisible.
        return id
    end

    -- Sprite: 32x32, orange tint (RGBA 1.0, 0.6, 0.1, 1.0), layer 4.
    -- Layer is lower than the player (layer 5) so the player renders on top.
    ok = ffe.addSprite(id, FOLLOWER_TEX, 32.0, 32.0, 1.0, 0.6, 0.1, 1.0, 4)
    if not ok then
        ffe.log("lua_demo: ERROR -- ffe.addSprite() failed for follower")
    end

    -- Opt into render interpolation so the follower moves smoothly.
    ok = ffe.addPreviousTransform(id)
    if not ok then
        ffe.log("lua_demo: WARNING -- ffe.addPreviousTransform() failed for follower; "
                .. "motion may not interpolate")
    end

    ffe.log("lua_demo: follower spawned (id=" .. tostring(id) .. ")")
    return id
end

-- ---------------------------------------------------------------------------
-- Internal: spawn a static marker at (x, y).
-- Returns the entity ID, or nil if creation failed or cap reached.
-- ---------------------------------------------------------------------------
local function spawnMarker(x, y)
    if markerCount >= MAX_MARKERS then
        ffe.log("lua_demo: marker cap reached (" .. tostring(MAX_MARKERS) .. ") -- ignoring SPACE")
        return nil
    end

    local id = ffe.createEntity()
    if id == nil then
        ffe.log("lua_demo: ERROR -- ffe.createEntity() returned nil for marker")
        return nil
    end

    -- No interpolation for static markers -- they never move, so
    -- addPreviousTransform is unnecessary. This is a deliberate design choice
    -- to verify that sprites without PreviousTransform still render correctly.
    local ok = ffe.addTransform(id, x, y, 0.0, 1.0, 1.0)
    if not ok then
        ffe.log("lua_demo: ERROR -- ffe.addTransform() failed for marker")
        return id
    end

    -- Sprite: 16x16, cyan tint (RGBA 0.2, 0.9, 0.9, 0.8), layer 3.
    ok = ffe.addSprite(id, MARKER_TEX, 16.0, 16.0, 0.2, 0.9, 0.9, 0.8, 3)
    if not ok then
        ffe.log("lua_demo: ERROR -- ffe.addSprite() failed for marker")
    end

    markerCount = markerCount + 1
    markers[markerCount] = id
    ffe.log("lua_demo: marker " .. tostring(markerCount) .. " spawned at ("
            .. string.format("%.1f", x) .. ", " .. string.format("%.1f", y)
            .. ") id=" .. tostring(id))
    return id
end

-- ---------------------------------------------------------------------------
-- update(entityId, dt) -- called by the C++ LuaScriptSystem every tick.
-- entityId: raw u32 entity ID (Lua integer) of the player entity
-- dt:       delta time in seconds (fixed 1/60 for the default 60 Hz tick)
-- ---------------------------------------------------------------------------
function update(entityId, dt)

    -- ------------------------------------------------------------------
    -- First-tick initialisation: load the checkerboard texture via the
    -- Session 9 ffe.loadTexture binding, then spawn the follower.
    -- Both are deferred to the first update() call so that setWorld() and
    -- the renderer are already live.
    --
    -- ffe.loadTexture requires renderer::setAssetRoot() to have been called
    -- from C++ before this point. In the current lua_demo/main.cpp that call
    -- is absent (texture loading uses the two-argument C++ overload directly),
    -- so loadTexture may return nil. We guard for that and fall back to the
    -- stub handle FOLLOWER_TEX = 1 so the demo keeps working headless.
    -- ------------------------------------------------------------------
    if textureHandle == nil then
        local loaded = ffe.loadTexture("checkerboard.png")
        if loaded ~= nil then
            textureHandle = loaded
            ffe.log("lua_demo: ffe.loadTexture('checkerboard.png') succeeded, handle=" .. tostring(textureHandle))
            FOLLOWER_TEX = textureHandle  -- use the real texture for follower sprites
        else
            ffe.log("lua_demo: WARNING -- ffe.loadTexture('checkerboard.png') returned nil "
                    .. "(asset root not set from C++?). Follower will use stub handle 1.")
            textureHandle = false  -- sentinel: we attempted the load; do not retry
        end
    end

    if followerEntityId == nil then
        followerEntityId = spawnFollower()
    end

    -- ------------------------------------------------------------------
    -- Read and update player transform
    -- ------------------------------------------------------------------
    local pt = ffe.getTransform(entityId)
    if pt == nil then return end

    local dx = 0.0
    local dy = 0.0

    if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + SPEED * dt end

    local xLimit = HALF_W - HALF_SPRITE
    local yLimit = HALF_H - HALF_SPRITE

    local nx = clamp(pt.x + dx, -xLimit, xLimit)
    local ny = clamp(pt.y + dy, -yLimit, yLimit)

    ffe.setTransform(entityId, nx, ny, pt.rotation, pt.scaleX, pt.scaleY)

    -- ------------------------------------------------------------------
    -- Follower: move toward the player
    -- Uses fillTransform (Session 12) to avoid per-frame table allocation.
    -- followerTransformBuf is pre-allocated once at module scope.
    -- ------------------------------------------------------------------
    if followerEntityId ~= nil then
        if ffe.fillTransform(followerEntityId, followerTransformBuf) then
            -- Direction vector from follower to player
            local vx = nx - followerTransformBuf.x
            local vy = ny - followerTransformBuf.y
            local dist = math.sqrt(vx * vx + vy * vy)

            if dist > 1.0 then
                -- Normalise and scale by follower speed
                local speed = FOLLOWER_SPEED * dt
                -- Avoid overshooting: cap movement to remaining distance
                if speed > dist then speed = dist end
                local moveX = (vx / dist) * speed
                local moveY = (vy / dist) * speed
                ffe.setTransform(followerEntityId,
                                 followerTransformBuf.x + moveX,
                                 followerTransformBuf.y + moveY,
                                 followerTransformBuf.rotation,
                                 followerTransformBuf.scaleX,
                                 followerTransformBuf.scaleY)
            end
        end
    end

    -- ------------------------------------------------------------------
    -- SPACE: play SFX + spawn a static marker at the player's position
    -- ------------------------------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_SPACE) then
        -- Play one-shot sound effect if loaded (Session 12).
        -- sfxHandle is nil when no sfx.wav exists in the asset root — gracefully skipped.
        if sfxHandle then
            ffe.playSound(sfxHandle, 0.8)  -- 80% volume
        end
        spawnMarker(nx, ny)
    end

    -- ------------------------------------------------------------------
    -- Music: start looping on first tick if a handle was loaded from Lua.
    -- musicHandle is loaded at script init time via ffe.loadSound().
    -- If musicHandle is nil (file not found / audio unavailable), music
    -- controls are silently skipped.
    -- ------------------------------------------------------------------
    if not musicStarted and musicHandle then
        ffe.playMusic(musicHandle, true)
        ffe.setMusicVolume(0.5)
        musicStarted = true
        ffe.log("lua_demo: music started (handle=" .. tostring(musicHandle) .. ", vol=0.5)")
    end

    -- ------------------------------------------------------------------
    -- M: toggle music on/off
    -- UP/DOWN arrows: increase/decrease music volume
    -- ------------------------------------------------------------------
    if musicStarted then
        if ffe.isKeyPressed(ffe.KEY_M) then
            if ffe.isMusicPlaying() then
                ffe.stopMusic()
                ffe.log("lua_demo: music stopped")
            else
                ffe.playMusic(musicHandle, true)
                ffe.log("lua_demo: music resumed")
            end
        end

        if ffe.isKeyPressed(ffe.KEY_UP) then
            local vol = ffe.getMusicVolume()
            local newVol = vol + MUSIC_VOL_STEP
            if newVol > 1.0 then newVol = 1.0 end
            ffe.setMusicVolume(newVol)
            ffe.log("lua_demo: music volume -> " .. string.format("%.1f", newVol))
        end

        if ffe.isKeyPressed(ffe.KEY_DOWN) then
            local vol = ffe.getMusicVolume()
            local newVol = vol - MUSIC_VOL_STEP
            if newVol < 0.0 then newVol = 0.0 end
            ffe.setMusicVolume(newVol)
            ffe.log("lua_demo: music volume -> " .. string.format("%.1f", newVol))
        end
    end

    -- ------------------------------------------------------------------
    -- ESC: clean shutdown
    -- ------------------------------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end

-- ---------------------------------------------------------------------------
-- shutdown() -- called by ScriptEngine::shutdown() before lua_close().
--
-- This is the place to release any Lua-owned resources. Currently that means
-- the GPU texture loaded via ffe.loadTexture on the first update() tick.
--
-- textureHandle is a file-level local, so it is visible here.
-- It is set to `false` (not nil) when a load was attempted but failed, so we
-- guard with `textureHandle and textureHandle ~= false` to avoid passing the
-- boolean sentinel or nil into ffe.unloadTexture.
-- ---------------------------------------------------------------------------
function shutdown()
    ffe.log("lua_demo: Lua shutdown() called -- cleaning up resources")

    -- Stop music before cleanup
    if musicStarted and ffe.isMusicPlaying() then
        ffe.stopMusic()
        ffe.log("lua_demo: music stopped on shutdown")
    end

    -- Release Lua-loaded audio handles (Session 12)
    if musicHandle then
        ffe.unloadSound(musicHandle)
        ffe.log("lua_demo: ffe.unloadSound called for music handle=" .. tostring(musicHandle))
        musicHandle = nil
    end
    if sfxHandle then
        ffe.unloadSound(sfxHandle)
        ffe.log("lua_demo: ffe.unloadSound called for sfx handle=" .. tostring(sfxHandle))
        sfxHandle = nil
    end

    -- Release GPU texture
    if textureHandle and textureHandle ~= false then
        ffe.unloadTexture(textureHandle)
        ffe.log("lua_demo: ffe.unloadTexture called for handle=" .. tostring(textureHandle))
        textureHandle = nil
    else
        ffe.log("lua_demo: no texture handle to unload (load failed or never attempted)")
    end
end
