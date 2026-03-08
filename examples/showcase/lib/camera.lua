-- lib/camera.lua -- FPS-style orbit camera for "Echoes of the Ancients"
--
-- Third-person orbit camera that follows the player.
-- FPS-style free mouse look (cursor captured, no right-click needed).
-- Right stick on gamepad as fallback.
-- Smooth follow via lerp toward player position.
-- Collision avoidance: raycast behind player to prevent wall clipping.
--
-- Globals set: Camera (table with public API)

Camera = {}

local FLIP_BOTH = true  -- Set false if mouse look is inverted on your system
-- FLIP_BOTH=true: Wayland cursor-disabled delivers opposite-sign deltas

--------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------
local ORBIT_RADIUS     = 8.0     -- default distance from player
local ORBIT_MIN_RADIUS = 3.0     -- minimum distance (collision clamp)
local ORBIT_MAX_RADIUS = 15.0    -- maximum zoom out
local PITCH_MIN        = -30.0   -- degrees (looking up)
local PITCH_MAX        = 60.0    -- degrees (looking down)
local MOUSE_SENSITIVITY = 0.17   -- degrees per pixel (~0.003 rad/px)
local GAMEPAD_SENSITIVITY = 120  -- degrees per second for right stick
local FOLLOW_SPEED     = 8.0     -- lerp speed for smooth follow
local CAMERA_HEIGHT_OFFSET = 1.5 -- look slightly above player center

-- Terrain clearance: when terrain-aware mode is on, clamp the camera
-- so it never goes below terrain surface + this margin.
local TERRAIN_CLEARANCE = 0.5

--------------------------------------------------------------------
-- State
--------------------------------------------------------------------
local yaw       = 0.0       -- horizontal angle (degrees)
local pitch      = 20.0      -- vertical angle (degrees)
local radius     = ORBIT_RADIUS
local targetX    = 0.0       -- smoothed target position
local targetY    = 0.0
local targetZ    = 0.0
local terrainAware = false   -- set true by levels that use terrain

