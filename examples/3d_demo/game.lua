-- game.lua -- "3D Spinning Cubes" demo for FFE
--
-- Demonstrates the 3D Mesh Rendering API introduced in Phase 2:
--   ffe.loadMesh / ffe.unloadMesh
--   ffe.createEntity3D
--   ffe.setTransform3D
--   ffe.setMeshColor
--   ffe.set3DCamera
--   ffe.setLightDirection / ffe.setLightColor / ffe.setAmbientColor
--
-- Three mesh entities orbit the world origin at different speeds and radii,
-- each tinted a distinct color.  A 2D HUD (entity count, FPS, controls) is
-- drawn on top using the standard ffe.drawText / ffe.drawRect API, showing
-- how 3D and 2D coexist in the same frame.
--
-- Controls:
--   WASD          orbit camera left/right, zoom in/out
--   UP/DOWN       raise/lower camera elevation
--   R             reset camera to default position
--   ESC           quit
--
-- Asset path: "models/cube.glb" relative to the asset root.
-- If the file is absent (handle == 0) the demo logs a warning,
-- skips entity creation, and renders only the HUD -- so the
-- executable is useful as a smoke test even without assets.
--
-- Coordinate system: right-handed, Y-up.
-- Window: 1280 x 720.

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local ROTATION_SPEED_A =  45.0   -- degrees per second, red cube
local ROTATION_SPEED_B =  72.0   -- degrees per second, green cube
local ROTATION_SPEED_C = 120.0   -- degrees per second, blue cube

local ORBIT_RADIUS_A = 0.0       -- centre (stationary)
local ORBIT_RADIUS_B = 2.5       -- orbits the centre
local ORBIT_RADIUS_C = 4.0       -- wider orbit

local ORBIT_SPEED_B  = 30.0      -- degrees per second for orbit
local ORBIT_SPEED_C  = 20.0      -- degrees per second for orbit

-- Camera defaults
local CAM_DEFAULT_DIST = 8.0
local CAM_DEFAULT_ELEV = 4.0
local CAM_MOVE_SPEED   = 60.0    -- degrees per second (yaw)
local CAM_ZOOM_SPEED   = 3.0     -- units per second
local CAM_ELEV_SPEED   = 2.0     -- units per second
local CAM_MIN_DIST     = 2.0
local CAM_MAX_DIST     = 20.0
local CAM_MIN_ELEV     = -8.0
local CAM_MAX_ELEV     = 12.0

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------
local meshHandle   = 0        -- ffe.MeshHandle integer (0 = invalid)
local entityA      = 0        -- red cube  (centre, spins on Y)
local entityB      = 0        -- green cube (orbits + spins)
local entityC      = 0        -- blue cube  (wide orbit + spins)
local meshLoaded   = false    -- true when cube.glb loaded successfully

-- Per-entity rotation accumulators (degrees)
local rotA = 0.0
local rotB = 0.0
local rotC = 0.0

-- Orbit angle accumulators (degrees)
local orbitB = 0.0
local orbitC = 120.0   -- start offset so cubes are not bunched together

-- Camera state
local camYaw  = 45.0          -- horizontal angle around Y axis (degrees)
local camDist = CAM_DEFAULT_DIST
local camElev = CAM_DEFAULT_ELEV

-- FPS tracking
local fpsTimer    = 0.0
local fpsFrames   = 0
local fpsDisplay  = 0

-- ---------------------------------------------------------------------------
-- Helper: compute camera world position from yaw/dist/elevation
-- ---------------------------------------------------------------------------
local function updateCamera()
    local rad = math.rad(camYaw)
    local cx = math.sin(rad) * camDist
    local cz = math.cos(rad) * camDist
    -- Camera looks at the world origin
    ffe.set3DCamera(cx, camElev, cz,   0, 0, 0)
end

-- ---------------------------------------------------------------------------
-- Scene init
-- ---------------------------------------------------------------------------

-- Set a dark, moody sky colour that contrasts with the lit cubes.
ffe.setBackgroundColor(0.05, 0.05, 0.12)

-- Configure scene lighting:
--   A warm directional light from the upper-right front,
--   plus a cool ambient fill so shadow faces are not pitch-black.
ffe.setLightDirection(1.0, -1.5, 0.8)
ffe.setLightColor(1.0, 0.92, 0.78)       -- warm white / golden hour
ffe.setAmbientColor(0.08, 0.08, 0.18)    -- cool blue-grey fill

-- Position the camera at default location.
updateCamera()

-- Load the cube mesh from the asset root.
-- "cube.glb" lives at <assetRoot>/models/cube.glb.
meshHandle = ffe.loadMesh("models/cube.glb")

