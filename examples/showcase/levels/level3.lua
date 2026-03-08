-- levels/level3.lua -- "The Summit" (Level 3) for "Echoes of the Ancients"
--
-- An open-air tower summit above the clouds. Floating stone platforms,
-- ancient pillars, dramatic sunset lighting, and a central battle arena.
-- The final level of the showcase game.
--
-- GAMEPLAY:
--   Platforming: jump across floating stepping stones to reach the arena
--   Moving platforms: 3 platforms oscillate via sine/cosine motion
--   Guardians: 4 guardians on separate floating platforms (one per quadrant)
--   Victory: collect the final artifact on the central altar
--
-- Entity budget: ~60 mesh entities, 3 point lights, ~30 physics bodies
-- Well within LEGACY-tier 60 fps budget.

ffe.log("[Level3] Loading The Summit...")

--------------------------------------------------------------------
-- Load mesh assets
--------------------------------------------------------------------
local cubeMesh = ffe.loadMesh("models/cube.glb")
if cubeMesh == 0 then
    ffe.log("[Level3] WARNING: cube.glb not found -- geometry will be missing")
    ffe.log("[Level3] Place cube.glb at assets/models/cube.glb")
end

local foxMesh = ffe.loadMesh("models/fox.glb")
if foxMesh == 0 then foxMesh = cubeMesh end

local helmetMesh = ffe.loadMesh("models/damaged_helmet.glb")
if helmetMesh == 0 then helmetMesh = cubeMesh end

local duckMesh = ffe.loadMesh("models/duck.glb")
if duckMesh == 0 then duckMesh = cubeMesh end

local cesiumMesh = ffe.loadMesh("models/cesium_man.glb")
if cesiumMesh == 0 then cesiumMesh = cubeMesh end

ffe.log("[Level3] Meshes loaded: cube=" .. tostring(cubeMesh)
    .. " fox=" .. tostring(foxMesh) .. " helmet=" .. tostring(helmetMesh)
    .. " duck=" .. tostring(duckMesh))

--------------------------------------------------------------------
-- Load audio assets
--------------------------------------------------------------------
local sfxCollect = ffe.loadSound("audio/sfx_collect.wav")
local sfxHit     = ffe.loadSound("audio/sfx_hit.wav")
local sfxGate    = ffe.loadSound("audio/sfx_gate.wav")

-- Music: epic battle theme for the summit climax
local musicHandle = ffe.loadMusic("audio/BattleMusic.ogg")
if musicHandle then
    ffe.playMusic(musicHandle, true)  -- loop
    ffe.log("[Level3] Battle music started (BattleMusic.ogg)")
else
    ffe.log("[Level3] WARNING: BattleMusic.ogg failed to load")
end

--------------------------------------------------------------------
-- Post-processing: full rendering stack for dramatic summit
-- Software renderers skip advanced FBO-based effects (see level1.lua).
--------------------------------------------------------------------
local softwareRenderer = ffe.isSoftwareRenderer()

if not softwareRenderer then
    ffe.enablePostProcessing()
    ffe.enableBloom(0.8, 0.3)
    ffe.setToneMapping(2)  -- ACES filmic
    ffe.enableSSAO()
    ffe.setAntiAliasing(2)  -- FXAA
end

--------------------------------------------------------------------
-- Lighting: dramatic golden hour summit
--------------------------------------------------------------------
ffe.setLightDirection(-0.2, -0.6, 0.4)   -- low sun angle
if softwareRenderer then
    -- Cranked lighting for software renderer -- golden hour needs to pop
    ffe.setLightColor(1.0, 0.9, 0.7)
    ffe.setAmbientColor(0.45, 0.42, 0.5)
else
    ffe.setLightColor(1.0, 0.8, 0.6)         -- golden hour
    ffe.setAmbientColor(0.12, 0.12, 0.2)     -- cold blue shadows
end

-- Shadows: wide area for the outdoor summit (skip on software renderer)
if not softwareRenderer then
    ffe.enableShadows(1024)
    ffe.setShadowBias(0.005)
    ffe.setShadowArea(40, 40, 0.1, 80)
end

--------------------------------------------------------------------
-- Fog: thick atmospheric summit fog
-- NOTE: Without post-processing, fog/bg values are used directly.
-- With ACES tone mapping, raw values are boosted so the
-- post-tonemapped result looks correct.
--------------------------------------------------------------------
if softwareRenderer then
    -- Push fog way out so floating platforms and arena are all visible
    ffe.setFog(0.55, 0.65, 0.82, 60.0, 200.0)
    ffe.setBackgroundColor(0.55, 0.65, 0.82)  -- bright sky blue
