-- levels/test_level.lua -- Test/prototype level for "Echoes of the Ancients"
--
-- A simple test scene to validate the player controller, camera, combat,
-- and HUD before building full levels with real assets.
--
-- Contents:
--   Ground plane (large static box)
--   Directional light + 2 point lights
--   Fog (blueish gray)
--   Player spawn at origin
--   3 placeholder artifact entities (small colored boxes)
--   1 placeholder guardian entity (red box with patrol AI)
--
-- No external assets (.glb models) — uses primitive physics boxes
-- colored with ffe.setMeshColor. Real assets come in M2.

ffe.log("[TestLevel] Loading test level...")

--------------------------------------------------------------------
-- Load meshes: cube for structural geometry, real models for characters
--------------------------------------------------------------------
local cubeMesh = ffe.loadMesh("models/cube.glb")
if cubeMesh == 0 then
    ffe.log("[TestLevel] WARNING: cube.glb not found -- visual geometry will be missing")
    ffe.log("[TestLevel] Place a cube.glb at assets/models/cube.glb for full visuals")
end

-- Character / prop meshes (graceful fallback to cube if missing)
local foxMesh = ffe.loadMesh("models/fox.glb")
if foxMesh == 0 then foxMesh = cubeMesh end

local duckMesh = ffe.loadMesh("models/duck.glb")
if duckMesh == 0 then duckMesh = cubeMesh end

local cesiumMesh = ffe.loadMesh("models/cesium_man.glb")
if cesiumMesh == 0 then cesiumMesh = cubeMesh end

--------------------------------------------------------------------
-- Lighting
--------------------------------------------------------------------
local softwareRenderer = ffe.isSoftwareRenderer()

-- Directional light: warm golden-hour sun from upper-right
ffe.setLightDirection(0.8, -1.0, 0.5)
ffe.setLightColor(1.0, 0.92, 0.80)
if softwareRenderer then
    ffe.setAmbientColor(0.25, 0.25, 0.32)
else
    ffe.setAmbientColor(0.12, 0.12, 0.18)
end

-- Enable shadows (skip on software renderer -- depth FBOs may fail)
if not softwareRenderer then
    ffe.enableShadows(1024)
    ffe.setShadowBias(0.005)
    ffe.setShadowArea(30, 30, 0.1, 60)
end

-- Point light 0: warm torch-like light on one side
ffe.addPointLight(0, 8, 3, 4, 1.0, 0.7, 0.3, 15)

-- Point light 1: cool blue accent light on the other side
ffe.addPointLight(1, -6, 4, -5, 0.3, 0.5, 1.0, 12)

--------------------------------------------------------------------
-- Fog: blueish gray atmosphere
--------------------------------------------------------------------
ffe.setFog(0.55, 0.60, 0.70, 20.0, 80.0)

--------------------------------------------------------------------
-- Background color (visible if no skybox)
--------------------------------------------------------------------
ffe.setBackgroundColor(0.55, 0.60, 0.70)

--------------------------------------------------------------------
-- Ground plane: large flat static box
--------------------------------------------------------------------
local ground = 0
if cubeMesh ~= 0 then
    ground = ffe.createEntity3D(cubeMesh, 0, -1, 0)
    if ground ~= 0 then
        ffe.setTransform3D(ground, 0, -1, 0, 0, 0, 0, 40, 1.0, 40)
        ffe.setMeshColor(ground, 0.45, 0.55, 0.35, 1.0)  -- brighter green/brown ground
        ffe.setMeshSpecular(ground, 0.1, 0.1, 0.1, 8)
    end
end

-- Ground physics body (always create, even without mesh)
if ground ~= 0 then
    ffe.createPhysicsBody(ground, {
        shape       = "box",
        halfExtents = { 40, 0.5, 40 },
        motion      = "static",
    })
else
    -- Create a physics-only ground
    ground = ffe.createEntity3D(0, 0, -1, 0)
    if ground ~= 0 then
        ffe.setTransform3D(ground, 0, -1, 0, 0, 0, 0, 40, 0.5, 40)
        ffe.createPhysicsBody(ground, {
            shape       = "box",
            halfExtents = { 40, 0.5, 40 },
            motion      = "static",
        })
    end
end

