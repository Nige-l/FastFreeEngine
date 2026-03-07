-- levels/level1.lua -- "The Courtyard" (Level 1) for "Echoes of the Ancients"
--
-- An overgrown stone courtyard with crumbling walls, a central fountain,
-- wall-mounted torches with point lights, a push-block puzzle, two
-- guardian enemies, and scattered gem collectibles.
--
-- Entity budget: ~55 mesh entities, 4 point lights, ~25 physics bodies
-- Well within LEGACY-tier 60 fps budget.

ffe.log("[Level1] Loading The Courtyard...")

--------------------------------------------------------------------
-- Load meshes: cube for structural geometry, real models for characters/props
--------------------------------------------------------------------
local cubeMesh = ffe.loadMesh("models/cube.glb")
if cubeMesh == 0 then
    ffe.log("[Level1] WARNING: cube.glb not found -- geometry will be missing")
    ffe.log("[Level1] Place cube.glb at assets/models/cube.glb")
end

-- Character / prop meshes (graceful fallback to cube if missing)
local helmetMesh   = ffe.loadMesh("models/damaged_helmet.glb")
if helmetMesh == 0 then helmetMesh = cubeMesh end

local foxMesh      = ffe.loadMesh("models/fox.glb")
if foxMesh == 0 then foxMesh = cubeMesh end

local duckMesh     = ffe.loadMesh("models/duck.glb")
if duckMesh == 0 then duckMesh = cubeMesh end

local figureMesh   = ffe.loadMesh("models/rigged_figure.glb")
if figureMesh == 0 then figureMesh = cubeMesh end

local cesiumMesh   = ffe.loadMesh("models/cesium_man.glb")
if cesiumMesh == 0 then cesiumMesh = cubeMesh end

ffe.log("[Level1] Meshes loaded: helmet=" .. tostring(helmetMesh)
    .. " fox=" .. tostring(foxMesh) .. " duck=" .. tostring(duckMesh)
    .. " figure=" .. tostring(figureMesh) .. " cesium=" .. tostring(cesiumMesh))

--------------------------------------------------------------------
-- Load audio assets (optional -- game plays fine without audio)
--------------------------------------------------------------------
local sfxCollect = ffe.loadSound("audio/sfx_collect.wav")
local sfxHit     = ffe.loadSound("audio/sfx_hit.wav")
local sfxGate    = ffe.loadSound("audio/sfx_gate.wav")
local musicHandle = ffe.loadMusic("audio/BattleMusic.mp3")

-- Start background music (with fallback logging, Bug 4)
if musicHandle and musicHandle ~= 0 then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.35)
    ffe.log("[Level1] Music playing: audio/BattleMusic.mp3")
else
    ffe.log("[Level1] WARNING: Could not load music (handle=" .. tostring(musicHandle) .. ")")
    ffe.log("[Level1] Expected file at: assets/audio/BattleMusic.mp3")
end

--------------------------------------------------------------------
-- Post-processing: bloom, tone mapping, SSAO, FXAA
--------------------------------------------------------------------
ffe.enablePostProcessing()
ffe.enableBloom(0.8, 0.3)
ffe.setToneMapping(2)  -- ACES filmic
ffe.enableSSAO()
ffe.setAntiAliasing(2)  -- FXAA

--------------------------------------------------------------------
-- Lighting: dramatic angled sunlight with cool ambient
--------------------------------------------------------------------
ffe.setLightDirection(0.3, -0.8, 0.5)
ffe.setLightColor(1.0, 0.95, 0.85)       -- warm sunlight
ffe.setAmbientColor(0.15, 0.18, 0.25)    -- cool ambient (blueish shadows)

-- Enable shadows
ffe.enableShadows(1024)
ffe.setShadowBias(0.005)
ffe.setShadowArea(40, 40, 0.1, 80)

--------------------------------------------------------------------
-- Fog: soft blue-grey outdoor atmosphere
--------------------------------------------------------------------
ffe.setFog(0.6, 0.65, 0.75, 20.0, 80.0)
ffe.setBackgroundColor(0.6, 0.65, 0.75)