if meshHandle == 0 then
    -- ffe.loadMesh already logged the specific error.
    -- The demo continues; only the HUD will be visible.
    ffe.log("WARNING: cube.glb not found -- running in HUD-only mode")
    ffe.log("         Place a cube.glb at assets/models/cube.glb to see 3D geometry")
else
    ffe.log("cube.glb loaded (handle=" .. tostring(meshHandle) .. ")")

    -- -----------------------------------------------------------------
    -- Entity A: red cube at the origin, spins on Y axis only
    -- -----------------------------------------------------------------
    entityA = ffe.createEntity3D(meshHandle, 0, 0, 0)
    if entityA ~= 0 then
        ffe.setMeshColor(entityA, 0.9, 0.25, 0.20, 1.0)   -- red tint
        ffe.setTransform3D(entityA, 0, 0, 0,  0, 0, 0,  1, 1, 1)
        ffe.log("Entity A (red)   created id=" .. tostring(entityA))
    end

    -- -----------------------------------------------------------------
    -- Entity B: green cube, orbits the origin at radius 2.5
    -- -----------------------------------------------------------------
    entityB = ffe.createEntity3D(meshHandle, ORBIT_RADIUS_B, 0, 0)
    if entityB ~= 0 then
        ffe.setMeshColor(entityB, 0.20, 0.85, 0.35, 1.0)  -- green tint
        ffe.setTransform3D(entityB, ORBIT_RADIUS_B, 0, 0,  0, 0, 0,  0.8, 0.8, 0.8)
        ffe.log("Entity B (green) created id=" .. tostring(entityB))
    end

    -- -----------------------------------------------------------------
    -- Entity C: blue cube, wider orbit, smaller scale
    -- -----------------------------------------------------------------
    entityC = ffe.createEntity3D(meshHandle, ORBIT_RADIUS_C, 0, 0)
    if entityC ~= 0 then
        ffe.setMeshColor(entityC, 0.25, 0.50, 1.00, 1.0)  -- blue tint
        ffe.setTransform3D(entityC, ORBIT_RADIUS_C, 0, 0,  0, 0, 0,  0.6, 0.6, 0.6)
        ffe.log("Entity C (blue)  created id=" .. tostring(entityC))
    end

    meshLoaded = (entityA ~= 0)
end

ffe.log("3D demo init complete. WASD camera, UP/DOWN elevation, R reset, ESC quit")

