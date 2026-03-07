-- game.lua -- "Echoes of the Ancients" showcase game for FFE
--
-- Main entry point. Manages game state machine and level sequencing.
-- Run with: ./ffe_runtime examples/showcase/game.lua
--
-- State machine: MENU -> PLAYING -> PAUSED -> LEVEL_COMPLETE -> GAME_OVER
-- For M1 prototype: skip menu, go straight to test scene.

--------------------------------------------------------------------
-- Game states
--------------------------------------------------------------------
local STATE_MENU           = "MENU"
local STATE_PLAYING        = "PLAYING"
local STATE_PAUSED         = "PAUSED"
local STATE_LEVEL_COMPLETE = "LEVEL_COMPLETE"
local STATE_GAME_OVER      = "GAME_OVER"

local gameState    = STATE_PLAYING  -- skip menu for prototype
local currentLevel = 0              -- 0 = not loaded yet
local totalLevels  = 3
local artifactCount    = 0
local totalArtifacts   = 1          -- per level

--------------------------------------------------------------------
-- Level registry — maps level number to its loader script path
--------------------------------------------------------------------
local LEVELS = {
    [1] = "levels/test_level.lua",
    -- [2] = "levels/level2_courtyard.lua",  -- M2
    -- [3] = "levels/level3_temple.lua",     -- M3
}

--------------------------------------------------------------------
-- Module globals (populated by ffe.loadScene loading the modules)
--
-- Since require is blocked in the FFE sandbox, we use ffe.loadScene
-- to execute Lua files. Each module sets globals (Player, Camera,
-- HUD, Combat, AI) that persist in the Lua state.
--------------------------------------------------------------------
-- Forward-declare module tables so we can reference them before load.
Player = nil
Camera = nil
HUD    = nil
Combat = nil
AI     = nil

--------------------------------------------------------------------
-- Load shared modules via ffe.loadScene (executes Lua in same state)
--------------------------------------------------------------------
ffe.loadScene("lib/player.lua")
ffe.loadScene("lib/camera.lua")
ffe.loadScene("lib/hud.lua")
ffe.loadScene("lib/combat.lua")
ffe.loadScene("lib/ai.lua")

ffe.log("[Showcase] Modules loaded")

--------------------------------------------------------------------
-- Scene setup — common environment
--------------------------------------------------------------------
-- Background color (visible when no skybox)
ffe.setBackgroundColor(0.12, 0.14, 0.22)

--------------------------------------------------------------------
-- Load level
--------------------------------------------------------------------
local function loadLevel(levelNum)
    if levelNum < 1 or levelNum > totalLevels then
        ffe.log("[Showcase] Invalid level number: " .. tostring(levelNum))
        return
    end

    local path = LEVELS[levelNum]
    if not path then
        ffe.log("[Showcase] Level " .. tostring(levelNum) .. " not implemented yet")
        return
    end

    -- Reset per-level state
    artifactCount  = 0
    totalArtifacts = 1
    currentLevel   = levelNum
    gameState      = STATE_PLAYING

    ffe.log("[Showcase] Loading level " .. tostring(levelNum) .. ": " .. path)
    ffe.loadScene(path)
end

--------------------------------------------------------------------
-- Global accessors for level scripts
--------------------------------------------------------------------
function collectArtifact()
    artifactCount = artifactCount + 1
    ffe.log("[Showcase] Artifact collected: " .. tostring(artifactCount)
        .. "/" .. tostring(totalArtifacts))
    if artifactCount >= totalArtifacts then
        gameState = STATE_LEVEL_COMPLETE
        ffe.log("[Showcase] Level complete!")
    end
end

function triggerGameOver()
    gameState = STATE_GAME_OVER
    ffe.log("[Showcase] Game Over")
end

function getGameState()
    return gameState
end

--------------------------------------------------------------------
-- Start: load test level
--------------------------------------------------------------------
loadLevel(1)

--------------------------------------------------------------------
-- FPS tracking
--------------------------------------------------------------------
local fpsTimer   = 0
local fpsFrames  = 0
local fpsDisplay = 0