--------------------------------------------------------------------
-- Terrain: gently rolling courtyard heightmap
--------------------------------------------------------------------
ffe.loadTerrain("terrain/courtyard_height.png", 60, 60, 8)

--------------------------------------------------------------------
-- Helper: create a static box entity with mesh, color, and physics
--------------------------------------------------------------------
local function createStaticBox(x, y, z, sx, sy, sz, r, g, b, rx, ry, rz)
    rx = rx or 0
    ry = ry or 0
    rz = rz or 0
    local ent = 0
    if cubeMesh ~= 0 then
        ent = ffe.createEntity3D(cubeMesh, x, y, z)
        if ent ~= 0 then
            ffe.setTransform3D(ent, x, y, z, rx, ry, rz, sx, sy, sz)
            ffe.setMeshColor(ent, r, g, b, 1.0)
            ffe.setMeshSpecular(ent, 0.15, 0.15, 0.15, 16)
            ffe.createPhysicsBody(ent, {
                shape       = "box",
                halfExtents = { sx * 0.5, sy * 0.5, sz * 0.5 },
                motion      = "static",
            })
        end
    end
    return ent
end

--------------------------------------------------------------------
-- Helper: create a dynamic box entity with mesh, color, and physics
--------------------------------------------------------------------
local function createDynamicBox(x, y, z, sx, sy, sz, r, g, b, mass)
    local ent = 0
    if cubeMesh ~= 0 then
        ent = ffe.createEntity3D(cubeMesh, x, y, z)
        if ent ~= 0 then
            ffe.setTransform3D(ent, x, y, z, 0, 0, 0, sx, sy, sz)
            ffe.setMeshColor(ent, r, g, b, 1.0)
            ffe.setMeshSpecular(ent, 0.2, 0.2, 0.2, 16)
            ffe.createPhysicsBody(ent, {
                shape       = "box",
                halfExtents = { sx * 0.5, sy * 0.5, sz * 0.5 },
                motion      = "dynamic",
                mass        = mass or 1.0,
                restitution = 0.0,
                friction    = 1.0,
            })
        end
    end
    return ent
end

--------------------------------------------------------------------
-- Ground plane: replaced by terrain heightmap (see loadTerrain above)
-- The terrain center is flat, so entities at Y=0 remain compatible.
--------------------------------------------------------------------

--------------------------------------------------------------------
-- Edge border walls: low walls at ground perimeter for spatial reference (Bug 2)
--------------------------------------------------------------------
-- North edge
createStaticBox(0, 0.25, 30,  30, 0.25, 0.3,  0.35, 0.32, 0.28)
-- South edge
createStaticBox(0, 0.25, -30, 30, 0.25, 0.3,  0.35, 0.32, 0.28)
-- East edge
createStaticBox(30, 0.25, 0,  0.3, 0.25, 30,  0.35, 0.32, 0.28)
-- West edge
createStaticBox(-30, 0.25, 0, 0.3, 0.25, 30,  0.35, 0.32, 0.28)

--------------------------------------------------------------------
-- Perimeter walls (4 segments with gaps for entrance/exit)
--------------------------------------------------------------------
-- South wall (entrance side): two segments with gap in center
createStaticBox(-12.5, 2, -20,  7.5, 2.5, 0.5,  0.5, 0.48, 0.42)  -- left segment
createStaticBox( 12.5, 2, -20,  7.5, 2.5, 0.5,  0.5, 0.48, 0.42)  -- right segment

-- North wall (exit side): two segments with gap in center for gate
createStaticBox(-12.5, 2, 20,  7.5, 2.5, 0.5,  0.5, 0.48, 0.42)
createStaticBox( 12.5, 2, 20,  7.5, 2.5, 0.5,  0.5, 0.48, 0.42)