else
    ffe.setFog(0.72, 0.78, 0.95, 15.0, 50.0)
    ffe.setBackgroundColor(0.72, 0.78, 0.95)
end

--------------------------------------------------------------------
-- Terrain: dramatic summit heightmap with mountain ring
--------------------------------------------------------------------
-- heightScale reduced from 12 to 5: the summit heightmap has solid-white (255)
-- edges, which at heightScale=12 produced 12 m cliffs that appeared on top of
-- every floating platform (the highest platform is at y=6). At heightScale=5
-- the cliff ring peaks at ~6 m and sits outside the central play area
-- (world x/z range [0,60] vs play area centred at origin), eliminating the
-- Z-fighting / geometry-on-terrain overlap.
local terrainHandle = ffe.loadTerrain("terrain/summit_height.png", 60, 60, 5)
-- Enable terrain-aware camera clamping so the orbit arc never dips
-- into the summit terrain ring visible in the background (Bug 2 fix).
if Camera then Camera.setTerrainAware(true) end

-- Vegetation: trees at summit quadrant positions
local treePositions = {
    {-20, -20}, {20, -20}, {-20, 20}, {20, 20}
}
for _, pos in ipairs(treePositions) do
    local ty = ffe.getTerrainHeight(pos[1], pos[2])
    if ty then ffe.addTree(pos[1], ty, pos[2], 1.5) end
end
setTreesActive(true)
ffe.log("[Level3] Trees placed on summit")

--------------------------------------------------------------------
-- Skybox: unload any previous skybox; use fog + background for sky
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
            ffe.setMeshSpecular(ent, 0.2, 0.15, 0.1, 16)
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
-- CENTRAL ARENA: large ground floor for the arena combat area.
-- The terrain heightmap spans [0,60]x[0,60] (not centered at origin)
-- so a physics ground plane is needed under the play area.
--------------------------------------------------------------------
-- Main arena floor (covers the full combat area)
createStaticBox(0, -0.25, 0, 12, 0.25, 12, 0.55, 0.45, 0.35)
-- Arena inner ring (slightly raised decorative center)
createStaticBox(0, 0.05, 0, 3.5, 0.05, 3.5, 0.65, 0.55, 0.42)

-- Edge border walls: sunk well below the play area so they cannot
-- appear in the camera FOV as stray geometry (Bug 3 fix).
-- They remain as thin static physics walls to bound the play space,
-- but at y=-2.0 they are invisible from the floating platform area.
createStaticBox(0, -2.0, 30,  30, 0.25, 0.3,  0.3, 0.22, 0.18)
createStaticBox(0, -2.0, -30, 30, 0.25, 0.3,  0.3, 0.22, 0.18)
createStaticBox(30, -2.0, 0,  0.3, 0.25, 30,  0.3, 0.22, 0.18)
createStaticBox(-30, -2.0, 0, 0.3, 0.25, 30,  0.3, 0.22, 0.18)

--------------------------------------------------------------------
-- ALTAR: Central raised platform for the final artifact
--------------------------------------------------------------------
-- Altar base
createStaticBox(0, 0.4, 0, 1.5, 0.4, 1.5, 0.7, 0.55, 0.40)
-- Altar top step
createStaticBox(0, 0.9, 0, 0.8, 0.1, 0.8, 0.75, 0.62, 0.45)

--------------------------------------------------------------------
-- STAIRWAY PLATFORMS: 4 ascending stepping stones from spawn to arena
-- Player spawns at (0, 3, -15) and jumps across these to reach the arena.
--------------------------------------------------------------------
-- Step 1: spawn platform (wide, safe landing)
createStaticBox(0, 2.5, -15, 2.5, 0.3, 2.5, 0.65, 0.52, 0.40)
-- Step 2: slightly higher
createStaticBox(0, 1.8, -11, 2.0, 0.25, 2.0, 0.62, 0.50, 0.38)
-- Step 3: closer to arena height
createStaticBox(0, 1.0, -7.5, 1.8, 0.25, 1.8, 0.60, 0.48, 0.36)
-- Step 4: final jump onto arena edge
createStaticBox(0, 0.3, -4.5, 1.5, 0.2, 1.5, 0.58, 0.46, 0.34)

