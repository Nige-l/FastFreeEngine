-- game.lua -- player movement + entity lifecycle demo for FFE lua_demo
--
-- This script is loaded once at startup via scriptEngine.doFile().
-- It defines a global update() function that the C++ host calls each tick.
--
-- What this demo exercises (Session 8 entity lifecycle bindings):
--   - ffe.createEntity()          spawn entities from Lua
--   - ffe.addTransform(...)       attach Transform component
--   - ffe.addSprite(...)          attach Sprite component
--   - ffe.addPreviousTransform()  opt entity into render interpolation
--   - ffe.destroyEntity()         not demonstrated here (no pickup/death yet)
--
-- Controls:
--   WASD       move the player
--   SPACE      spawn a static marker at the player's position (up to 5)
--   ESC        quit cleanly via ffe.requestShutdown()
--
-- Coordinate system: centered origin, -640..640 (x), -360..360 (y).
-- Window: 1280x720.
--
-- Texture handle note:
--   The C++ host loads checkerboard.png and owns that handle. We cannot pass it
--   to Lua directly without a further binding (e.g. ffe.getTextureHandle(name)).
--   Follower and spawned-marker sprites use handle 1 as an opaque stub -- this
--   is explicitly documented as acceptable for the headless renderer in the
--   test suite and in the .context.md "Do not hold texture handles across scene
--   transitions" warning. A real game would pass the handle through a dedicated
--   C++ binding or expose it as a registered constant.

local SPEED        = 150.0      -- pixels per second
local FOLLOWER_SPEED = 80.0     -- pixels per second, follower chases player
local HALF_W       = 640.0      -- visible half-width
local HALF_H       = 360.0      -- visible half-height
local HALF_SPRITE  = 24.0       -- half-size of the 48px player sprite
local MAX_MARKERS  = 5          -- maximum spawned static markers
local MARKER_TEX   = 1          -- stub texture handle (headless-safe)
local FOLLOWER_TEX = 1          -- stub texture handle (headless-safe)

ffe.log("lua_demo: script loaded. WASD=move, SPACE=spawn marker, ESC=quit")

-- ---------------------------------------------------------------------------
-- Module-level state
-- Persistent across update() calls because these are upvalues of the module
-- chunk, not locals inside update().
-- ---------------------------------------------------------------------------

-- followerEntityId: created on the first update() tick, reused every frame.
local followerEntityId = nil
local markerCount = 0

-- markers: table of entity IDs for spawned static sprites.
-- Declared here so that spawn logic inside update() can append to it.
local markers = {}

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
    -- First-tick initialisation: spawn the follower.
    -- We defer this to the first update() call so that setWorld() has
    -- already been called and the World is live.
    -- ------------------------------------------------------------------
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
    -- ------------------------------------------------------------------
    if followerEntityId ~= nil then
        local ft = ffe.getTransform(followerEntityId)
        if ft ~= nil then
            -- Direction vector from follower to player
            local vx = nx - ft.x
            local vy = ny - ft.y
            local dist = math.sqrt(vx * vx + vy * vy)

            if dist > 1.0 then
                -- Normalise and scale by follower speed
                local speed = FOLLOWER_SPEED * dt
                -- Avoid overshooting: cap movement to remaining distance
                if speed > dist then speed = dist end
                local moveX = (vx / dist) * speed
                local moveY = (vy / dist) * speed
                ffe.setTransform(followerEntityId,
                                 ft.x + moveX,
                                 ft.y + moveY,
                                 ft.rotation,
                                 ft.scaleX,
                                 ft.scaleY)
            end
        end
    end

    -- ------------------------------------------------------------------
    -- SPACE: spawn a static marker at the player's current position
    -- ------------------------------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_SPACE) then
        spawnMarker(nx, ny)
    end

    -- ------------------------------------------------------------------
    -- ESC: clean shutdown
    -- ------------------------------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end