-- East wall (solid)
createStaticBox(20, 2, 0,  0.5, 2.5, 20,  0.5, 0.48, 0.42)

-- West wall (solid)
createStaticBox(-20, 2, 0,  0.5, 2.5, 20,  0.5, 0.48, 0.42)

--------------------------------------------------------------------
-- Entrance archway pillars (south gap decoration)
--------------------------------------------------------------------
createStaticBox(-5.5, 2, -20,  0.6, 3, 0.6,  0.55, 0.5, 0.45)
createStaticBox( 5.5, 2, -20,  0.6, 3, 0.6,  0.55, 0.5, 0.45)
-- Arch lintel above entrance
createStaticBox(0, 4.5, -20,  6, 0.4, 0.7,  0.55, 0.5, 0.45)

--------------------------------------------------------------------
-- Exit gate area (north gap)
--------------------------------------------------------------------
-- Gate pillars
createStaticBox(-5.5, 2, 20,  0.6, 3, 0.6,  0.55, 0.5, 0.45)
createStaticBox( 5.5, 2, 20,  0.6, 3, 0.6,  0.55, 0.5, 0.45)

-- The gate itself: tall red box that rises when puzzle is solved
local gateEntity = 0
local gateY      = 2.0       -- initial Y (blocking)
local gateOpen   = false
local gateTargetY = 2.0

if cubeMesh ~= 0 then
    gateEntity = ffe.createEntity3D(cubeMesh, 0, gateY, 20)
    if gateEntity ~= 0 then
        ffe.setTransform3D(gateEntity, 0, gateY, 20, 0, 0, 0, 4.5, 2.5, 0.4)
        ffe.setMeshColor(gateEntity, 0.7, 0.2, 0.15, 1.0)
        ffe.setMeshSpecular(gateEntity, 0.4, 0.2, 0.2, 32)
        ffe.createPhysicsBody(gateEntity, {
            shape       = "box",
            halfExtents = { 4.5, 2.5, 0.4 },
            motion      = "kinematic",
        })
    end
end

--------------------------------------------------------------------
-- Corner pillars (4 decorative stone pillars)
--------------------------------------------------------------------
createStaticBox(-15, 2.5, -15,  0.7, 3, 0.7,  0.52, 0.5, 0.45)
createStaticBox( 15, 2.5, -15,  0.7, 3, 0.7,  0.52, 0.5, 0.45)
createStaticBox(-15, 2.5,  15,  0.7, 3, 0.7,  0.52, 0.5, 0.45)
createStaticBox( 15, 2.5,  15,  0.7, 3, 0.7,  0.52, 0.5, 0.45)

--------------------------------------------------------------------
-- Central fountain (decorative stack of boxes + statue figure on top)
--------------------------------------------------------------------
-- Base pool
createStaticBox(0, 0.3, 0,  3, 0.3, 3,  0.45, 0.5, 0.55)
-- Inner rim
createStaticBox(0, 0.7, 0,  2.2, 0.1, 2.2,  0.5, 0.55, 0.6)
-- Central column
createStaticBox(0, 1.5, 0,  0.4, 1.2, 0.4,  0.55, 0.55, 0.6)
-- Top basin
createStaticBox(0, 2.5, 0,  1.0, 0.15, 1.0,  0.5, 0.52, 0.58)
-- Statue figure atop the fountain (rigged figure as ancient statue)
if figureMesh ~= 0 then
    local statue = ffe.createEntity3D(figureMesh, 0, 2.8, 0)
    if statue ~= 0 then
        ffe.setTransform3D(statue, 0, 2.8, 0, 0, 0, 0, 0.6, 0.6, 0.6)
        ffe.setMeshColor(statue, 0.55, 0.6, 0.5, 1.0)  -- weathered bronze
        ffe.setMeshSpecular(statue, 0.5, 0.5, 0.4, 64)
    end
end