--------------------------------------------------------------------
-- STATIC FLOATING PLATFORMS: 5 platforms at varying heights
-- Arranged in a ring around the central arena.
--------------------------------------------------------------------
local staticPlatforms = {
    -- NE platform (guardian 1)
    { x =  10, y = 3.0, z = -8,  sx = 2.5, sy = 0.3, sz = 2.5, r = 0.42, g = 0.32, b = 0.25 },
    -- NW platform (guardian 2)
    { x = -10, y = 4.0, z = -8,  sx = 2.0, sy = 0.3, sz = 2.0, r = 0.4,  g = 0.3,  b = 0.23 },
    -- SE platform (gem pickup)
    { x =  12, y = 2.0, z =  6,  sx = 2.0, sy = 0.25, sz = 2.0, r = 0.44, g = 0.34, b = 0.26 },
    -- SW platform (guardian 3)
    { x = -11, y = 5.0, z =  7,  sx = 2.5, sy = 0.3, sz = 2.5, r = 0.38, g = 0.28, b = 0.22 },
    -- Far north high platform (gem pickup)
    { x =  0,  y = 6.0, z =  14, sx = 1.8, sy = 0.25, sz = 1.8, r = 0.46, g = 0.36, b = 0.28 },
}

for _, p in ipairs(staticPlatforms) do
    createStaticBox(p.x, p.y, p.z, p.sx, p.sy, p.sz, p.r, p.g, p.b)
end

--------------------------------------------------------------------
-- Helper: create a kinematic box entity (for moving platforms)
-- Kinematic bodies can be repositioned each frame and correctly
-- interact with dynamic bodies (player, enemies). Static bodies
-- should NOT be moved after creation — their physics stays fixed.
--------------------------------------------------------------------
local function createKinematicBox(x, y, z, sx, sy, sz, r, g, b)
    local ent = 0
    if cubeMesh ~= 0 then
        ent = ffe.createEntity3D(cubeMesh, x, y, z)
        if ent ~= 0 then
            ffe.setTransform3D(ent, x, y, z, 0, 0, 0, sx, sy, sz)
            ffe.setMeshColor(ent, r, g, b, 1.0)
            ffe.setMeshSpecular(ent, 0.2, 0.15, 0.1, 16)
            ffe.createPhysicsBody(ent, {
                shape       = "box",
                halfExtents = { sx * 0.5, sy * 0.5, sz * 0.5 },
                motion      = "kinematic",
            })
        end
    end
    return ent
end

--------------------------------------------------------------------
-- MOVING PLATFORMS: 3 platforms oscillate on sine/cosine paths
-- Period ~3-4 seconds. Updated via ffe.every timer.
-- Uses kinematic bodies so physics position tracks the visual.
--------------------------------------------------------------------
local movingPlatforms = {}

-- Moving platform A: oscillates on X axis (connects NE region)
local movPlatA = createKinematicBox(7, 2.0, -3, 1.5, 0.2, 1.5, 0.55, 0.45, 0.3)
if movPlatA ~= 0 then
    movingPlatforms[#movingPlatforms + 1] = {
        entity = movPlatA,
        baseX  = 7,   baseY = 2.0, baseZ = -3,
        sx     = 1.5, sy    = 0.2, sz    = 1.5,
        ampX   = 3.0, ampY  = 0,   ampZ  = 0,
        speed  = 1.8,  -- radians per second
        offset = 0,
    }
end

-- Moving platform B: oscillates on Y axis (vertical elevator near SW)
local movPlatB = createKinematicBox(-7, 1.5, 5, 1.5, 0.2, 1.5, 0.55, 0.45, 0.3)
if movPlatB ~= 0 then
    movingPlatforms[#movingPlatforms + 1] = {
        entity = movPlatB,
        baseX  = -7,  baseY = 1.5, baseZ = 5,
        sx     = 1.5, sy    = 0.2, sz    = 1.5,
        ampX   = 0,   ampY  = 3.5, ampZ  = 0,
        speed  = 1.5,
        offset = 1.0,  -- phase offset
    }
end

-- Moving platform C: oscillates on Z axis (connects to far north, hard to reach)
local movPlatC = createKinematicBox(0, 5.5, 9, 1.2, 0.2, 1.2, 0.6, 0.5, 0.35)
if movPlatC ~= 0 then
    movingPlatforms[#movingPlatforms + 1] = {
        entity = movPlatC,
        baseX  = 0,   baseY = 5.5, baseZ = 9,
        sx     = 1.2, sy    = 0.2, sz    = 1.2,
        ampX   = 0,   ampY  = 0,   ampZ  = 4.0,
        speed  = 1.6,
        offset = 2.0,
    }