--------------------------------------------------------------------
-- Camera.update(dt)
-- Call once per frame from the main update loop.
--------------------------------------------------------------------
function Camera.update(dt)
    if not Player then return end

    local px, py, pz = Player.getPosition()

    -- Smooth follow: lerp toward player position
    local lerpFactor = 1.0 - math.exp(-FOLLOW_SPEED * dt)
    targetX = targetX + (px - targetX) * lerpFactor
    targetY = targetY + ((py + CAMERA_HEIGHT_OFFSET) - targetY) * lerpFactor
    targetZ = targetZ + (pz - targetZ) * lerpFactor

    -- FPS-style mouse look: use raw mouse deltas (cursor is captured)
    local dx = ffe.getMouseDeltaX()
    local dy = ffe.getMouseDeltaY()

    local flipSign = FLIP_BOTH and -1 or 1
    yaw   = yaw   + dx * MOUSE_SENSITIVITY * flipSign
    pitch = pitch - dy * MOUSE_SENSITIVITY * flipSign  -- negate: GLFW Y grows downward, so negative dy = mouse up = look up

    -- Gamepad: right stick for camera orbit (with dead-zone)
    if ffe.isGamepadConnected(0) then
        local rx = ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_RIGHT_X)
        local ry = ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_RIGHT_Y)

        -- Dead-zone: ignore stick values below 0.15 to avoid drift
        local DEAD_ZONE = 0.15
        if math.abs(rx) < DEAD_ZONE then rx = 0 end
        if math.abs(ry) < DEAD_ZONE then ry = 0 end

        yaw   = yaw   + rx * GAMEPAD_SENSITIVITY * dt * flipSign
        pitch = pitch - ry * GAMEPAD_SENSITIVITY * dt * flipSign  -- negate: stick up = negative ry = look up
    end

    -- Clamp pitch
    if pitch < PITCH_MIN then pitch = PITCH_MIN end
    if pitch > PITCH_MAX then pitch = PITCH_MAX end

    -- Wrap yaw to [0, 360)
    if yaw < 0 then yaw = yaw + 360 end
    if yaw >= 360 then yaw = yaw - 360 end

    -- Collision avoidance: raycast from target toward the camera position
    -- to prevent the camera going through walls.
    local effectiveRadius = radius
    local yawRad   = math.rad(yaw)
    local pitchRad = math.rad(pitch)

    -- Camera offset direction (from target toward where camera would be)
    -- Engine orbit: cam = target + radius*(cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))
    -- So direction from target toward camera is (+sin(yaw), +sin(pitch), +cos(yaw))
    local camDirX = math.sin(yawRad) * math.cos(pitchRad)
    local camDirY = math.sin(pitchRad)
    local camDirZ = math.cos(yawRad) * math.cos(pitchRad)

    -- Raycast from target in camera direction
    local hitEnt, hx, hy, hz, nx, ny, nz, hitDist =
        ffe.castRay(targetX, targetY, targetZ, camDirX, camDirY, camDirZ, radius)

    if hitEnt then
        -- Pull camera closer to avoid clipping (leave a small buffer)
        local buffer = 0.5
        effectiveRadius = math.max(ORBIT_MIN_RADIUS, hitDist - buffer)
    end

    -- Terrain-aware Y clamp: compute where the camera will actually be placed
    -- and ensure it never dips into the terrain surface.
    -- Engine orbit formula: camY = targetY + effectiveRadius * sin(pitch)
    -- pitchRad is already computed above.
    if terrainAware then
        local camX = targetX + effectiveRadius * math.cos(pitchRad) * math.sin(yawRad)
        local camY = targetY + effectiveRadius * math.sin(pitchRad)
        local camZ = targetZ + effectiveRadius * math.cos(pitchRad) * math.cos(yawRad)
        local groundY = ffe.getTerrainHeight(camX, camZ) or 0
        local minCamY = groundY + TERRAIN_CLEARANCE
        if camY < minCamY then
            -- The camera dips below terrain. Increase the effective pitch until
            -- the camera Y is above the minimum, but never exceed PITCH_MAX.
            -- Solve: targetY + r*sin(p) >= minCamY  =>  sin(p) >= (minCamY - targetY)/r
            local sinNeeded = (minCamY - targetY) / effectiveRadius
            if sinNeeded <= 1.0 then
                local pClamped = math.deg(math.asin(sinNeeded))
                if pClamped > pitch then
                    pitch = math.min(pClamped, PITCH_MAX)
                    pitchRad = math.rad(pitch)
                end
            end
        end
    end

    -- Apply orbit camera
    ffe.set3DCameraOrbit(targetX, targetY, targetZ, effectiveRadius, yaw, pitch)
end

--------------------------------------------------------------------
-- Camera.getYaw() -> degrees
-- Returns the current horizontal yaw angle for movement direction.
--------------------------------------------------------------------
function Camera.getYaw()
    return yaw
end

--------------------------------------------------------------------
-- Camera.getPitch() -> degrees
--------------------------------------------------------------------
function Camera.getPitch()
    return pitch
end

--------------------------------------------------------------------
-- Camera.setPosition(x, y, z)
-- Teleport the camera target (e.g., on level load).
--------------------------------------------------------------------
function Camera.setPosition(x, y, z)
    targetX = x
    targetY = (y or 0) + CAMERA_HEIGHT_OFFSET
    targetZ = z or 0
end

--------------------------------------------------------------------
-- Camera.setYawPitch(y, p)
--------------------------------------------------------------------
function Camera.setYawPitch(y, p)
    yaw   = y or yaw
    pitch = p or pitch
end

--------------------------------------------------------------------
-- Camera.setTerrainAware(enabled)
-- When true, the camera Y is clamped so it never dips below the
-- terrain surface + TERRAIN_CLEARANCE (0.5m). Call this after
-- ffe.loadTerrain() in any level that uses a terrain heightmap.
-- Default is false (off) so non-terrain levels are unaffected.
--------------------------------------------------------------------
function Camera.setTerrainAware(enabled)
    terrainAware = enabled and true or false
    ffe.log("[Camera] terrainAware=" .. tostring(terrainAware))
end

ffe.log("[Camera] Module loaded")