--------------------------------------------------------------------
-- Decorative rubble / obstacles around the courtyard
--------------------------------------------------------------------
createStaticBox( 8, 0.4, -8,  1.2, 0.4, 0.8,  0.45, 0.42, 0.38)
createStaticBox(-10, 0.3, 6,  0.8, 0.3, 1.0,  0.43, 0.4, 0.36)
createStaticBox( 12, 0.5, 10, 0.6, 0.5, 0.6,  0.42, 0.4, 0.35)

--------------------------------------------------------------------
-- Destructible wall section (hides the main artifact)
-- 3 hits to destroy. We track hits on this entity.
--------------------------------------------------------------------
local destructWall = 0
local destructWallHP = 3

if cubeMesh ~= 0 then
    destructWall = ffe.createEntity3D(cubeMesh, -16, 1.5, 8)
    if destructWall ~= 0 then
        ffe.setTransform3D(destructWall, -16, 1.5, 8, 0, 0, 0, 1.5, 1.5, 0.5)
        ffe.setMeshColor(destructWall, 0.6, 0.45, 0.35, 1.0)
        ffe.setMeshSpecular(destructWall, 0.1, 0.1, 0.1, 8)
        ffe.createPhysicsBody(destructWall, {
            shape       = "box",
            halfExtents = { 1.5, 1.5, 0.5 },
            motion      = "static",
        })
    end
end

--------------------------------------------------------------------
-- Wall-mounted torch positions (with point lights)
--------------------------------------------------------------------
-- Torch sconce visual entities (small boxes on walls)
local torchPositions = {
    { x = -19.5, y = 3.0, z = -8 },   -- west wall
    { x = -19.5, y = 3.0, z =  8 },   -- west wall
    { x =  19.5, y = 3.0, z = -8 },   -- east wall
    { x =  19.5, y = 3.0, z =  8 },   -- east wall
}

for i, tp in ipairs(torchPositions) do
    -- Torch bracket (small box)
    if cubeMesh ~= 0 then
        local torch = ffe.createEntity3D(cubeMesh, tp.x, tp.y, tp.z)
        if torch ~= 0 then
            ffe.setTransform3D(torch, tp.x, tp.y, tp.z, 0, 0, 0, 0.15, 0.3, 0.15)
            ffe.setMeshColor(torch, 0.3, 0.25, 0.15, 1.0)
        end
    end

    -- Point light at torch position (warm orange glow)
    -- We have 4 point light slots: indices 0-3
    ffe.addPointLight(i - 1, tp.x, tp.y + 0.5, tp.z,  1.0, 0.65, 0.25, 10)
end

--------------------------------------------------------------------
-- Push-block puzzle: 2 blocks, 2 pressure plates
--------------------------------------------------------------------
-- Pressure plate target zones (defined as coordinate regions)
local pressurePlates = {
    { x =  6, z = 16, halfSize = 1.2 },   -- right plate, near north wall
    { x = -6, z = 16, halfSize = 1.2 },   -- left plate, near north wall
}

-- Visual markers for pressure plates (flat colored boxes on ground)
for _, plate in ipairs(pressurePlates) do
    if cubeMesh ~= 0 then
        local marker = ffe.createEntity3D(cubeMesh, plate.x, 0.05, plate.z)
        if marker ~= 0 then
            ffe.setTransform3D(marker, plate.x, 0.05, plate.z,
                               0, 0, 0, plate.halfSize, 0.05, plate.halfSize)
            ffe.setMeshColor(marker, 0.8, 0.6, 0.15, 1.0)  -- gold/amber indicator
            ffe.setMeshSpecular(marker, 0.5, 0.4, 0.1, 64)
        end
    end
end

-- Pushable blocks: heavy dynamic boxes
local pushBlock1 = createDynamicBox( 8, 0.8, 5,  0.8, 0.8, 0.8,  0.55, 0.52, 0.48, 8.0)
local pushBlock2 = createDynamicBox(-8, 0.8, 5,  0.8, 0.8, 0.8,  0.55, 0.52, 0.48, 8.0)

-- Track puzzle state
local puzzleSolved = false
local blockOnPlate = { false, false }