-- ---------------------------------------------------------------------------
-- update(entityId, dt) -- called per tick by the C++ host
--
-- entityId: the host-created entity (unused in this demo -- we manage our own)
-- dt:       delta time in seconds
-- ---------------------------------------------------------------------------
function update(entityId, dt)

    -- ------------------------------------------------------------------
    -- FPS counter
    -- ------------------------------------------------------------------
    fpsFrames = fpsFrames + 1
    fpsTimer  = fpsTimer  + dt
    if fpsTimer >= 0.5 then
        fpsDisplay = math.floor(fpsFrames / fpsTimer + 0.5)
        fpsFrames  = 0
        fpsTimer   = 0.0
    end

    -- ------------------------------------------------------------------
    -- Camera controls
    -- ------------------------------------------------------------------
    if ffe.isKeyHeld(ffe.KEY_A) then
        camYaw = camYaw - CAM_MOVE_SPEED * dt
    end
    if ffe.isKeyHeld(ffe.KEY_D) then
        camYaw = camYaw + CAM_MOVE_SPEED * dt
    end
    if ffe.isKeyHeld(ffe.KEY_W) then
        camDist = math.max(CAM_MIN_DIST, camDist - CAM_ZOOM_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_S) then
        camDist = math.min(CAM_MAX_DIST, camDist + CAM_ZOOM_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_UP) then
        camElev = math.min(CAM_MAX_ELEV, camElev + CAM_ELEV_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_DOWN) then
        camElev = math.max(CAM_MIN_ELEV, camElev - CAM_ELEV_SPEED * dt)
    end
    if ffe.isKeyPressed(ffe.KEY_R) then
        camYaw  = 45.0
        camDist = CAM_DEFAULT_DIST
        camElev = CAM_DEFAULT_ELEV
    end

    updateCamera()

    -- ------------------------------------------------------------------
    -- Animate 3D entities
    -- ------------------------------------------------------------------
    if meshLoaded then

        -- Advance rotation accumulators
        rotA   = rotA   + ROTATION_SPEED_A * dt
        rotB   = rotB   + ROTATION_SPEED_B * dt
        rotC   = rotC   + ROTATION_SPEED_C * dt
        orbitB = orbitB + ORBIT_SPEED_B    * dt
        orbitC = orbitC + ORBIT_SPEED_C    * dt

        -- Wrap angles to prevent unbounded growth over long sessions.
        -- Using modulo 360 is safe for degree values.
        if rotA   > 360 then rotA   = rotA   - 360 end
        if rotB   > 360 then rotB   = rotB   - 360 end
        if rotC   > 360 then rotC   = rotC   - 360 end
        if orbitB > 360 then orbitB = orbitB - 360 end
        if orbitC > 360 then orbitC = orbitC - 360 end

        -- Entity A: stationary at origin, spins on Y only
        if entityA ~= 0 then
            ffe.setTransform3D(entityA,
                0, 0, 0,              -- position
                0, rotA, 0,           -- rotation (ry = Y spin)
                1, 1, 1)              -- scale (always explicit -- see "What NOT to Do")
        end

        -- Entity B: orbits origin in the XZ plane, also tumbles on its local axes
        if entityB ~= 0 then
            local radB = math.rad(orbitB)
            local bx = math.sin(radB) * ORBIT_RADIUS_B
            local bz = math.cos(radB) * ORBIT_RADIUS_B
            ffe.setTransform3D(entityB,
                bx, 0, bz,            -- position on orbit
                rotB, rotB * 0.5, 0,  -- tumble: rx + slower ry
                0.8, 0.8, 0.8)        -- scale
        end

        -- Entity C: wider orbit at a slight Y elevation, fast self-spin
        if entityC ~= 0 then
            local radC = math.rad(orbitC)
            local cx = math.sin(radC) * ORBIT_RADIUS_C
            local cz = math.cos(radC) * ORBIT_RADIUS_C
            -- Bob gently on Y using a sine wave
            local cy = math.sin(math.rad(orbitC * 2)) * 0.5
            ffe.setTransform3D(entityC,
                cx, cy, cz,           -- position (bobs on Y)
                0, rotC, rotC * 0.3,  -- ry spin + slight rz roll
                0.6, 0.6, 0.6)        -- scale
        end

    end

    -- ------------------------------------------------------------------
    -- 2D HUD overlay
    -- ------------------------------------------------------------------
    local sw = ffe.getScreenWidth()

    -- Title bar
    ffe.drawRect(0, 0, sw, 36, 0, 0, 0, 0.55)
    ffe.drawText("FFE  3D DEMO", 12, 8, 2, 1, 0.85, 0.3, 1)

    -- FPS counter (right-aligned)
    local fpsStr = "FPS: " .. tostring(fpsDisplay)
    ffe.drawText(fpsStr, sw - (#fpsStr * 8 * 2) - 12, 8, 2, 0.4, 0.9, 0.4, 1)

    -- Status: mesh load result
    if meshLoaded then
        local entStr = "Entities: 3  |  Mesh: cube.glb"
        ffe.drawText(entStr, sw / 2 - (#entStr * 8), 8, 2, 0.6, 0.8, 1.0, 0.85)
    else
        ffe.drawText("cube.glb NOT FOUND -- HUD only mode", 12, 44, 2, 1, 0.4, 0.2, 1)
    end

    -- Control hint bar at bottom
    ffe.drawRect(0, 692, sw, 28, 0, 0, 0, 0.5)
    ffe.drawText(
        "WASD: orbit/zoom  |  UP/DOWN: elevation  |  R: reset  |  ESC: quit",
        12, 697, 2, 0.45, 0.55, 0.65, 0.9)

    -- Color key for the three cubes
    if meshLoaded then
        ffe.drawText("RED: stationary", 12, 44, 2, 0.9, 0.3, 0.2, 1)
        ffe.drawText("GREEN: inner orbit", 12, 68, 2, 0.2, 0.85, 0.35, 1)
        ffe.drawText("BLUE: outer orbit", 12, 92, 2, 0.3, 0.55, 1.0, 1)
    end

    -- ------------------------------------------------------------------
    -- ESC to quit
    -- ------------------------------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end

-- ---------------------------------------------------------------------------
-- shutdown() -- called by ScriptEngine before lua_close()
-- ---------------------------------------------------------------------------
function shutdown()
    -- Unload the mesh.  ffe.unloadMesh is a no-op when meshHandle == 0,
    -- but we guard anyway for clarity.
    if meshHandle ~= 0 then
        ffe.unloadMesh(meshHandle)
        meshHandle = 0
        ffe.log("cube.glb unloaded")
    end

    -- Reset entity IDs so stale values are not accidentally reused.
    entityA = 0
    entityB = 0
    entityC = 0

    ffe.log("3D demo shutdown complete")
end