--------------------------------------------------------------------
-- Some walls/obstacles for camera collision testing
--------------------------------------------------------------------
local function createWall(x, y, z, sx, sy, sz, r, g, b)
    if cubeMesh == 0 then return 0 end
    local wall = ffe.createEntity3D(cubeMesh, x, y, z)
    if wall ~= 0 then
        ffe.setTransform3D(wall, x, y, z, 0, 0, 0, sx, sy, sz)
        ffe.setMeshColor(wall, r, g, b, 1.0)
        ffe.setMeshSpecular(wall, 0.2, 0.2, 0.2, 16)
        ffe.createPhysicsBody(wall, {
            shape       = "box",
            halfExtents = { sx, sy, sz },
            motion      = "static",
        })
    end
    return wall
end

-- A few walls to make the space interesting
createWall(12, 2, 0,   0.5, 3, 8,   0.5, 0.45, 0.40)   -- east wall
createWall(-12, 2, 0,  0.5, 3, 8,   0.5, 0.45, 0.40)   -- west wall
createWall(0, 2, 12,   8, 3, 0.5,   0.5, 0.45, 0.40)    -- north wall
createWall(5, 1.5, 5,  1.5, 1.5, 0.5, 0.6, 0.55, 0.45)  -- obstacle

--------------------------------------------------------------------
-- Artifacts: 3 small colored boxes at known positions
--------------------------------------------------------------------
local artifacts = {}

local artifactPositions = {
    { x =  8, y = 0.5, z =  6 },
    { x = -7, y = 0.5, z =  4 },
    { x =  3, y = 0.5, z = -8 },
}

local artifactColors = {
    { 1.0, 0.85, 0.1 },  -- gold
    { 0.2, 0.9,  0.5 },  -- emerald
    { 0.6, 0.3,  1.0 },  -- purple
}

for i, pos in ipairs(artifactPositions) do
    if duckMesh ~= 0 then
        local art = ffe.createEntity3D(duckMesh, pos.x, pos.y, pos.z)
        if art ~= 0 then
            -- Duck model as artifact collectible, tinted with artifact color
            ffe.setTransform3D(art, pos.x, pos.y, pos.z, 0, 0, 0, 0.008, 0.008, 0.008)
            local c = artifactColors[i]
            ffe.setMeshColor(art, c[1], c[2], c[3], 1.0)
            ffe.setMeshSpecular(art, 1.0, 0.9, 0.5, 128)

            -- Kinematic physics body as trigger volume
            ffe.createPhysicsBody(art, {
                shape       = "box",
                halfExtents = { 0.5, 0.5, 0.5 },
                motion      = "kinematic",
            })

            artifacts[art] = { index = i, collected = false }
            ffe.log("[TestLevel] Artifact " .. tostring(i) .. " placed at ("
                .. tostring(pos.x) .. ", " .. tostring(pos.y) .. ", " .. tostring(pos.z) .. ")")
        end
    end
end

