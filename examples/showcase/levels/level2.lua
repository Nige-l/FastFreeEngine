-- levels/level2.lua -- "The Temple" (Level 2) for "Echoes of the Ancients"
--
-- A dark underground temple with pillars, a lava pit, narrow bridges,
-- glowing crystals, and a central altar. Dramatic colored point lights
-- in near-darkness create an oppressive, atmospheric environment.
--
-- GAMEPLAY:
--   Crystal puzzle: activate 4 crystals in correct order (blue->green->purple->cyan)
--   Timed platforms: east and west bridges appear/disappear on 3s cycle
--   Boss guardian: upgraded guardian guarding the central altar
--   Victory: all crystals activated -> exit portal opens -> walk to portal
--
-- Entity budget: ~60 mesh entities, 4 point lights, ~33 physics bodies
-- Well within LEGACY-tier 60 fps budget.

ffe.log("[Level2] Loading The Temple...")

--------------------------------------------------------------------
-- Load the shared cube mesh (used for all geometry)
--------------------------------------------------------------------
local cubeMesh = ffe.loadMesh("models/cube.glb")
if cubeMesh == 0 then
    ffe.log("[Level2] WARNING: cube.glb not found -- geometry will be missing")
    ffe.log("[Level2] Place cube.glb at assets/models/cube.glb")
end

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
    ffe.setMusicVolume(0.3)
    ffe.log("[Level2] Music playing: audio/BattleMusic.mp3")
else
    ffe.log("[Level2] WARNING: Could not load music (handle=" .. tostring(musicHandle) .. ")")
    ffe.log("[Level2] Expected file at: assets/audio/BattleMusic.mp3")
end

--------------------------------------------------------------------
-- Post-processing: bloom for lava glow, SSAO for underground depth
--------------------------------------------------------------------
ffe.enablePostProcessing()
ffe.enableBloom(1.0, 0.2)        -- subtle bloom, lava glow
ffe.setToneMapping(2)             -- ACES filmic
ffe.enableSSAO()                  -- adds depth to dark underground
ffe.setAntiAliasing(2)            -- FXAA

--------------------------------------------------------------------
-- Lighting: dark underground with warm torch-lit feel
--------------------------------------------------------------------
ffe.setLightDirection(0, -1, 0.2)       -- overhead light
ffe.setLightColor(0.8, 0.6, 0.4)       -- warm/orange (torch-lit feel)
ffe.setAmbientColor(0.08, 0.05, 0.03)  -- very dark ambient

-- Enable shadows (lower resolution for underground -- 512 is fine)
ffe.enableShadows(512)
ffe.setShadowBias(0.005)
ffe.setShadowArea(35, 35, 0.1, 60)

--------------------------------------------------------------------
-- Fog: dark reddish-brown, oppressive short range
--------------------------------------------------------------------
ffe.setFog(0.15, 0.08, 0.05, 5.0, 35.0)
ffe.setBackgroundColor(0.02, 0.01, 0.03)

--------------------------------------------------------------------
-- No skybox (underground)
--------------------------------------------------------------------
ffe.unloadSkybox()

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
-- Helper: create a visual-only box (no physics)
--------------------------------------------------------------------
local function createVisualBox(x, y, z, sx, sy, sz, r, g, b, a, rx, ry, rz)
    rx = rx or 0
    ry = ry or 0
    rz = rz or 0
    a  = a or 1.0
    local ent = 0
    if cubeMesh ~= 0 then
        ent = ffe.createEntity3D(cubeMesh, x, y, z)
        if ent ~= 0 then
            ffe.setTransform3D(ent, x, y, z, rx, ry, rz, sx, sy, sz)
            ffe.setMeshColor(ent, r, g, b, a)
        end
    end
    return ent
end

--------------------------------------------------------------------
-- FLOOR: Main stone platform sections
-- The temple is a cross-shaped layout with a central area and
-- 4 bridge arms extending over the lava pit.
--------------------------------------------------------------------
-- Central platform (16x1x16 -- thicker + brighter for visibility)
createStaticBox(0, -0.5, 0, 8, 1.0, 8, 0.40, 0.35, 0.30)

-- South platform (entrance area, wider 12x1x10)
createStaticBox(0, -0.5, -15, 6, 1.0, 5, 0.40, 0.35, 0.30)

-- North platform (exit area, wider 10x1x8)
createStaticBox(0, -0.5, 15, 5, 1.0, 4, 0.40, 0.35, 0.30)

-- East platform (crystal alcove, wider 8x1x8)
createStaticBox(13, -0.5, 0, 4, 1.0, 4, 0.40, 0.35, 0.30)

-- West platform (crystal alcove, wider 8x1x8)
createStaticBox(-13, -0.5, 0, 4, 1.0, 4, 0.40, 0.35, 0.30)

