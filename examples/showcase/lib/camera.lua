-- lib/camera.lua -- Orbit camera module for "Echoes of the Ancients"
--
-- Third-person orbit camera that follows the player.
-- Mouse-controlled orbit (horizontal = yaw, vertical = pitch).
-- Right stick on gamepad for camera control.
-- Smooth follow via lerp toward player position.
-- Collision avoidance: raycast behind player to prevent wall clipping.
--
-- Globals set: Camera (table with public API)

Camera = {}

--------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------
local ORBIT_RADIUS     = 8.0     -- default distance from player
local ORBIT_MIN_RADIUS = 3.0     -- minimum distance (collision clamp)
local ORBIT_MAX_RADIUS = 15.0    -- maximum zoom out
local PITCH_MIN        = -30.0   -- degrees (looking up)
local PITCH_MAX        = 60.0    -- degrees (looking down)
local MOUSE_SENSITIVITY = 0.15   -- degrees per pixel of mouse movement
local GAMEPAD_SENSITIVITY = 120  -- degrees per second for right stick
local FOLLOW_SPEED     = 8.0     -- lerp speed for smooth follow
local CAMERA_HEIGHT_OFFSET = 1.5 -- look slightly above player center

--------------------------------------------------------------------
-- State
--------------------------------------------------------------------
local yaw       = 0.0       -- horizontal angle (degrees)
local pitch      = 20.0      -- vertical angle (degrees)
local radius     = ORBIT_RADIUS
local targetX    = 0.0       -- smoothed target position
local targetY    = 0.0
local targetZ    = 0.0
local prevMouseX = 0
local prevMouseY = 0
local initialized = false

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

    -- Mouse look: track delta from previous frame
    local mx = ffe.getMouseX()
    local my = ffe.getMouseY()

    if not initialized then
        prevMouseX = mx
        prevMouseY = my
        initialized = true
    end

    -- Only orbit when right mouse button is held (free look)
    if ffe.isMouseHeld(ffe.MOUSE_RIGHT) then
        local dx = mx - prevMouseX
        local dy = my - prevMouseY
        yaw   = yaw   + dx * MOUSE_SENSITIVITY
        pitch = pitch + dy * MOUSE_SENSITIVITY
    end

    prevMouseX = mx
    prevMouseY = my

    -- Keyboard camera controls (arrow keys as fallback)
    if ffe.isKeyHeld(ffe.KEY_LEFT) then
        yaw = yaw - 90 * dt
    end
    if ffe.isKeyHeld(ffe.KEY_RIGHT) then
        yaw = yaw + 90 * dt
    end
    if ffe.isKeyHeld(ffe.KEY_UP) then
        pitch = pitch - 45 * dt
    end
    if ffe.isKeyHeld(ffe.KEY_DOWN) then
        pitch = pitch + 45 * dt
    end

    -- Gamepad: right stick for camera orbit
    if ffe.isGamepadConnected(0) then
        local rx = ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_RIGHT_X)
        local ry = ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_RIGHT_Y)
        yaw   = yaw   + rx * GAMEPAD_SENSITIVITY * dt
        pitch = pitch + ry * GAMEPAD_SENSITIVITY * dt
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
    local camDirX = -math.sin(yawRad) * math.cos(pitchRad)
    local camDirY =  math.sin(pitchRad)
    local camDirZ = -math.cos(yawRad) * math.cos(pitchRad)

    -- Raycast from target in camera direction
    local hitEnt, hx, hy, hz, nx, ny, nz, hitDist =
        ffe.castRay(targetX, targetY, targetZ, camDirX, camDirY, camDirZ, radius)

    if hitEnt then
        -- Pull camera closer to avoid clipping (leave a small buffer)
        local buffer = 0.5
        effectiveRadius = math.max(ORBIT_MIN_RADIUS, hitDist - buffer)
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

ffe.log("[Camera] Module loaded")