end

--------------------------------------------------------------------
-- STONE PILLARS: Tall pillars rising from platforms like ancient ruins
--------------------------------------------------------------------
-- Pillars on the central arena (4 corners)
createVisualBox(-5, 4, -5, 0.5, 4, 0.5, 0.38, 0.28, 0.22, 1.0)
createVisualBox( 5, 4, -5, 0.5, 4, 0.5, 0.38, 0.28, 0.22, 1.0)
createVisualBox(-5, 4,  5, 0.5, 4, 0.5, 0.38, 0.28, 0.22, 1.0)
createVisualBox( 5, 4,  5, 0.5, 4, 0.5, 0.38, 0.28, 0.22, 1.0)

-- Pillars on floating platforms (decorative ruins reaching into the sky)
createVisualBox( 10, 6.5, -8, 0.4, 3.5, 0.4, 0.36, 0.26, 0.2, 1.0)
createVisualBox(-10, 7.5, -8, 0.4, 3.5, 0.4, 0.36, 0.26, 0.2, 1.0)
createVisualBox(-11, 9.0,  7, 0.45, 4.0, 0.45, 0.34, 0.24, 0.18, 1.0)
createVisualBox(  0, 9.5, 14, 0.35, 3.5, 0.35, 0.34, 0.24, 0.18, 1.0)

-- Broken pillar stumps on the arena edge (atmosphere)
createVisualBox( 6, 1.0, 0, 0.4, 1.0, 0.4, 0.4, 0.3, 0.24, 1.0)
createVisualBox(-6, 0.8, 0, 0.35, 0.8, 0.35, 0.4, 0.3, 0.24, 1.0)
createVisualBox( 0, 0.6, 6, 0.3, 0.6, 0.3, 0.4, 0.3, 0.24, 1.0)

--------------------------------------------------------------------
-- BRAZIER BOWLS: Bright glowing fire pits on key platforms
-- Purely visual geometry — small bright orange/yellow boxes
--------------------------------------------------------------------
-- Central altar brazier (flanking the artifact)
createVisualBox(-1.2, 1.1, -1.2, 0.25, 0.15, 0.25, 0.5, 0.35, 0.2, 1.0)  -- bowl
createVisualBox(-1.2, 1.3, -1.2, 0.15, 0.1, 0.15, 1.0, 0.75, 0.2, 1.0)   -- flame
createVisualBox( 1.2, 1.1,  1.2, 0.25, 0.15, 0.25, 0.5, 0.35, 0.2, 1.0)  -- bowl
createVisualBox( 1.2, 1.3,  1.2, 0.15, 0.1, 0.15, 1.0, 0.75, 0.2, 1.0)   -- flame

-- NE platform brazier
createVisualBox(10, 3.5, -8, 0.2, 0.12, 0.2, 0.5, 0.35, 0.2, 1.0)
createVisualBox(10, 3.65, -8, 0.12, 0.08, 0.12, 1.0, 0.7, 0.15, 1.0)

-- SW platform brazier
createVisualBox(-11, 5.5, 7, 0.2, 0.12, 0.2, 0.5, 0.35, 0.2, 1.0)
createVisualBox(-11, 5.65, 7, 0.12, 0.08, 0.12, 1.0, 0.7, 0.15, 1.0)

-- Spawn platform brazier (welcoming glow)
createVisualBox(1.5, 3.1, -15, 0.2, 0.12, 0.2, 0.5, 0.35, 0.2, 1.0)
createVisualBox(1.5, 3.25, -15, 0.12, 0.08, 0.12, 1.0, 0.8, 0.2, 1.0)

--------------------------------------------------------------------
-- POINT LIGHTS: Warm golden brazier glow on key platforms
-- (max 4 point lights; use 3 to leave headroom)
--------------------------------------------------------------------
-- Brazier on the central altar (bright warm gold, larger radius)
ffe.addPointLight(0, 0, 2.5, 0, 1.0, 0.85, 0.4, 15)

-- Brazier on NE floating platform
ffe.addPointLight(1, 10, 4.5, -8, 1.0, 0.75, 0.3, 12)

-- Brazier on SW floating platform
ffe.addPointLight(2, -11, 6.5, 7, 1.0, 0.7, 0.25, 12)