-- Platform edge markers: glowing border strips for spatial reference (Bug 2)
-- Central platform edges
createVisualBox(0, 0.02, 8,  8, 0.02, 0.15, 0.4, 0.25, 0.15, 1.0)
createVisualBox(0, 0.02, -8, 8, 0.02, 0.15, 0.4, 0.25, 0.15, 1.0)
createVisualBox(8, 0.02, 0,  0.15, 0.02, 8,  0.4, 0.25, 0.15, 1.0)
createVisualBox(-8, 0.02, 0, 0.15, 0.02, 8,  0.4, 0.25, 0.15, 1.0)

--------------------------------------------------------------------
-- LAVA PIT: Red-orange glowing flat plane below floor level
--------------------------------------------------------------------
createVisualBox(0, -2.0, 0, 18, 0.3, 18, 1.0, 0.35, 0.05, 1.0)

--------------------------------------------------------------------
-- NARROW BRIDGES: 4 stone bridges crossing the lava
-- East and west bridges are timed platforms (appear/disappear).
-- South and north bridges are permanent.
--------------------------------------------------------------------
-- Bridge 1: South platform -> Central (permanent, along Z axis)
createStaticBox(0, -0.35, -9, 1, 0.15, 4, 0.3, 0.28, 0.26)

-- Bridge 4: Central -> North (permanent, along Z axis)
createStaticBox(0, -0.35, 9, 1, 0.15, 3, 0.3, 0.28, 0.26)

--------------------------------------------------------------------
-- TIMED PLATFORMS: East and west bridges appear/disappear on 3s cycle
-- When disappeared: move below floor (Y = -10)
-- When appeared: move to bridge height (Y = -0.35)
-- Visual warning: bridge flashes before disappearing
--------------------------------------------------------------------
local TIMED_BRIDGE_Y_UP   = -0.35
local TIMED_BRIDGE_Y_DOWN = -10.0
local TIMED_BRIDGE_CYCLE  = 3.0   -- seconds per phase
local TIMED_BRIDGE_WARN   = 0.8   -- seconds of flashing before disappearing

local timedBridges = {}

-- East timed bridge
local eastBridge = createStaticBox(9, TIMED_BRIDGE_Y_UP, 0, 3.5, 0.15, 1, 0.3, 0.28, 0.26)
if eastBridge ~= 0 then
    timedBridges[#timedBridges + 1] = {
        entity  = eastBridge,
        x       = 9,
        z       = 0,
        sx      = 3.5,
        sy      = 0.15,
        sz      = 1,
        timer   = 0,
        visible = true,
    }
end

-- West timed bridge (offset by half a cycle so they alternate)
local westBridge = createStaticBox(-9, TIMED_BRIDGE_Y_UP, 0, 3.5, 0.15, 1, 0.3, 0.28, 0.26)
if westBridge ~= 0 then
    timedBridges[#timedBridges + 1] = {
        entity  = westBridge,
        x       = -9,
        z       = 0,
        sx      = 3.5,
        sy      = 0.15,
        sz      = 1,
        timer   = TIMED_BRIDGE_CYCLE * 0.5,  -- offset start
        visible = true,
    }
end

--------------------------------------------------------------------
-- PERIMETER WALLS (dark stone, 6 units high)
--------------------------------------------------------------------
-- South wall: two segments with entrance gap
createStaticBox(-11, 3, -20, 7, 3, 0.5, 0.2, 0.18, 0.17)
createStaticBox( 11, 3, -20, 7, 3, 0.5, 0.2, 0.18, 0.17)

-- North wall: two segments with exit gap
createStaticBox(-11, 3, 20, 7, 3, 0.5, 0.2, 0.18, 0.17)
createStaticBox( 11, 3, 20, 7, 3, 0.5, 0.2, 0.18, 0.17)

-- East wall (solid)
createStaticBox(18, 3, 0, 0.5, 3, 20, 0.2, 0.18, 0.17)

-- West wall (solid)
createStaticBox(-18, 3, 0, 0.5, 3, 20, 0.2, 0.18, 0.17)

--------------------------------------------------------------------
-- PILLARS: Tall stone columns (1x6x1)
--------------------------------------------------------------------
-- Central platform corner pillars (4)
createStaticBox(-5, 3, -5, 0.5, 3, 0.5, 0.22, 0.2, 0.19)
createStaticBox( 5, 3, -5, 0.5, 3, 0.5, 0.22, 0.2, 0.19)
createStaticBox(-5, 3,  5, 0.5, 3, 0.5, 0.22, 0.2, 0.19)
createStaticBox( 5, 3,  5, 0.5, 3, 0.5, 0.22, 0.2, 0.19)