--------------------------------------------------------------------
-- update(entityId, dt) — called per tick by ffe_runtime
--------------------------------------------------------------------
function update(entityId, dt)
    -- FPS counter
    fpsFrames = fpsFrames + 1
    fpsTimer  = fpsTimer + dt
    if fpsTimer >= 0.5 then
        fpsDisplay = math.floor(fpsFrames / fpsTimer + 0.5)
        fpsFrames  = 0
        fpsTimer   = 0
    end

    -- ESC to quit
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
        return
    end

    -- Pause toggle (P key)
    if ffe.isKeyPressed(ffe.KEY_P) then
        if gameState == STATE_PLAYING then
            gameState = STATE_PAUSED
        elseif gameState == STATE_PAUSED then
            gameState = STATE_PLAYING
        end
    end

    -- State machine
    if gameState == STATE_PLAYING then
        -- Update game systems
        if Player then Player.update(dt) end
        if Camera then Camera.update(dt) end
        if Combat then Combat.update(dt) end
        if AI     then AI.updateAll(dt)  end

    elseif gameState == STATE_PAUSED then
        -- Draw pause overlay only, no updates

    elseif gameState == STATE_LEVEL_COMPLETE then
        -- Advance to next level on ENTER
        if ffe.isKeyPressed(ffe.KEY_ENTER) then
            if currentLevel < totalLevels and LEVELS[currentLevel + 1] then
                loadLevel(currentLevel + 1)
            else
                ffe.log("[Showcase] All levels complete! Congratulations!")
                gameState = STATE_MENU
            end
        end

    elseif gameState == STATE_GAME_OVER then
        -- Restart on ENTER
        if ffe.isKeyPressed(ffe.KEY_ENTER) then
            loadLevel(currentLevel)
        end
    end

    -- HUD (drawn every frame regardless of state)
    local sw = ffe.getScreenWidth()
    local sh = ffe.getScreenHeight()

    if HUD then
        local hp    = Player and Player.getHealth() or 0
        local maxHp = 100
        local lvlName = "Level " .. tostring(currentLevel)
        HUD.draw(hp, maxHp, artifactCount, totalArtifacts, lvlName)
    end

    -- FPS display (top-right)
    local fpsStr = "FPS: " .. tostring(fpsDisplay)
    ffe.drawText(fpsStr, sw - (#fpsStr * 16) - 8, 8, 2, 0.5, 0.9, 0.5, 1)

    -- State-specific overlays
    if gameState == STATE_PAUSED then
        ffe.drawRect(0, sh / 2 - 30, sw, 60, 0, 0, 0, 0.7)
        ffe.drawText("PAUSED  -  Press P to resume", sw / 2 - 220, sh / 2 - 10, 3, 1, 1, 1, 1)
    elseif gameState == STATE_LEVEL_COMPLETE then
        ffe.drawRect(0, sh / 2 - 50, sw, 100, 0, 0, 0, 0.7)
        ffe.drawText("LEVEL COMPLETE!", sw / 2 - 150, sh / 2 - 30, 4, 0.2, 1, 0.4, 1)
        ffe.drawText("Press ENTER to continue", sw / 2 - 180, sh / 2 + 10, 3, 0.8, 0.8, 0.8, 1)
    elseif gameState == STATE_GAME_OVER then
        ffe.drawRect(0, sh / 2 - 50, sw, 100, 0, 0, 0, 0.7)
        ffe.drawText("GAME OVER", sw / 2 - 100, sh / 2 - 30, 4, 1, 0.2, 0.2, 1)
        ffe.drawText("Press ENTER to retry", sw / 2 - 160, sh / 2 + 10, 3, 0.8, 0.8, 0.8, 1)
    end

    -- Title bar
    ffe.drawRect(0, 0, sw, 30, 0, 0, 0, 0.5)
    ffe.drawText("ECHOES OF THE ANCIENTS", 12, 6, 2, 0.9, 0.75, 0.4, 1)
end

--------------------------------------------------------------------
-- shutdown() — called by engine before Lua state closes
--------------------------------------------------------------------
function shutdown()
    ffe.log("[Showcase] Shutdown")
    if Player then Player.cleanup() end
    ffe.disableFog()
    ffe.disableShadows()
    ffe.removePointLight(0)
    ffe.removePointLight(1)
    ffe.removePointLight(2)
    ffe.removePointLight(3)
    ffe.unloadSkybox()
end