--------------------------------------------------------------------
-- Additional point lights for airy, well-lit summit (slots 3-7)
--------------------------------------------------------------------
-- Slot 3: NW floating platform (cool accent for contrast)
ffe.addPointLight(3, -10, 5.5, -8, 0.6, 0.7, 1.0, 10)

-- Slot 4: Spawn platform warm glow (so player sees where they start)
ffe.addPointLight(4, 0, 4.0, -15, 1.0, 0.9, 0.6, 10)

-- Slot 5: Far north high platform (rose-colored to match gem)
ffe.addPointLight(5, 0, 8.0, 14, 1.0, 0.4, 0.6, 10)

-- Slot 6: SE platform (topaz warm glow to match gem)
ffe.addPointLight(6, 12, 3.5, 6, 0.9, 0.7, 0.2, 8)

-- Slot 7: Moving platform region (subtle sky-blue accent)
ffe.addPointLight(7, -7, 4.0, 5, 0.5, 0.65, 1.0, 8)

--------------------------------------------------------------------
-- MAIN ARTIFACT: The FINAL artifact (damaged_helmet) on the central altar
--------------------------------------------------------------------
local mainArtifact = 0
local mainArtifactPos = { x = 0, y = 1.8, z = 0 }

if helmetMesh ~= 0 then
    mainArtifact = ffe.createEntity3D(helmetMesh,
        mainArtifactPos.x, mainArtifactPos.y, mainArtifactPos.z)
    if mainArtifact ~= 0 then
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

--------------------------------------------------------------------
-- OPTIONAL GEM PICKUPS: 3 on floating platforms + 1 hidden on moving platform
--------------------------------------------------------------------
local gems = {}
local gemPositions = {
    { x =  12, y = 2.6, z =  6   },  -- SE static platform
    { x =   0, y = 6.6, z =  14  },  -- far north high platform
    { x = -10, y = 4.6, z = -8   },  -- NW static platform
}
local gemColors = {
    { 0.9, 0.6, 0.1 },   -- topaz (warm, sunset theme)
    { 1.0, 0.3, 0.5 },   -- rose
    { 0.4, 0.8, 1.0 },   -- sky blue
}

for i, pos in ipairs(gemPositions) do
    if duckMesh ~= 0 then
        local gem = ffe.createEntity3D(duckMesh, pos.x, pos.y, pos.z)
        if gem ~= 0 then
            ffe.setTransform3D(gem, pos.x, pos.y, pos.z, 0, 0, 0, 0.008, 0.008, 0.008)
            local c = gemColors[i]
            ffe.setMeshColor(gem, c[1], c[2], c[3], 1.0)
            ffe.setMeshSpecular(gem, 1.0, 1.0, 1.0, 256)  -- extra shiny gems
            ffe.createPhysicsBody(gem, {
                shape       = "box",
                halfExtents = { 0.35, 0.35, 0.35 },
                motion      = "kinematic",
            })
            gems[gem] = { index = i, collected = false, pos = pos }
        end
    end

    -- Glowing pedestal under each gem
    if cubeMesh ~= 0 then
        local pedestal = ffe.createEntity3D(cubeMesh, pos.x, pos.y - 0.35, pos.z)
        if pedestal ~= 0 then
            local c = gemColors[i]
            ffe.setTransform3D(pedestal, pos.x, pos.y - 0.35, pos.z, 0, 0, 0, 0.5, 0.05, 0.5)
            ffe.setMeshColor(pedestal, c[1] * 0.6, c[2] * 0.6, c[3] * 0.6, 1.0)
            ffe.setMeshSpecular(pedestal, 0.6, 0.6, 0.6, 64)
        end
    end
end

-- Hidden bonus gem: on the Z-oscillating moving platform (hard to reach!)
local hiddenGemPos = { x = 0, y = 6.2, z = 9 }
local hiddenGem = 0
if duckMesh ~= 0 then
    hiddenGem = ffe.createEntity3D(duckMesh,
        hiddenGemPos.x, hiddenGemPos.y, hiddenGemPos.z)
    if hiddenGem ~= 0 then
        ffe.setTransform3D(hiddenGem,
            hiddenGemPos.x, hiddenGemPos.y, hiddenGemPos.z,
            0, 0, 0, 0.008, 0.008, 0.008)
        ffe.setMeshColor(hiddenGem, 1.0, 1.0, 0.2, 1.0)  -- bright gold
        ffe.setMeshSpecular(hiddenGem, 1.0, 1.0, 0.5, 128)
        ffe.createPhysicsBody(hiddenGem, {
            shape       = "box",
            halfExtents = { 0.35, 0.35, 0.35 },
            motion      = "kinematic",
        })
        gems[hiddenGem] = { index = 4, collected = false, pos = hiddenGemPos }
    end