--------------------------------------------------------------------
-- Artifact rotation animation (in update via timer)
-- We store artifact entity IDs for per-frame rotation.
--------------------------------------------------------------------
local artifactEntities = {}
for entId, data in pairs(artifacts) do
    artifactEntities[#artifactEntities + 1] = entId
end

local artifactAngle = 0

--------------------------------------------------------------------
-- Guardian: 1 red box enemy with patrol waypoints
--------------------------------------------------------------------
local guardian = 0
if foxMesh ~= 0 then
    local gx, gy, gz = -5, 0.5, -3
    guardian = ffe.createEntity3D(foxMesh, gx, gy, gz)
    if guardian ~= 0 then
        -- Fox model -- scale to fit the scene
        ffe.setTransform3D(guardian, gx, gy, gz, 0, 0, 0, 0.03, 0.03, 0.03)
        ffe.setMeshColor(guardian, 0.85, 0.25, 0.15, 1.0)  -- fiery red tint
        ffe.setMeshSpecular(guardian, 0.4, 0.2, 0.2, 32)

        ffe.createPhysicsBody(guardian, {
            shape       = "box",
            halfExtents = { 0.7, 0.6, 1.0 },
            motion      = "dynamic",
            mass        = 2.0,
            restitution = 0.0,
            friction    = 0.8,
        })

        -- Register with AI system
        AI.create(guardian, {
            { x = -5, y = 0.5, z = -3 },
            { x =  5, y = 0.5, z = -3 },
            { x =  5, y = 0.5, z =  3 },
            { x = -5, y = 0.5, z =  3 },
        }, 75)
        AI.setEnemyColor(guardian, 0.85, 0.25, 0.15, 1.0)  -- fiery red
        -- Start walk animation if fox model has clips
        local animCount = ffe.getAnimationCount3D(guardian)
        if animCount > 0 then
            ffe.playAnimation3D(guardian, 0, true)
            ffe.setAnimationSpeed3D(guardian, 1.2)
        end

        ffe.log("[TestLevel] Guardian placed at ("
            .. tostring(gx) .. ", " .. tostring(gy) .. ", " .. tostring(gz) .. ")")
    end
end

--------------------------------------------------------------------
-- Collision callback: detect artifact pickups
--------------------------------------------------------------------
ffe.onCollision3D(function(entityA, entityB, px, py, pz, nx, ny, nz, eventType)
    if eventType ~= "enter" then return end

    local playerEnt = Player and Player.getEntity() or 0
    if playerEnt == 0 then return end

    -- Check if collision involves the player and an artifact
    local otherEnt = nil
    if entityA == playerEnt then otherEnt = entityB
    elseif entityB == playerEnt then otherEnt = entityA
    end

    if otherEnt and artifacts[otherEnt] and not artifacts[otherEnt].collected then
        artifacts[otherEnt].collected = true
        ffe.destroyPhysicsBody(otherEnt)
        ffe.destroyEntity(otherEnt)

        -- Remove from rotation list
        for i, ent in ipairs(artifactEntities) do
            if ent == otherEnt then
                table.remove(artifactEntities, i)
                break
            end
        end

        -- Notify game state
        if collectArtifact then collectArtifact() end

        ffe.log("[TestLevel] Artifact picked up!")
        ffe.cameraShake(0.3, 0.1)

        if HUD then
            HUD.showPrompt("Artifact collected!", 2.0)
        end
    end
end)

--------------------------------------------------------------------
-- Set level artifact count in the game state
--------------------------------------------------------------------
totalArtifacts = 3

--------------------------------------------------------------------
-- Player spawn
--------------------------------------------------------------------
Player.create(0, 2, 0, cesiumMesh)
Camera.setPosition(0, 2, 0)
Camera.setYawPitch(0, 20)

--------------------------------------------------------------------
-- Override the main update to include level-specific logic
-- We hook into the existing update by storing a level update function.
--------------------------------------------------------------------
local originalUpdate = update

-- Level-specific per-frame work: rotate artifacts, show prompts
local levelUpdateRegistered = false

-- We can't override update() here (it's owned by game.lua), so instead
-- we store a level tick function that game.lua can optionally call.
-- For the prototype, we piggyback on the AI.updateAll to also rotate artifacts.
-- A cleaner approach would be a level-tick callback system.

-- Use a timer to rotate artifacts every frame (ffe.every with tiny interval)
ffe.every(0.016, function()
    if getGameState and getGameState() ~= "PLAYING" then return end

    artifactAngle = artifactAngle + 90 * 0.016
    if artifactAngle > 360 then artifactAngle = artifactAngle - 360 end

    -- Rotate and bob each uncollected artifact (duck model scale)
    for _, entId in ipairs(artifactEntities) do
        local data = artifacts[entId]
        if data and not data.collected then
            local pos = artifactPositions[data.index]
            local bobY = pos.y + math.sin(math.rad(artifactAngle * 2)) * 0.3
            ffe.setTransform3D(entId, pos.x, bobY, pos.z,
                0, artifactAngle, 0,
                0.008, 0.008, 0.008)
        end
    end

    -- Show interaction prompt when near an artifact
    if Player then
        local px, py, pz = Player.getPosition()
        for _, entId in ipairs(artifactEntities) do
            local data = artifacts[entId]
            if data and not data.collected then
                local pos = artifactPositions[data.index]
                local dx = px - pos.x
                local dz = pz - pos.z
                local dist = math.sqrt(dx * dx + dz * dz)
                if dist < 3.0 and HUD then
                    HUD.showPrompt("Walk into the artifact to collect it", 0.1)
                end
            end
        end
    end
end)

ffe.log("[TestLevel] Test level loaded successfully!")
ffe.log("[TestLevel] Controls: WASD move, SPACE jump, LMB/Gamepad-X attack")
ffe.log("[TestLevel] Right-click + drag (or right stick) to orbit camera")
ffe.log("[TestLevel] Collect 3 artifacts, defeat the guardian!")