-- Bridge-edge pillars (4, decorative only -- no physics needed)
createVisualBox(-1.5, 2, -5.5, 0.4, 2, 0.4, 0.22, 0.2, 0.19, 1.0)
createVisualBox( 1.5, 2, -5.5, 0.4, 2, 0.4, 0.22, 0.2, 0.19, 1.0)
createVisualBox(-1.5, 2,  5.5, 0.4, 2, 0.4, 0.22, 0.2, 0.19, 1.0)
createVisualBox( 1.5, 2,  5.5, 0.4, 2, 0.4, 0.22, 0.2, 0.19, 1.0)

-- Wall-hugging pillars (4, decorative only -- no physics needed)
createVisualBox(-17, 3, -10, 0.6, 3, 0.6, 0.22, 0.2, 0.19, 1.0)
createVisualBox( 17, 3, -10, 0.6, 3, 0.6, 0.22, 0.2, 0.19, 1.0)
createVisualBox(-17, 3,  10, 0.6, 3, 0.6, 0.22, 0.2, 0.19, 1.0)
createVisualBox( 17, 3,  10, 0.6, 3, 0.6, 0.22, 0.2, 0.19, 1.0)

--------------------------------------------------------------------
-- CEILING: Dark flat plane above to enclose the space
--------------------------------------------------------------------
createVisualBox(0, 7, 0, 18, 0.3, 20, 0.08, 0.06, 0.07, 1.0)

--------------------------------------------------------------------
-- STALACTITES: Hanging boxes from ceiling for atmosphere
--------------------------------------------------------------------
createVisualBox(-8, 5.5, -7, 0.25, 1.2, 0.25, 0.18, 0.16, 0.15, 1.0)
createVisualBox( 10, 5.0, 5, 0.3, 1.5, 0.3, 0.16, 0.14, 0.13, 1.0)
createVisualBox( 3, 5.8, -12, 0.2, 0.9, 0.2, 0.17, 0.15, 0.14, 1.0)
createVisualBox(-6, 5.2, 11, 0.35, 1.3, 0.35, 0.15, 0.13, 0.12, 1.0)
createVisualBox( 14, 5.6, -3, 0.2, 1.0, 0.2, 0.16, 0.14, 0.13, 1.0)

--------------------------------------------------------------------
-- CRYSTAL PEDESTALS: 4 at cardinal positions
-- Puzzle: activate in correct order: blue -> green -> purple -> cyan
-- Press E (or gamepad Y) within 3 units of a crystal to interact.
--------------------------------------------------------------------
local CRYSTAL_INTERACT_DIST = 3.0
local CORRECT_SEQUENCE = { "blue", "green", "purple", "cyan" }

local crystalData = {
    { name = "blue",   px = 0,   pz = -13, cr = 0.3, cg = 0.5, cb = 1.0, lr = 0.3, lg = 0.5, lb = 1.0, radius = 12 },
    { name = "cyan",   px = 13,  pz = 0,   cr = 0.2, cg = 0.8, cb = 0.8, lr = 0.2, lg = 0.8, lb = 0.8, radius = 10 },
    { name = "purple", px = 0,   pz = 13,  cr = 0.7, cg = 0.3, cb = 0.9, lr = 0.7, lg = 0.3, lb = 0.9, radius = 10 },
    { name = "green",  px = -13, pz = 0,   cr = 0.3, cg = 0.9, cb = 0.4, lr = 0.3, lg = 0.9, lb = 0.4, radius = 8  },
}

-- Build a lookup by name for quick access
local crystalByName = {}

local crystalEntities = {}