end

-- Track all rotating entities (artifact + gems) for animation
local artifactEntities = {}
if mainArtifact ~= 0 then
    artifactEntities[#artifactEntities + 1] = {
        entity = mainArtifact,
        pos    = mainArtifactPos,
        scale  = 0.8,
        isMain = true,
    }
end
for entId, data in pairs(gems) do
    artifactEntities[#artifactEntities + 1] = {
        entity = entId,
        pos    = data.pos,
        scale  = 0.008,
        isMain = false,
    }
end

--------------------------------------------------------------------
-- Level state
--------------------------------------------------------------------
local artifactAngle  = 0
local gemsCollected  = 0
local totalGems      = 4   -- 3 normal + 1 hidden
local mainCollected  = false
local levelComplete  = false
local elapsed        = 0   -- global elapsed time for moving platforms
local victoryTimer   = 0   -- countdown after collecting final artifact

-- Set global artifact count for game.lua
totalArtifacts = 1  -- only the main artifact is needed for game.lua tracking

--------------------------------------------------------------------
-- GUARDIANS: 4 guardians on separate floating platforms
-- One per quadrant: 2 regular (80 HP), 1 tough (120 HP), 1 boss (200 HP)
--------------------------------------------------------------------
AI.reset()

-- Guardian 1 (NE platform): regular, 80 HP
local guardian1 = 0
local g1Mesh = (foxMesh ~= 0) and foxMesh or cubeMesh
if g1Mesh ~= 0 then
    local gx, gy, gz = 10, 3.8, -8
    guardian1 = ffe.createEntity3D(g1Mesh, gx, gy, gz)
    if guardian1 ~= 0 then
        ffe.setTransform3D(guardian1, gx, gy, gz, 0, 180, 0, 0.012, 0.012, 0.012)
        ffe.setMeshColor(guardian1, 0.9, 0.55, 0.2, 1.0)  -- warm amber fox, sunset tint
        ffe.setMeshSpecular(guardian1, 0.3, 0.2, 0.1, 16)
        ffe.createPhysicsBody(guardian1, {
            shape       = "box",
            halfExtents = { 0.7, 0.65, 1.0 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian1, {
            { x =  9,  y = 3.8, z = -9 },
            { x =  11, y = 3.8, z = -9 },
            { x =  11, y = 3.8, z = -7 },
            { x =  9,  y = 3.8, z = -7 },
        }, 80)
        AI.setEnemyColor(guardian1, 0.9, 0.55, 0.2, 1.0)
        local animCount = ffe.getAnimationCount3D(guardian1)
        if animCount > 1 then
            ffe.playAnimation3D(guardian1, 1, true)  -- clip 1 = Walk
            ffe.setAnimationSpeed3D(guardian1, 1.0)
        end
    end
end

-- Guardian 2 (NW platform): regular, 80 HP
local guardian2 = 0
local g2Mesh = (foxMesh ~= 0) and foxMesh or cubeMesh
if g2Mesh ~= 0 then
    local gx, gy, gz = -10, 4.8, -8
    guardian2 = ffe.createEntity3D(g2Mesh, gx, gy, gz)
    if guardian2 ~= 0 then
        ffe.setTransform3D(guardian2, gx, gy, gz, 0, 0, 0, 0.012, 0.012, 0.012)
        ffe.setMeshColor(guardian2, 0.9, 0.55, 0.2, 1.0)
        ffe.setMeshSpecular(guardian2, 0.3, 0.2, 0.1, 16)
        ffe.createPhysicsBody(guardian2, {
            shape       = "box",
            halfExtents = { 0.7, 0.65, 1.0 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian2, {
            { x = -11, y = 4.8, z = -9 },
            { x =  -9, y = 4.8, z = -9 },
            { x =  -9, y = 4.8, z = -7 },
            { x = -11, y = 4.8, z = -7 },
        }, 80)
        AI.setEnemyColor(guardian2, 0.9, 0.55, 0.2, 1.0)
        local animCount = ffe.getAnimationCount3D(guardian2)
        if animCount > 1 then
            ffe.playAnimation3D(guardian2, 1, true)  -- clip 1 = Walk
            ffe.setAnimationSpeed3D(guardian2, 1.0)
        end
    end
end

-- Guardian 3 (SW platform): tough, 120 HP
local guardian3 = 0
local g3Mesh = (foxMesh ~= 0) and foxMesh or cubeMesh
if g3Mesh ~= 0 then
    local gx, gy, gz = -11, 5.8, 7
    guardian3 = ffe.createEntity3D(g3Mesh, gx, gy, gz)
    if guardian3 ~= 0 then
        -- Slightly larger scale for the tougher guardian
        ffe.setTransform3D(guardian3, gx, gy, gz, 0, 90, 0, 0.015, 0.015, 0.015)
        ffe.setMeshColor(guardian3, 0.7, 0.35, 0.1, 1.0)  -- darker amber, meaner
        ffe.setMeshSpecular(guardian3, 0.4, 0.2, 0.1, 32)
        ffe.createPhysicsBody(guardian3, {
            shape       = "box",
            halfExtents = { 0.8, 0.7, 1.1 },
            motion      = "dynamic",
            mass        = 4.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian3, {
            { x = -12, y = 5.8, z =  6 },
            { x = -10, y = 5.8, z =  6 },
            { x = -10, y = 5.8, z =  8 },
            { x = -12, y = 5.8, z =  8 },
        }, 120)
        AI.setEnemyColor(guardian3, 0.7, 0.35, 0.1, 1.0)
        local animCount = ffe.getAnimationCount3D(guardian3)
        if animCount > 2 then
            ffe.playAnimation3D(guardian3, 2, true)  -- clip 2 = Run (more aggressive)
            ffe.setAnimationSpeed3D(guardian3, 1.2)
        elseif animCount > 1 then
            ffe.playAnimation3D(guardian3, 1, true)
            ffe.setAnimationSpeed3D(guardian3, 1.2)
        end
    end
end

-- Guardian 4 (central arena): BOSS, 200 HP, larger, gold tint
-- Boss uses cube.glb for a visually distinct, monolithic appearance.
local bossGuardian = 0
if cubeMesh ~= 0 then
    local gx, gy, gz = 4, 0.8, 4
    bossGuardian = ffe.createEntity3D(cubeMesh, gx, gy, gz)
    if bossGuardian ~= 0 then
        -- 1.5x scale compared to regular guardians
        ffe.setTransform3D(bossGuardian, gx, gy, gz, 0, 0, 0, 2.1, 1.95, 2.1)
        -- Gold tint: the ancient summit guardian
        ffe.setMeshColor(bossGuardian, 1.0, 0.8, 0.2, 1.0)
        ffe.setMeshSpecular(bossGuardian, 0.9, 0.7, 0.3, 64)
        ffe.createPhysicsBody(bossGuardian, {
            shape       = "box",
            halfExtents = { 1.0, 0.9, 1.3 },
            motion      = "dynamic",
            mass        = 6.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(bossGuardian, {
            { x =  4, y = 0.8, z =  4 },
            { x = -4, y = 0.8, z =  4 },
            { x = -4, y = 0.8, z = -4 },
            { x =  4, y = 0.8, z = -4 },
        }, 200)
        AI.setEnemyColor(bossGuardian, 1.0, 0.8, 0.2, 1.0)  -- gold boss tint
    end
end

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

    -- Check if it is the main artifact (victory condition)
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

        -- Victory sequence!
        if sfxCollect then ffe.playSound(sfxCollect, 1.0) end
        ffe.cameraShake(2.0, 0.5)

        if HUD then
            HUD.showPrompt("THE FINAL ARTIFACT! You have conquered The Summit!", 5.0)
        end
        ffe.log("[Level3] FINAL ARTIFACT COLLECTED! Victory!")

        -- Trigger game.lua artifact collection (starts LEVEL_COMPLETE state)
        if collectArtifact then collectArtifact() end

        -- Start victory timer for dramatic delay
        victoryTimer = 3.0
        levelComplete = true
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
        ffe.log("[Level3] Gem collected: " .. tostring(gemsCollected)
            .. "/" .. tostring(totalGems))
        return
    end
end)

--------------------------------------------------------------------
-- Player spawn (on first stepping stone platform)
-- Query terrain height at spawn XZ so the player lands on the
-- surface even when the summit heightmap raises the ground above Y=0.
--------------------------------------------------------------------
local terrainSurfaceY = ffe.getTerrainHeight(0, -11) or 0
ffe.log("[Level3] Terrain surface at spawn XZ(0,-11): " .. tostring(terrainSurfaceY))
local SPAWN_Y = math.max(terrainSurfaceY + 2.5, 2.5)
Player.create(0, SPAWN_Y, -11, cesiumMesh)
Camera.setPosition(0, SPAWN_Y + 3, -14)
Camera.setYawPitch(180, -20)  -- Camera behind (south), looking north toward arena and floating platforms; negative pitch = looking up toward horizon

if HUD then
    HUD.showPrompt("The Summit -- Reach the central altar and claim the final artifact!", 5.0)
end

--------------------------------------------------------------------
-- Per-frame level tick
--------------------------------------------------------------------
local TICK_RATE = 0.016  -- ~60 Hz

ffe.every(TICK_RATE, function()
    if getGameState and getGameState() ~= "PLAYING" then return end

    elapsed = elapsed + TICK_RATE

    -- ================================================================
    -- Moving platforms: update positions with sine/cosine oscillation
    -- ================================================================
    for _, mp in ipairs(movingPlatforms) do
        local t = elapsed * mp.speed + mp.offset
        local nx = mp.baseX + mp.ampX * math.sin(t)
        local ny = mp.baseY + mp.ampY * math.sin(t)
        local nz = mp.baseZ + mp.ampZ * math.sin(t)
        ffe.setTransform3D(mp.entity, nx, ny, nz, 0, 0, 0, mp.sx, mp.sy, mp.sz)
    end

    -- Update hidden gem position to track moving platform C
    if hiddenGem ~= 0 and not (gems[hiddenGem] and gems[hiddenGem].collected) then
        local mp = movingPlatforms[3]  -- platform C
        if mp then
            local t = elapsed * mp.speed + mp.offset
            local pz = mp.baseZ + mp.ampZ * math.sin(t)
            local gemY = mp.baseY + mp.ampY * math.sin(t) + 0.7
            hiddenGemPos.z = pz
            hiddenGemPos.y = gemY

            -- Update gem position in the artifact tracking table
            for _, ad in ipairs(artifactEntities) do
                if ad.entity == hiddenGem then
                    ad.pos.z = pz
                    ad.pos.y = gemY
                    break
                end
            end
        end
    end

    -- ================================================================
    -- Show gem counter (top-right area, below enemies)
    -- ================================================================
    if gemsCollected < totalGems then
        local sw = ffe.getScreenWidth()
        local gemStr = "Gems: " .. tostring(gemsCollected) .. "/" .. tostring(totalGems)
        local gemX = sw - (#gemStr * 16) - 16
        ffe.drawRect(gemX - 4, 56, #gemStr * 16 + 8, 22, 0, 0, 0, 0.5)
        ffe.drawText(gemStr, gemX, 58, 2, 0.9, 0.7, 0.2, 0.9)
    end

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
    -- Victory timer: dramatic pause then level complete
    -- ================================================================
    if levelComplete and victoryTimer > 0 then
        victoryTimer = victoryTimer - TICK_RATE

        -- Periodic camera shakes during victory sequence
        if victoryTimer > 1.5 and math.floor(victoryTimer * 4) % 2 == 0 then
            ffe.cameraShake(0.5, 0.1)
        end
    end

    -- ================================================================
    -- Fall detection: respawn at central arena if player falls too low.
    -- Use absolute Y threshold (terrain spans [0,60] not centered at
    -- origin, so getTerrainHeight returns 0 for our play area).
    -- ================================================================
    if Player then
        local px, py, pz = Player.getPosition()
        if py < -15 then
            Player.cleanup()
            Player.create(0, SPAWN_Y, -11, cesiumMesh)
            Camera.setPosition(0, SPAWN_Y + 3, -14)
            if HUD then HUD.showPrompt("Lost in the clouds... regaining footing!", 2.0) end
        end
    end
end)

--------------------------------------------------------------------
-- Done
--------------------------------------------------------------------
ffe.log("[Level3] The Summit loaded successfully!")
ffe.log("[Level3] Objectives:")
ffe.log("[Level3]   1. Jump across the floating stepping stones to reach the arena")
ffe.log("[Level3]   2. Navigate moving platforms to reach distant areas")
ffe.log("[Level3]   3. Defeat or avoid the 4 summit guardians")
ffe.log("[Level3]   4. Collect the Final Artifact from the central altar")
ffe.log("[Level3]   5. Explore for hidden gems on floating platforms")
ffe.log("[Level3] Controls: WASD move, SPACE jump, Mouse look, LMB attack, ESC pause")
ffe.log("[Level3] Gamepad: L-Stick move, R-Stick camera, A jump, X attack, Y interact")