--------------------------------------------------------------------
-- Helper: check if a block entity is on a pressure plate
--------------------------------------------------------------------
local tbufBlock = {}

local function isBlockOnPlate(blockEntity, plate)
    if blockEntity == 0 then return false end
    ffe.fillTransform3D(blockEntity, tbufBlock)
    local bx = tbufBlock.x or 0
    local bz = tbufBlock.z or 0
    local dx = math.abs(bx - plate.x)
    local dz = math.abs(bz - plate.z)
    return dx < plate.halfSize and dz < plate.halfSize
end

--------------------------------------------------------------------
-- Guardians: 2 enemies with patrol routes
--------------------------------------------------------------------
-- Reset AI for fresh level load
AI.reset()

-- Guardian 1: patrols east side (fox model -- menacing beast!)
local guardian1 = 0
if foxMesh ~= 0 then
    local gx, gy, gz = 10, 0.8, -5
    guardian1 = ffe.createEntity3D(foxMesh, gx, gy, gz)
    if guardian1 ~= 0 then
        -- Fox model is small; scale up to make it imposing
        ffe.setTransform3D(guardian1, gx, gy, gz, 0, 0, 0, 0.03, 0.03, 0.03)
        ffe.setMeshColor(guardian1, 0.85, 0.25, 0.15, 1.0)  -- fiery red tint
        ffe.setMeshSpecular(guardian1, 0.4, 0.15, 0.15, 32)
        ffe.createPhysicsBody(guardian1, {
            shape       = "box",
            halfExtents = { 0.7, 0.65, 1.0 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian1, {
            { x = 10, y = 0.8, z = -10 },
            { x = 15, y = 0.8, z =  -3 },
            { x = 10, y = 0.8, z =   5 },
            { x = 15, y = 0.8, z =  10 },
        }, 80)
    end
end

-- Guardian 2: patrols west side (fox model)
local guardian2 = 0
if foxMesh ~= 0 then
    local gx, gy, gz = -10, 0.8, 5
    guardian2 = ffe.createEntity3D(foxMesh, gx, gy, gz)
    if guardian2 ~= 0 then
        ffe.setTransform3D(guardian2, gx, gy, gz, 0, 0, 0, 0.03, 0.03, 0.03)
        ffe.setMeshColor(guardian2, 0.85, 0.25, 0.15, 1.0)  -- fiery red tint
        ffe.setMeshSpecular(guardian2, 0.4, 0.15, 0.15, 32)
        ffe.createPhysicsBody(guardian2, {
            shape       = "box",
            halfExtents = { 0.7, 0.65, 1.0 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian2, {
            { x = -10, y = 0.8, z = 10 },
            { x = -15, y = 0.8, z =  3 },
            { x = -10, y = 0.8, z = -5 },
            { x = -15, y = 0.8, z = -10 },
        }, 80)
    end
end

--------------------------------------------------------------------
-- Artifacts / collectibles
--------------------------------------------------------------------
-- Main artifact: ancient damaged helmet behind the destructible wall (west side)
local mainArtifact = 0
local mainArtifactPos = { x = -18, y = 1.0, z = 8 }

if helmetMesh ~= 0 then
    mainArtifact = ffe.createEntity3D(helmetMesh,
        mainArtifactPos.x, mainArtifactPos.y, mainArtifactPos.z)
    if mainArtifact ~= 0 then
        -- Damaged helmet model -- scale to look like a relic on a pedestal
        ffe.setTransform3D(mainArtifact,
            mainArtifactPos.x, mainArtifactPos.y, mainArtifactPos.z,
            0, 0, 0, 0.8, 0.8, 0.8)
        ffe.setMeshColor(mainArtifact, 1.0, 0.85, 0.1, 1.0)
        ffe.setMeshSpecular(mainArtifact, 1.0, 0.9, 0.4, 128)
        ffe.createPhysicsBody(mainArtifact, {
            shape       = "box",
            halfExtents = { 0.5, 0.5, 0.5 },
            motion      = "kinematic",
        })
    end
end

-- Optional gem pickups: 3 scattered around the courtyard
local gems = {}
local gemPositions = {
    { x =  12, y = 0.6, z = -12 },  -- SE corner
    { x = -12, y = 0.6, z = -12 },  -- SW corner
    { x =   0, y = 1.2, z =   8 },  -- near fountain, north
}
local gemColors = {
    { 0.2, 0.9, 0.4 },   -- emerald
    { 0.6, 0.3, 1.0 },   -- amethyst
    { 0.1, 0.6, 1.0 },   -- sapphire
}

for i, pos in ipairs(gemPositions) do
    if duckMesh ~= 0 then
        local gem = ffe.createEntity3D(duckMesh, pos.x, pos.y, pos.z)
        if gem ~= 0 then
            -- Duck model as gem collectible -- tinted with gem colors
            ffe.setTransform3D(gem, pos.x, pos.y, pos.z, 0, 0, 0, 0.008, 0.008, 0.008)
            local c = gemColors[i]
            ffe.setMeshColor(gem, c[1], c[2], c[3], 1.0)
            ffe.setMeshSpecular(gem, 0.8, 0.8, 0.8, 128)
            ffe.createPhysicsBody(gem, {
                shape       = "box",
                halfExtents = { 0.35, 0.35, 0.35 },
                motion      = "kinematic",
            })
            gems[gem] = { index = i, collected = false, pos = pos }
        end
    end
end

-- Track all artifact entities for rotation animation
local artifactEntities = {}
if mainArtifact ~= 0 then
    artifactEntities[#artifactEntities + 1] = {
        entity = mainArtifact,
        pos    = mainArtifactPos,
        scale  = 0.8,       -- helmet model scale
        isMain = true,
    }
end
for entId, data in pairs(gems) do
    artifactEntities[#artifactEntities + 1] = {
        entity = entId,
        pos    = data.pos,
        scale  = 0.008,     -- duck model scale
        isMain = false,
    }
end

--------------------------------------------------------------------
-- Level state
--------------------------------------------------------------------
local artifactAngle  = 0
local gemsCollected  = 0
local totalGems      = 3
local mainCollected  = false
local levelComplete  = false
local levelCompleteTimer = 0

-- Set global artifact count for game.lua
totalArtifacts = 1  -- only the main artifact is needed

--------------------------------------------------------------------
-- Collision callback: artifact/gem pickups
--------------------------------------------------------------------
ffe.onCollision3D(function(entityA, entityB, px, py, pz, nx, ny, nz, eventType)
    if eventType ~= "enter" then return end

    local playerEnt = Player and Player.getEntity() or 0
    if playerEnt == 0 then return end

    -- Determine which entity is not the player
    local otherEnt = nil
    if entityA == playerEnt then otherEnt = entityB
    elseif entityB == playerEnt then otherEnt = entityA
    end
    if not otherEnt then return end

    -- Check if it is the main artifact
    if otherEnt == mainArtifact and not mainCollected then
        mainCollected = true
        ffe.destroyPhysicsBody(otherEnt)
        ffe.destroyEntity(otherEnt)
        mainArtifact = 0

        -- Remove from rotation list
        for i, ad in ipairs(artifactEntities) do
            if ad.entity == otherEnt then
                table.remove(artifactEntities, i)
                break
            end
        end

        if sfxCollect then ffe.playSound(sfxCollect, 1.0) end
        ffe.cameraShake(0.5, 0.15)
        if collectArtifact then collectArtifact() end
        if HUD then HUD.showPrompt("Ancient Artifact acquired!", 3.0) end
        ffe.log("[Level1] Main artifact collected!")
        return
    end

    -- Check if it is a gem
    if gems[otherEnt] and not gems[otherEnt].collected then
        gems[otherEnt].collected = true
        ffe.destroyPhysicsBody(otherEnt)
        ffe.destroyEntity(otherEnt)

        -- Remove from rotation list
        for i, ad in ipairs(artifactEntities) do
            if ad.entity == otherEnt then
                table.remove(artifactEntities, i)
                break
            end
        end

        gemsCollected = gemsCollected + 1
        if sfxCollect then ffe.playSound(sfxCollect, 0.8) end
        ffe.cameraShake(0.3, 0.1)
        if HUD then
            HUD.showPrompt("Gem collected! (" .. tostring(gemsCollected)
                .. "/" .. tostring(totalGems) .. ")", 2.0)
        end
        ffe.log("[Level1] Gem collected: " .. tostring(gemsCollected)
            .. "/" .. tostring(totalGems))
        return
    end
end)

--------------------------------------------------------------------
-- Combat: destructible wall handling
-- Override the AI damage function temporarily to also handle
-- the destructible wall via combat raycast
--------------------------------------------------------------------
local origCombatAttack = Combat.attack

Combat.attack = function(playerPos, playerForward)
    -- Run normal combat logic
    local hitEntity = origCombatAttack(playerPos, playerForward)

    -- Also check if we hit the destructible wall
    if hitEntity and hitEntity == destructWall and destructWallHP > 0 then
        destructWallHP = destructWallHP - 1
        if sfxHit then ffe.playSound(sfxHit, 0.7) end
        ffe.cameraShake(0.4, 0.1)

        -- Flash the wall on hit
        ffe.setMeshColor(destructWall, 1, 0.8, 0.6, 1.0)
        ffe.after(0.1, function()
            if destructWall ~= 0 and destructWallHP > 0 then
                ffe.setMeshColor(destructWall, 0.6, 0.45, 0.35, 1.0)
            end
        end)

        ffe.log("[Level1] Destructible wall hit! HP: " .. tostring(destructWallHP))

        if destructWallHP <= 0 then
            -- Destroy the wall segment
            ffe.destroyPhysicsBody(destructWall)
            ffe.destroyEntity(destructWall)
            destructWall = 0
            if HUD then HUD.showPrompt("The wall crumbles!", 2.5) end
            ffe.log("[Level1] Destructible wall destroyed!")
        end
    end

    return hitEntity
end

--------------------------------------------------------------------
-- Player spawn (south side of courtyard, at the entrance)
--------------------------------------------------------------------
local spawnTerrainY = ffe.getTerrainHeight(0, -17)
Player.create(0, spawnTerrainY + 1.5, -17, cesiumMesh)
Camera.setPosition(0, spawnTerrainY + 1.5, -17)
Camera.setYawPitch(0, 35)  -- Pitch down to see the ground on spawn (Bug 2)

if HUD then
    HUD.showPrompt("The Courtyard -- Find the Ancient Artifact", 4.0)
end

--------------------------------------------------------------------
-- Per-frame level tick: puzzle logic, artifact animation, gate anim
-- Uses ffe.every with ~60Hz rate for per-frame logic.
--------------------------------------------------------------------
local TICK_RATE = 0.016  -- ~60 Hz

ffe.every(TICK_RATE, function()
    if getGameState and getGameState() ~= "PLAYING" then return end

    -- Rotate and bob artifacts / gems
    artifactAngle = artifactAngle + 90 * TICK_RATE
    if artifactAngle > 360 then artifactAngle = artifactAngle - 360 end

    for _, ad in ipairs(artifactEntities) do
        local bobY = ad.pos.y + math.sin(math.rad(artifactAngle * 2)) * 0.3
        ffe.setTransform3D(ad.entity, ad.pos.x, bobY, ad.pos.z,
            0, artifactAngle, 0,
            ad.scale, ad.scale, ad.scale)
    end

    -- Puzzle: check if blocks are on pressure plates
    if not puzzleSolved then
        local plate1Active = isBlockOnPlate(pushBlock1, pressurePlates[1])
                          or isBlockOnPlate(pushBlock2, pressurePlates[1])
        local plate2Active = isBlockOnPlate(pushBlock1, pressurePlates[2])
                          or isBlockOnPlate(pushBlock2, pressurePlates[2])

        -- Visual feedback: change plate marker color when activated
        -- (We can't easily reference the marker entities here, so we
        -- use the blockOnPlate tracking for HUD prompts instead.)
        if plate1Active ~= blockOnPlate[1] then
            blockOnPlate[1] = plate1Active
            if plate1Active and HUD then
                HUD.showPrompt("Pressure plate activated!", 1.5)
            end
        end
        if plate2Active ~= blockOnPlate[2] then
            blockOnPlate[2] = plate2Active
            if plate2Active and HUD then
                HUD.showPrompt("Pressure plate activated!", 1.5)
            end
        end

        if plate1Active and plate2Active then
            puzzleSolved = true
            gateOpen = true
            gateTargetY = 7.0  -- rise above the archway
            if sfxGate then ffe.playSound(sfxGate, 1.0) end
            if HUD then HUD.showPrompt("The gate is opening!", 3.0) end
            ffe.log("[Level1] Puzzle solved! Gate opening.")
        end
    end

    -- Gate animation: smoothly raise/lower
    if gateEntity ~= 0 and gateOpen then
        if gateY < gateTargetY then
            gateY = gateY + 3.0 * TICK_RATE  -- rise speed
            if gateY > gateTargetY then gateY = gateTargetY end
            ffe.setTransform3D(gateEntity, 0, gateY, 20, 0, 0, 0, 4.5, 2.5, 0.4)
        end
    end

    -- Show proximity prompts
    if Player then
        local px, py, pz = Player.getPosition()

        -- Near push blocks: show hint
        if not puzzleSolved then
            ffe.fillTransform3D(pushBlock1, tbufBlock)
            local b1x = tbufBlock.x or 0
            local b1z = tbufBlock.z or 0
            ffe.fillTransform3D(pushBlock2, tbufBlock)
            local b2x = tbufBlock.x or 0
            local b2z = tbufBlock.z or 0

            local d1 = math.sqrt((px - b1x)^2 + (pz - b1z)^2)
            local d2 = math.sqrt((px - b2x)^2 + (pz - b2z)^2)

            if d1 < 3.0 or d2 < 3.0 then
                if HUD then
                    HUD.showPrompt("Push the blocks onto the golden plates", 0.1)
                end
            end
        end

        -- Near destructible wall: show hint
        if destructWall ~= 0 then
            local dwDist = math.sqrt((px - (-16))^2 + (pz - 8)^2)
            if dwDist < 4.0 then
                if HUD then
                    HUD.showPrompt("This wall looks weak... (Attack to break)", 0.1)
                end
            end
        end

        -- Fall-off detection: respawn if player falls below terrain
        local groundY = ffe.getTerrainHeight(px, pz)
        if py < groundY - 10 then
            Player.cleanup()
            local spawnY = ffe.getTerrainHeight(0, -17) + 1.5
            Player.create(0, spawnY, -17, cesiumMesh)
            Camera.setPosition(0, spawnY, -17)
            if HUD then HUD.showPrompt("Watch your step!", 2.0) end
        end
    end
end)

--------------------------------------------------------------------
-- Done
--------------------------------------------------------------------
ffe.log("[Level1] The Courtyard loaded successfully!")
ffe.log("[Level1] Objectives:")
ffe.log("[Level1]   1. Push 2 stone blocks onto the golden pressure plates")
ffe.log("[Level1]   2. Break the crumbling wall to find the artifact")
ffe.log("[Level1]   3. Collect the Ancient Artifact")
ffe.log("[Level1]   4. Defeat or avoid the 2 guardians")
ffe.log("[Level1] Controls: WASD move, SPACE jump, Mouse look, LMB attack, E interact, ESC pause")
ffe.log("[Level1] Gamepad: L-Stick move, R-Stick camera, A jump, X attack, Y interact")