for i, cd in ipairs(crystalData) do
    -- Pedestal base (raised dark platform)
    createStaticBox(cd.px, 0.3, cd.pz, 1.0, 0.3, 1.0, 0.3, 0.28, 0.26)

    -- Crystal entity (small bright glowing box on top of pedestal)
    if cubeMesh ~= 0 then
        local cy = 1.0
        local crystal = ffe.createEntity3D(cubeMesh, cd.px, cy, cd.pz)
        if crystal ~= 0 then
            ffe.setTransform3D(crystal, cd.px, cy, cd.pz, 0, 45, 0, 0.3, 0.5, 0.3)
            ffe.setMeshColor(crystal, cd.cr, cd.cg, cd.cb, 1.0)
            ffe.setMeshSpecular(crystal, 1.0, 1.0, 1.0, 128)
            local cdata = {
                entity    = crystal,
                pos       = { x = cd.px, y = cy, z = cd.pz },
                name      = cd.name,
                activated = false,
                baseR     = cd.cr,
                baseG     = cd.cg,
                baseB     = cd.cb,
                lightIdx  = i - 1,
                lightR    = cd.lr,
                lightG    = cd.lg,
                lightB    = cd.lb,
                radius    = cd.radius,
            }
            crystalEntities[#crystalEntities + 1] = cdata
            crystalByName[cd.name] = cdata
        end
    end

    -- Point light at crystal position (colored glow -- dim initially)
    -- Point light slots 0-3 correspond to crystals 1-4
    ffe.addPointLight(i - 1, cd.px, 1.5, cd.pz, cd.lr * 0.4, cd.lg * 0.4, cd.lb * 0.4, cd.radius * 0.5)
end

--------------------------------------------------------------------
-- CRYSTAL PUZZLE STATE
--------------------------------------------------------------------
local crystalSequenceIndex = 0   -- how many correct activations so far
local puzzleSolved         = false
local puzzleResetTimer     = 0   -- >0 during error flash

-- Activate a crystal visually (brighten color + expand point light)
local function activateCrystal(cdata)
    cdata.activated = true
    -- Bright white-tinted version of its color
    ffe.setMeshColor(cdata.entity,
        math.min(cdata.baseR + 0.5, 1.0),
        math.min(cdata.baseG + 0.5, 1.0),
        math.min(cdata.baseB + 0.5, 1.0),
        1.0)
    ffe.setMeshSpecular(cdata.entity, 1.0, 1.0, 1.0, 256)
    -- Boost point light to full brightness and radius
    ffe.addPointLight(cdata.lightIdx,
        cdata.pos.x, 1.5, cdata.pos.z,
        cdata.lightR, cdata.lightG, cdata.lightB,
        cdata.radius)
    if sfxCollect then ffe.playSound(sfxCollect, 0.6) end
    ffe.cameraShake(0.3, 0.1)
    ffe.log("[Level2] Crystal activated: " .. cdata.name)
end

-- Deactivate a crystal visually (restore dim color + dim point light)
local function deactivateCrystal(cdata)
    cdata.activated = false
    ffe.setMeshColor(cdata.entity, cdata.baseR, cdata.baseG, cdata.baseB, 1.0)
    ffe.setMeshSpecular(cdata.entity, 1.0, 1.0, 1.0, 128)
    ffe.addPointLight(cdata.lightIdx,
        cdata.pos.x, 1.5, cdata.pos.z,
        cdata.lightR * 0.4, cdata.lightG * 0.4, cdata.lightB * 0.4,
        cdata.radius * 0.5)
end

-- Flash all crystals red briefly (wrong sequence)
local function flashCrystalsRed()
    for _, cd in ipairs(crystalEntities) do
        ffe.setMeshColor(cd.entity, 1.0, 0.1, 0.1, 1.0)
    end
    if sfxHit then ffe.playSound(sfxHit, 1.0) end
    ffe.cameraShake(0.8, 0.25)
    puzzleResetTimer = 0.6  -- flash duration
end

-- Reset all crystals to inactive
local function resetAllCrystals()
    crystalSequenceIndex = 0
    for _, cd in ipairs(crystalEntities) do
        deactivateCrystal(cd)
    end
    ffe.log("[Level2] Crystal sequence reset!")
end

-- Try to activate a crystal by name
local function tryCrystalActivation(crystalName)
    if puzzleSolved then return end
    if puzzleResetTimer > 0 then return end  -- still flashing red

    local cdata = crystalByName[crystalName]
    if not cdata then return end
    if cdata.activated then return end  -- already activated

    -- Check if this is the next crystal in the correct sequence
    local expectedName = CORRECT_SEQUENCE[crystalSequenceIndex + 1]
    if crystalName == expectedName then
        -- Correct!
        crystalSequenceIndex = crystalSequenceIndex + 1
        activateCrystal(cdata)

        if crystalSequenceIndex >= #CORRECT_SEQUENCE then
            -- Puzzle solved!
            puzzleSolved = true
            onPuzzleSolved()
        else
            if HUD then
                HUD.showPrompt(cdata.name:sub(1,1):upper() .. cdata.name:sub(2)
                    .. " crystal activated! (" .. tostring(crystalSequenceIndex)
                    .. "/" .. tostring(#CORRECT_SEQUENCE) .. ")", 2.5)
            end
        end
    else
        -- Wrong order! Flash red and reset
        flashCrystalsRed()
        if HUD then
            HUD.showPrompt("Wrong sequence! The crystals go dark...", 2.5)
        end
    end
end

--------------------------------------------------------------------
-- CENTRAL ALTAR: Raised platform in the middle with the artifact
--------------------------------------------------------------------
-- Altar base (large stepped platform)
createStaticBox(0, 0.25, 0, 2.0, 0.25, 2.0, 0.28, 0.25, 0.23)
-- Altar top step
createStaticBox(0, 0.7, 0, 1.2, 0.2, 1.2, 0.32, 0.28, 0.25)

--------------------------------------------------------------------
-- EXIT PORTAL: Archway frame near north wall
-- Inactive until crystal puzzle is solved.
--------------------------------------------------------------------
-- Portal pillars (decorative -- no physics)
createVisualBox(-3, 2.5, 19, 0.6, 2.5, 0.6, 0.35, 0.25, 0.4, 1.0)
createVisualBox( 3, 2.5, 19, 0.6, 2.5, 0.6, 0.35, 0.25, 0.4, 1.0)
-- Portal lintel
createVisualBox(0, 5.2, 19, 3.5, 0.4, 0.6, 0.35, 0.25, 0.4, 1.0)

-- Portal "glow" visual (dark for now -- will light up when active)
local portalGlow = createVisualBox(0, 2.5, 19.3, 2.4, 2.0, 0.1, 0.1, 0.05, 0.15, 1.0)

-- Portal trigger zone (kinematic physics body the player walks into)
local portalTrigger = 0
if cubeMesh ~= 0 then
    portalTrigger = ffe.createEntity3D(cubeMesh, 0, 1.5, 19)
    if portalTrigger ~= 0 then
        -- Invisible trigger -- very small visual, mostly physics
        ffe.setTransform3D(portalTrigger, 0, 1.5, 19, 0, 0, 0, 0.1, 0.1, 0.1)
        ffe.setMeshColor(portalTrigger, 0, 0, 0, 0)
        ffe.createPhysicsBody(portalTrigger, {
            shape       = "box",
            halfExtents = { 2.5, 2.5, 1.0 },
            motion      = "kinematic",
        })
    end
end
local portalActive = false

--------------------------------------------------------------------
-- ENTRANCE ARCHWAY (south side)
--------------------------------------------------------------------
createVisualBox(-3, 2.5, -19, 0.6, 2.5, 0.6, 0.3, 0.27, 0.25, 1.0)
createVisualBox( 3, 2.5, -19, 0.6, 2.5, 0.6, 0.3, 0.27, 0.25, 1.0)
createVisualBox(0, 5.2, -19, 3.5, 0.4, 0.6, 0.3, 0.27, 0.25, 1.0)

--------------------------------------------------------------------
-- MAIN ARTIFACT: Gold artifact on central altar (rotating)
-- Can be collected at any time for bonus (not required for portal)
--------------------------------------------------------------------
local mainArtifact = 0
local mainArtifactPos = { x = 0, y = 1.5, z = 0 }

if cubeMesh ~= 0 then
    mainArtifact = ffe.createEntity3D(cubeMesh,
        mainArtifactPos.x, mainArtifactPos.y, mainArtifactPos.z)
    if mainArtifact ~= 0 then
        ffe.setTransform3D(mainArtifact,
            mainArtifactPos.x, mainArtifactPos.y, mainArtifactPos.z,
            0, 0, 0, 0.4, 0.4, 0.4)
        ffe.setMeshColor(mainArtifact, 1.0, 0.85, 0.1, 1.0)
        ffe.setMeshSpecular(mainArtifact, 1.0, 0.9, 0.4, 128)
        ffe.createPhysicsBody(mainArtifact, {
            shape       = "box",
            halfExtents = { 0.45, 0.45, 0.45 },
            motion      = "kinematic",
        })
    end
end

--------------------------------------------------------------------
-- OPTIONAL GEM PICKUPS: 2 on bridge platforms
--------------------------------------------------------------------
local gems = {}
local gemPositions = {
    { x = 9,  y = 0.3, z = 0 },   -- east bridge midpoint
    { x = -9, y = 0.3, z = 0 },   -- west bridge midpoint
}
local gemColors = {
    { 0.9, 0.2, 0.5 },  -- ruby
    { 0.1, 0.8, 1.0 },  -- aquamarine
}

for i, pos in ipairs(gemPositions) do
    if cubeMesh ~= 0 then
        local gem = ffe.createEntity3D(cubeMesh, pos.x, pos.y, pos.z)
        if gem ~= 0 then
            ffe.setTransform3D(gem, pos.x, pos.y, pos.z, 0, 0, 0, 0.25, 0.25, 0.25)
            local c = gemColors[i]
            ffe.setMeshColor(gem, c[1], c[2], c[3], 1.0)
            ffe.setMeshSpecular(gem, 0.8, 0.8, 0.8, 128)
            ffe.createPhysicsBody(gem, {
                shape       = "box",
                halfExtents = { 0.3, 0.3, 0.3 },
                motion      = "kinematic",
            })
            gems[gem] = { index = i, collected = false, pos = pos }
        end
    end
end

-- Track all rotating entities (artifact + gems)
local artifactEntities = {}
if mainArtifact ~= 0 then
    artifactEntities[#artifactEntities + 1] = {
        entity = mainArtifact,
        pos    = mainArtifactPos,
        scale  = 0.4,
        isMain = true,
    }
end
for entId, data in pairs(gems) do
    artifactEntities[#artifactEntities + 1] = {
        entity = entId,
        pos    = data.pos,
        scale  = 0.25,
        isMain = false,
    }
end

--------------------------------------------------------------------
-- Level state
--------------------------------------------------------------------
local artifactAngle    = 0
local crystalAngle     = 0
local gemsCollected    = 0
local totalGems        = 2
local mainCollected    = false
local levelComplete    = false

-- Set global artifact count for game.lua
totalArtifacts = 1  -- only the main artifact is needed for game.lua tracking

--------------------------------------------------------------------
-- GUARDIANS: 2 regular + 1 boss (upgraded)
--------------------------------------------------------------------
AI.reset()

-- Guardian 1: patrols south bridge and entrance area
local guardian1 = 0
if cubeMesh ~= 0 then
    local gx, gy, gz = 0, 0.8, -11
    guardian1 = ffe.createEntity3D(cubeMesh, gx, gy, gz)
    if guardian1 ~= 0 then
        ffe.setTransform3D(guardian1, gx, gy, gz, 0, 0, 0, 1.0, 1.3, 1.0)
        ffe.setMeshColor(guardian1, 0.55, 0.2, 0.6, 1.0)
        ffe.setMeshSpecular(guardian1, 0.4, 0.2, 0.5, 32)
        ffe.createPhysicsBody(guardian1, {
            shape       = "box",
            halfExtents = { 0.5, 0.65, 0.5 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian1, {
            { x =  0, y = 0.8, z = -13 },
            { x =  3, y = 0.8, z = -16 },
            { x = -3, y = 0.8, z = -16 },
            { x =  0, y = 0.8, z = -9  },
        }, 80)
    end
end

-- Guardian 2: patrols north platform near exit
local guardian2 = 0
if cubeMesh ~= 0 then
    local gx, gy, gz = 2, 0.8, 14
    guardian2 = ffe.createEntity3D(cubeMesh, gx, gy, gz)
    if guardian2 ~= 0 then
        ffe.setTransform3D(guardian2, gx, gy, gz, 0, 0, 0, 1.0, 1.3, 1.0)
        ffe.setMeshColor(guardian2, 0.55, 0.2, 0.6, 1.0)
        ffe.setMeshSpecular(guardian2, 0.4, 0.2, 0.5, 32)
        ffe.createPhysicsBody(guardian2, {
            shape       = "box",
            halfExtents = { 0.5, 0.65, 0.5 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian2, {
            { x =  3, y = 0.8, z = 14 },
            { x = -3, y = 0.8, z = 14 },
            { x = -3, y = 0.8, z = 16 },
            { x =  3, y = 0.8, z = 16 },
        }, 80)
    end
end

-- BOSS GUARDIAN: guards the central altar area
-- Double HP (160), slightly faster, larger scale (1.5x), dark gold color
local bossGuardian = 0
if cubeMesh ~= 0 then
    local gx, gy, gz = 3, 1.0, 3
    bossGuardian = ffe.createEntity3D(cubeMesh, gx, gy, gz)
    if bossGuardian ~= 0 then
        -- 1.5x scale compared to regular guardians
        ffe.setTransform3D(bossGuardian, gx, gy, gz, 0, 0, 0, 1.5, 1.95, 1.5)
        -- Dark gold/bronze color to distinguish from purple regulars
        ffe.setMeshColor(bossGuardian, 0.7, 0.5, 0.15, 1.0)
        ffe.setMeshSpecular(bossGuardian, 0.8, 0.6, 0.2, 64)
        ffe.createPhysicsBody(bossGuardian, {
            shape       = "box",
            halfExtents = { 0.75, 0.975, 0.75 },
            motion      = "dynamic",
            mass        = 6.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        -- 160 HP (double the regular 80)
        AI.create(bossGuardian, {
            { x =  4, y = 1.0, z =  4 },
            { x = -4, y = 1.0, z =  4 },
            { x = -4, y = 1.0, z = -4 },
            { x =  4, y = 1.0, z = -4 },
        }, 160)
    end
end

--------------------------------------------------------------------
-- PUZZLE SOLVED: activate exit portal
--------------------------------------------------------------------
function onPuzzleSolved()
    portalActive = true
    ffe.log("[Level2] Crystal puzzle solved! Portal is now active!")

    -- Light up the portal glow (bright purple-white)
    if portalGlow ~= 0 then
        ffe.setMeshColor(portalGlow, 0.6, 0.3, 1.0, 1.0)
    end

    -- Play gate/portal sound
    if sfxGate then ffe.playSound(sfxGate, 1.0) end
    ffe.cameraShake(1.0, 0.3)

    if HUD then
        HUD.showPrompt("All crystals activated! The portal opens!", 4.0)
    end
end

--------------------------------------------------------------------
-- Collision callback: artifact/gem pickups + portal trigger
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

    -- Check if player walks into active portal
    if otherEnt == portalTrigger and portalActive and not levelComplete then
        levelComplete = true
        ffe.log("[Level2] Player reached the portal! Level complete!")
        if sfxGate then ffe.playSound(sfxGate, 1.0) end
        ffe.cameraShake(0.5, 0.15)
        -- Trigger level complete in game.lua
        -- Use collectArtifact if main artifact was not already collected
        if not mainCollected and collectArtifact then
            collectArtifact()
        end
        -- If main artifact was already collected, just ensure game state transitions
        if mainCollected then
            -- Force level completion via game state
            if getGameState and getGameState() == "PLAYING" then
                -- The artifact was already collected, so game.lua already
                -- transitioned to LEVEL_COMPLETE. Just show prompt.
                if HUD then HUD.showPrompt("The Temple conquered!", 3.0) end
            end
        else
            if HUD then HUD.showPrompt("The Temple conquered!", 3.0) end
        end
        return
    end

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
        if HUD then HUD.showPrompt("Temple Artifact acquired!", 3.0) end
        ffe.log("[Level2] Main artifact collected!")
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
        ffe.log("[Level2] Gem collected: " .. tostring(gemsCollected)
            .. "/" .. tostring(totalGems))
        return
    end
end)

--------------------------------------------------------------------
-- Player spawn (south entrance bridge)
--------------------------------------------------------------------
Player.create(0, 1.5, -17, cubeMesh)
Camera.setPosition(0, 1.5, -17)
Camera.setYawPitch(0, 30)  -- Pitch down to see the floor on spawn (Bug 2)

if HUD then
    HUD.showPrompt("The Temple -- Activate the crystals to open the portal", 5.0)
end

--------------------------------------------------------------------
-- Internal: distance XZ (ignore Y for interaction range)
--------------------------------------------------------------------
local function distXZ(ax, az, bx, bz)
    local dx = ax - bx
    local dz = az - bz
    return math.sqrt(dx * dx + dz * dz)
end

--------------------------------------------------------------------
-- Per-frame level tick
--------------------------------------------------------------------
local TICK_RATE = 0.016  -- ~60 Hz

ffe.every(TICK_RATE, function()
    if getGameState and getGameState() ~= "PLAYING" then return end

    -- ================================================================
    -- Rotate and bob artifacts / gems
    -- ================================================================
    artifactAngle = artifactAngle + 90 * TICK_RATE
    if artifactAngle > 360 then artifactAngle = artifactAngle - 360 end

    for _, ad in ipairs(artifactEntities) do
        local bobY = ad.pos.y + math.sin(math.rad(artifactAngle * 2)) * 0.3
        ffe.setTransform3D(ad.entity, ad.pos.x, bobY, ad.pos.z,
            0, artifactAngle, 0,
            ad.scale, ad.scale, ad.scale)
    end

    -- ================================================================
    -- Slowly rotate crystal entities for shimmer effect
    -- Activated crystals spin faster and bob higher
    -- ================================================================
    crystalAngle = crystalAngle + 45 * TICK_RATE
    if crystalAngle > 360 then crystalAngle = crystalAngle - 360 end

    for _, cd in ipairs(crystalEntities) do
        local spinSpeed = cd.activated and (crystalAngle * 2) or crystalAngle
        local bobAmt    = cd.activated and 0.25 or 0.1
        local scaleY    = cd.activated and 0.65 or 0.5
        local bobY = cd.pos.y + math.sin(math.rad(spinSpeed * 1.5)) * bobAmt
        ffe.setTransform3D(cd.entity, cd.pos.x, bobY, cd.pos.z,
            0, spinSpeed, 0,
            0.3, scaleY, 0.3)
    end

    -- ================================================================
    -- Crystal puzzle reset flash timer
    -- ================================================================
    if puzzleResetTimer > 0 then
        puzzleResetTimer = puzzleResetTimer - TICK_RATE
        if puzzleResetTimer <= 0 then
            puzzleResetTimer = 0
            resetAllCrystals()
        end
    end

    -- ================================================================
    -- Crystal interaction: check if player is near a crystal and presses E
    -- ================================================================
    if Player and not puzzleSolved and puzzleResetTimer <= 0 then
        local px, py, pz = Player.getPosition()
        local nearCrystal = nil
        local nearDist    = CRYSTAL_INTERACT_DIST + 1  -- sentinel

        for _, cd in ipairs(crystalEntities) do
            local d = distXZ(px, pz, cd.pos.x, cd.pos.z)
            if d < CRYSTAL_INTERACT_DIST and d < nearDist then
                nearCrystal = cd
                nearDist    = d
            end
        end

        -- Show interaction prompt when near a crystal
        if nearCrystal and not nearCrystal.activated then
            local capName = nearCrystal.name:sub(1,1):upper() .. nearCrystal.name:sub(2)
            if HUD then
                HUD.showPrompt("[E] Activate " .. capName .. " Crystal", 0.1)
            end
        end

        -- Handle interaction press
        if nearCrystal and Player.isInteracting() then
            tryCrystalActivation(nearCrystal.name)
        end
    end

    -- Show portal prompt when near and active
    if portalActive and not levelComplete and Player then
        local px, py, pz = Player.getPosition()
        local portalDist = distXZ(px, pz, 0, 19)
        if portalDist < 5.0 then
            if HUD then
                HUD.showPrompt("Walk into the portal to proceed!", 0.1)
            end
        end
    end

    -- ================================================================
    -- Timed platforms: cycle between visible and hidden
    -- ================================================================
    for _, bridge in ipairs(timedBridges) do
        bridge.timer = bridge.timer + TICK_RATE
        local phase = bridge.timer % (TIMED_BRIDGE_CYCLE * 2)

        if phase < TIMED_BRIDGE_CYCLE then
            -- Bridge is UP (visible)
            if not bridge.visible then
                bridge.visible = true
                ffe.setTransform3D(bridge.entity,
                    bridge.x, TIMED_BRIDGE_Y_UP, bridge.z,
                    0, 0, 0, bridge.sx, bridge.sy, bridge.sz)
            end

            -- Warning flash: bridge flashes before disappearing
            local timeUntilDown = TIMED_BRIDGE_CYCLE - phase
            if timeUntilDown < TIMED_BRIDGE_WARN then
                -- Flash between normal color and bright yellow
                local flashRate = 8.0  -- flashes per second
                local flash = math.sin(phase * flashRate * 3.14159 * 2)
                if flash > 0 then
                    ffe.setMeshColor(bridge.entity, 0.8, 0.6, 0.1, 1.0)
                else
                    ffe.setMeshColor(bridge.entity, 0.3, 0.28, 0.26, 1.0)
                end
            else
                ffe.setMeshColor(bridge.entity, 0.3, 0.28, 0.26, 1.0)
            end
        else
            -- Bridge is DOWN (hidden)
            if bridge.visible then
                bridge.visible = false
                ffe.setTransform3D(bridge.entity,
                    bridge.x, TIMED_BRIDGE_Y_DOWN, bridge.z,
                    0, 0, 0, bridge.sx, bridge.sy, bridge.sz)
                ffe.setMeshColor(bridge.entity, 0.3, 0.28, 0.26, 1.0)
            end
        end
    end

    -- ================================================================
    -- Portal glow pulsing (when active)
    -- ================================================================
    if portalActive and portalGlow ~= 0 and not levelComplete then
        local pulse = 0.5 + 0.5 * math.sin(crystalAngle * 0.05)
        ffe.setMeshColor(portalGlow,
            0.4 + 0.3 * pulse,
            0.2 + 0.2 * pulse,
            0.7 + 0.3 * pulse,
            1.0)
    end

    -- ================================================================
    -- Fall-off detection: respawn if player falls into lava
    -- ================================================================
    if Player then
        local px, py, pz = Player.getPosition()
        if py < -3 then
            Player.cleanup()
            Player.create(0, 1.5, -17, cubeMesh)
            Camera.setPosition(0, 1.5, -17)
            if HUD then HUD.showPrompt("The lava claims another...", 2.0) end
        end
    end
end)

--------------------------------------------------------------------
-- Done
--------------------------------------------------------------------
ffe.log("[Level2] The Temple loaded successfully!")
ffe.log("[Level2] Objectives:")
ffe.log("[Level2]   1. Activate the 4 crystals in the correct order (blue, green, purple, cyan)")
ffe.log("[Level2]   2. Cross the timed bridges to reach the crystal alcoves")
ffe.log("[Level2]   3. Defeat or avoid the temple guardians and the boss")
ffe.log("[Level2]   4. Collect the Temple Artifact from the central altar (bonus)")
ffe.log("[Level2]   5. Enter the portal once the crystals are activated")
ffe.log("[Level2] Controls: WASD move, SPACE jump, Mouse look, LMB attack, E interact, ESC pause")
ffe.log("[Level2] Gamepad: L-Stick move, R-Stick camera, A jump, X attack, Y interact")
