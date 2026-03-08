-- game.lua -- "Echoes of the Ancients" showcase game for FFE
--
-- Main entry point. Manages game state machine and level sequencing.
-- Run with: ./ffe_runtime examples/showcase/game.lua
--
-- State machine: MENU -> PLAYING -> PAUSED -> LEVEL_COMPLETE -> GAME_OVER -> VICTORY

--------------------------------------------------------------------
-- Game states
--------------------------------------------------------------------
local STATE_MENU           = "MENU"
local STATE_PLAYING        = "PLAYING"
local STATE_PAUSED         = "PAUSED"
local STATE_LEVEL_COMPLETE = "LEVEL_COMPLETE"
local STATE_GAME_OVER      = "GAME_OVER"
local STATE_VICTORY        = "VICTORY"

local gameState    = STATE_MENU     -- start at title screen
local currentLevel = 0              -- 0 = not loaded yet
local totalLevels  = 3
local artifactCount    = 0
local totalArtifacts   = 1          -- per level
local levelCompleteTimer = 0

-- Victory tracking: cumulative stats across all 3 levels
local totalArtifactsAll  = 0   -- total artifacts collected across all levels
local totalPlayTime      = 0   -- seconds of active play time
local victoryScreenTimer = 0   -- animation timer for congratulations screen

-- Per-level vegetation state (cleaned up at every level transition)
local grassHandle  = nil
local treesActive  = false
local waterHandle  = nil

--------------------------------------------------------------------
-- Level registry — maps level number to its loader script path
--------------------------------------------------------------------
local LEVELS = {
    [1] = "levels/level1.lua",
    [2] = "levels/level2.lua",
    [3] = "levels/level3.lua",
}

local LEVEL_NAMES = {
    [1] = "The Courtyard",
    [2] = "The Temple",
    [3] = "The Summit",
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
Menus  = nil

--------------------------------------------------------------------
-- Load shared modules via ffe.loadScene (executes Lua in same state)
--------------------------------------------------------------------
ffe.loadScene("lib/player.lua")
ffe.loadScene("lib/camera.lua")
ffe.loadScene("lib/hud.lua")
ffe.loadScene("lib/combat.lua")
ffe.loadScene("lib/ai.lua")
ffe.loadScene("lib/menus.lua")

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

    -- Clean up previous level (if any)
    if currentLevel > 0 then
        if Player then Player.cleanup() end
        if AI then AI.reset() end
        ffe.cancelAllTimers()
        -- Clean up vegetation BEFORE unloading terrain
        if grassHandle then
            ffe.removeVegetationPatch(grassHandle)
            grassHandle = nil
        end
        if treesActive then
            ffe.clearTrees()
            treesActive = false
        end
        if waterHandle then
            ffe.destroyWater(waterHandle)
            waterHandle = nil
        end
        -- Unload terrain and disable post-processing BEFORE destroying entities
        ffe.unloadTerrain()
        ffe.disablePostProcessing()
        ffe.disableSSAO()
        ffe.destroyAllEntities()
        -- Reset lighting to defaults
        ffe.disableFog()
        ffe.disableShadows()
        ffe.removePointLight(0)
        ffe.removePointLight(1)
        ffe.removePointLight(2)
        ffe.removePointLight(3)
        ffe.removePointLight(4)
        ffe.removePointLight(5)
        ffe.removePointLight(6)
        ffe.removePointLight(7)
        ffe.unloadSkybox()
        ffe.stopMusic()
    end

    -- Reset per-level state
    artifactCount  = 0
    totalArtifacts = 1
    currentLevel   = levelNum
    gameState      = STATE_PLAYING

    -- Capture mouse for FPS-style look (Bug 6)
    ffe.setCursorCaptured(true)

    ffe.log("[Showcase] Loading level " .. tostring(levelNum) .. ": " .. path)
    ffe.loadScene(path)
end

--------------------------------------------------------------------
-- Vegetation state setters (called by level scripts)
--------------------------------------------------------------------
function setGrassHandle(h)  grassHandle = h end
function setTreesActive(v)  treesActive = v end
function setWaterHandle(h)  waterHandle = h end

--------------------------------------------------------------------
-- Global accessors for level scripts
--------------------------------------------------------------------
function collectArtifact()
    artifactCount = artifactCount + 1
    totalArtifactsAll = totalArtifactsAll + 1
    ffe.log("[Showcase] Artifact collected: " .. tostring(artifactCount)
        .. "/" .. tostring(totalArtifacts)
        .. " (total: " .. tostring(totalArtifactsAll) .. ")")
    if artifactCount >= totalArtifacts then
        gameState = STATE_LEVEL_COMPLETE
        levelCompleteTimer = 0
        ffe.setCursorCaptured(false)  -- Release cursor for level-complete screen
        ffe.log("[Showcase] Level complete!")
    end
end

function triggerGameOver()
    gameState = STATE_GAME_OVER
    ffe.setCursorCaptured(false)  -- Release cursor for game-over screen
    ffe.log("[Showcase] Game Over")
end

function getGameState()
    return gameState
end

--------------------------------------------------------------------
-- Helper: full cleanup for returning to menu
--------------------------------------------------------------------
local function cleanupToMenu()
    if currentLevel > 0 then
        if Player then Player.cleanup() end
        if AI then AI.reset() end
        ffe.cancelAllTimers()
        -- Clean up vegetation BEFORE unloading terrain
        if grassHandle then
            ffe.removeVegetationPatch(grassHandle)
            grassHandle = nil
        end
        if treesActive then
            ffe.clearTrees()
            treesActive = false
        end
        if waterHandle then
            ffe.destroyWater(waterHandle)
            waterHandle = nil
        end
        -- Unload terrain and disable post-processing BEFORE destroying entities
        ffe.unloadTerrain()
        ffe.disablePostProcessing()
        ffe.disableSSAO()
        ffe.destroyAllEntities()
        ffe.disableFog()
        ffe.disableShadows()
        ffe.removePointLight(0)
        ffe.removePointLight(1)
        ffe.removePointLight(2)
        ffe.removePointLight(3)
        ffe.removePointLight(4)
        ffe.removePointLight(5)
        ffe.removePointLight(6)
        ffe.removePointLight(7)
        ffe.unloadSkybox()
        ffe.stopMusic()
    end
    currentLevel = 0
    artifactCount = 0
    totalArtifactsAll = 0
    totalPlayTime = 0
    victoryScreenTimer = 0
    gameState = STATE_MENU
    -- Release mouse cursor for menu navigation (Bug 6)
    ffe.setCursorCaptured(false)
    if Menus then Menus.resetTitle() end
    -- Set dark atmospheric background for title screen
    ffe.setBackgroundColor(0.04, 0.03, 0.06)
end

--------------------------------------------------------------------
-- Start: show title screen (do NOT load a level yet)
--------------------------------------------------------------------
if Menus then Menus.resetTitle() end
ffe.setBackgroundColor(0.04, 0.03, 0.06)

--------------------------------------------------------------------
-- FPS tracking
--------------------------------------------------------------------
local fpsTimer   = 0
local fpsFrames  = 0
local fpsDisplay = 0

--------------------------------------------------------------------
-- Helper: draw a decorative border (four thin rects)
--------------------------------------------------------------------
local function drawBorder(x, y, w, h, r, g, b, a, thickness)
    thickness = thickness or 2
    ffe.drawRect(x, y, w, thickness, r, g, b, a)
    ffe.drawRect(x, y + h - thickness, w, thickness, r, g, b, a)
    ffe.drawRect(x, y, thickness, h, r, g, b, a)
    ffe.drawRect(x + w - thickness, y, thickness, h, r, g, b, a)
end

--------------------------------------------------------------------
-- Helper: center text X position (charWidth = 8 * scale)
--------------------------------------------------------------------
local function centerTextX(text, scale)
    local sw = ffe.getScreenWidth()
    return math.floor((sw - #text * 8 * scale) / 2)
end

--------------------------------------------------------------------
-- Victory screen: draw animated star-like sparkle rects
--------------------------------------------------------------------
local starSeeds = {}
for i = 1, 20 do
    starSeeds[i] = {
        xFrac  = math.random() * 1000 / 1000,  -- 0..1 fraction of screen width
        yFrac  = math.random() * 1000 / 1000,  -- 0..1 fraction of screen height
        speed  = 1.5 + math.random() * 3000 / 1000,  -- pulse speed
        phase  = math.random() * 6280 / 1000,  -- initial phase
        size   = 2 + math.floor(math.random() * 4),
    }
end

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

    local sw = ffe.getScreenWidth()
    local sh = ffe.getScreenHeight()

    --------------------------------------------------------------------
    -- STATE: MENU — title screen
    --------------------------------------------------------------------
    if gameState == STATE_MENU then
        -- ESC quits from the menu
        if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
            ffe.requestShutdown()
            return
        end

        -- Draw the title screen
        if Menus then Menus.drawTitle(dt) end

        -- ENTER or gamepad START starts the game
        local startPressed = ffe.isKeyPressed(ffe.KEY_ENTER)
        if ffe.isGamepadConnected(0) and ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_START) then
            startPressed = true
        end
        if ffe.isGamepadConnected(0) and ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_A) then
            startPressed = true
        end

        if startPressed then
            ffe.setBackgroundColor(0.12, 0.14, 0.22)
            loadLevel(1)
        end

        -- FPS display on menu too
        local fpsStr = "FPS: " .. tostring(fpsDisplay)
        ffe.drawText(fpsStr, sw - (#fpsStr * 16) - 8, 8, 2, 0.5, 0.9, 0.5, 1)
        return
    end

    --------------------------------------------------------------------
    -- STATE: PAUSED — pause menu with navigation
    --------------------------------------------------------------------
    if gameState == STATE_PAUSED then
        -- Handle pause menu input
        if Menus then
            local action = Menus.handlePauseInput(dt)
            if action == "resume" then
                gameState = STATE_PLAYING
                ffe.setCursorCaptured(true)  -- Re-capture cursor for gameplay
            elseif action == "restart" then
                loadLevel(currentLevel)
            elseif action == "quit" then
                cleanupToMenu()
            end

            -- Draw the game HUD underneath (frozen)
            if HUD then
                local hp    = Player and Player.getHealth() or 0
                local maxHp = 100
                local lvlName = "Level " .. tostring(currentLevel)
                if LEVEL_NAMES[currentLevel] then
                    lvlName = lvlName .. " - " .. LEVEL_NAMES[currentLevel]
                end
                HUD.draw(hp, maxHp, artifactCount, totalArtifacts, lvlName, 0)
            end

            -- Draw pause overlay on top
            Menus.drawPause(dt, Menus.getPauseSelected())
        end

        -- FPS
        local fpsStr = "FPS: " .. tostring(fpsDisplay)
        ffe.drawText(fpsStr, sw - (#fpsStr * 16) - 8, 8, 2, 0.5, 0.9, 0.5, 1)

        -- Title bar
        ffe.drawRect(0, 0, sw, 30, 0, 0, 0, 0.5)
        ffe.drawText("ECHOES OF THE ANCIENTS", 12, 6, 2, 0.9, 0.75, 0.4, 1)
        return
    end

    --------------------------------------------------------------------
    -- ESC during gameplay -> pause (not quit)
    --------------------------------------------------------------------
    if gameState == STATE_PLAYING then
        if ffe.isKeyPressed(ffe.KEY_ESCAPE) or ffe.isKeyPressed(ffe.KEY_P) then
            gameState = STATE_PAUSED
            ffe.setCursorCaptured(false)  -- Release cursor for pause menu
            if Menus then Menus.resetPause() end
        end
        -- Also allow gamepad START to pause
        if ffe.isGamepadConnected(0) and ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_START) then
            gameState = STATE_PAUSED
            ffe.setCursorCaptured(false)  -- Release cursor for pause menu
            if Menus then Menus.resetPause() end
        end
    end

    --------------------------------------------------------------------
    -- STATE: PLAYING — normal gameplay
    --------------------------------------------------------------------
    if gameState == STATE_PLAYING then
        totalPlayTime = totalPlayTime + dt

        if Player then Player.update(dt) end
        if Camera then Camera.update(dt) end
        if Combat then Combat.update(dt) end
        if AI     then AI.updateAll(dt)  end

    --------------------------------------------------------------------
    -- STATE: LEVEL_COMPLETE
    --------------------------------------------------------------------
    elseif gameState == STATE_LEVEL_COMPLETE then
        levelCompleteTimer = (levelCompleteTimer or 0) + dt
        local advancePressed = ffe.isKeyPressed(ffe.KEY_ENTER)
        if ffe.isGamepadConnected(0) and ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_A) then
            advancePressed = true
        end
        if advancePressed and levelCompleteTimer > 1.0 then
            if currentLevel < totalLevels and LEVELS[currentLevel + 1] then
                levelCompleteTimer = 0
                loadLevel(currentLevel + 1)
            else
                ffe.log("[Showcase] All levels complete! Congratulations!")
                gameState = STATE_VICTORY
                victoryScreenTimer = 0
                ffe.setCursorCaptured(false)  -- Release cursor for victory screen
                ffe.stopMusic()
            end
        end

    --------------------------------------------------------------------
    -- STATE: VICTORY — polished congratulations screen
    --------------------------------------------------------------------
    elseif gameState == STATE_VICTORY then
        victoryScreenTimer = victoryScreenTimer + dt
        -- ENTER returns to main menu after a delay
        local exitPressed = ffe.isKeyPressed(ffe.KEY_ENTER)
        if ffe.isGamepadConnected(0) and ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_A) then
            exitPressed = true
        end
        if exitPressed and victoryScreenTimer > 2.0 then
            cleanupToMenu()
            return
        end
        -- ESC also returns to menu
        if ffe.isKeyPressed(ffe.KEY_ESCAPE) and victoryScreenTimer > 1.0 then
            cleanupToMenu()
            return
        end

    --------------------------------------------------------------------
    -- STATE: GAME_OVER
    --------------------------------------------------------------------
    elseif gameState == STATE_GAME_OVER then
        local retryPressed = ffe.isKeyPressed(ffe.KEY_ENTER)
        if ffe.isGamepadConnected(0) and ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_A) then
            retryPressed = true
        end
        if retryPressed then
            loadLevel(currentLevel)
        end
        -- ESC goes back to menu from game over
        if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
            cleanupToMenu()
            return
        end
    end

    --------------------------------------------------------------------
    -- HUD (drawn every frame during gameplay states)
    --------------------------------------------------------------------
    if gameState == STATE_PLAYING or gameState == STATE_LEVEL_COMPLETE
       or gameState == STATE_GAME_OVER then
        if HUD then
            local hp    = Player and Player.getHealth() or 0
            local maxHp = 100
            local lvlName = "Level " .. tostring(currentLevel)
            if LEVEL_NAMES[currentLevel] then
                lvlName = lvlName .. " - " .. LEVEL_NAMES[currentLevel]
            end
            HUD.draw(hp, maxHp, artifactCount, totalArtifacts, lvlName, dt)
        end
    end

    -- FPS display (top-right)
    local fpsStr = "FPS: " .. tostring(fpsDisplay)
    ffe.drawText(fpsStr, sw - (#fpsStr * 16) - 8, 8, 2, 0.5, 0.9, 0.5, 1)

    --------------------------------------------------------------------
    -- State-specific overlays
    --------------------------------------------------------------------
    if gameState == STATE_LEVEL_COMPLETE then
        ffe.drawRect(0, sh / 2 - 60, sw, 120, 0, 0, 0, 0.7)
        local completeName = LEVEL_NAMES[currentLevel] or ("Level " .. tostring(currentLevel))
        ffe.drawText(completeName .. " COMPLETE!", centerTextX(completeName .. " COMPLETE!", 4), sh / 2 - 40, 4, 0.2, 1, 0.4, 1)
        if currentLevel < totalLevels and LEVELS[currentLevel + 1] then
            local contText = "Press ENTER to continue"
            ffe.drawText(contText, centerTextX(contText, 3), sh / 2 + 10, 3, 0.8, 0.8, 0.8, 1)
        else
            local resultText = "Press ENTER to see your results!"
            ffe.drawText(resultText, centerTextX(resultText, 3), sh / 2 + 10, 3, 1, 0.9, 0.3, 1)
        end

    elseif gameState == STATE_VICTORY then
        -- Full-screen dark overlay
        ffe.drawRect(0, 0, sw, sh, 0.03, 0.02, 0.06, 0.95)

        -- Animated star-like sparkle particles using drawRect
        for i = 1, #starSeeds do
            local s = starSeeds[i]
            local sx = s.xFrac * sw
            local sy = s.yFrac * sh
            local sparkleAlpha = 0.15 + 0.35 * math.max(0, math.sin(victoryScreenTimer * s.speed + s.phase))
            local sparkleSize = s.size
            -- Gold/white sparkle
            ffe.drawRect(sx - sparkleSize / 2, sy - sparkleSize / 2,
                sparkleSize, sparkleSize,
                1.0, 0.9, 0.5, sparkleAlpha)
            -- Tiny bright center
            if sparkleSize > 2 then
                ffe.drawRect(sx - 1, sy - 1, 2, 2, 1.0, 1.0, 0.9, sparkleAlpha * 1.5)
            end
        end

        -- Top gold border line
        local borderW = math.min(sw * 0.7, 700)
        local borderX = math.floor((sw - borderW) / 2)
        ffe.drawRect(borderX, sh * 0.12, borderW, 3, 0.95, 0.8, 0.3, 0.7)

        -- Animated gold title with pulsing brightness
        local pulse = 0.8 + 0.2 * math.sin(victoryScreenTimer * 2.5)
        local congratsText = "CONGRATULATIONS!"
        ffe.drawText(congratsText, centerTextX(congratsText, 5), math.floor(sh * 0.16), 5,
            pulse, pulse * 0.85, pulse * 0.3, 1)

        -- Bottom gold border line under title
        ffe.drawRect(borderX, sh * 0.16 + 48, borderW, 3, 0.95, 0.8, 0.3, 0.7)

        -- Subtitle lines
        local sub1 = "You have completed"
        local sub2 = "Echoes of the Ancients"
        ffe.drawText(sub1, centerTextX(sub1, 3), math.floor(sh * 0.28), 3, 0.8, 0.7, 0.5, 1)
        ffe.drawText(sub2, centerTextX(sub2, 4), math.floor(sh * 0.28) + 30, 4, 0.9, 0.75, 0.4, 1)

        -- Stats panel with gold border
        local panelW = math.min(sw * 0.5, 500)
        local panelH = 170
        local panelX = math.floor((sw - panelW) / 2)
        local panelY = math.floor(sh * 0.45)

        -- Panel background
        ffe.drawRect(panelX, panelY, panelW, panelH, 0.08, 0.04, 0.1, 0.8)
        -- Gold border around stats panel
        drawBorder(panelX, panelY, panelW, panelH, 0.95, 0.8, 0.3, 0.6, 2)

        -- Inner decorative line
        ffe.drawRect(panelX + 10, panelY + 8, panelW - 20, 1, 0.95, 0.8, 0.3, 0.3)

        local statsTitle = "FINAL STATS"
        ffe.drawText(statsTitle, centerTextX(statsTitle, 3), panelY + 16, 3, 1, 0.9, 0.5, 1)

        -- Decorative line under stats title
        ffe.drawRect(panelX + 20, panelY + 44, panelW - 40, 1, 0.95, 0.8, 0.3, 0.3)

        -- Stats with staggered reveal (each stat fades in after a delay)
        local statScale = 2
        local statBaseY = panelY + 56

        -- Artifacts
        local artifactStr = "Artifacts Collected: " .. tostring(totalArtifactsAll) .. " / " .. tostring(totalLevels)
        local artAlpha = math.min(1, math.max(0, (victoryScreenTimer - 0.5) * 2))
        ffe.drawText(artifactStr, centerTextX(artifactStr, statScale), statBaseY, statScale,
            0.9, 0.85, 0.7, artAlpha)

        -- Play time
        local totalMins = math.floor(totalPlayTime / 60)
        local totalSecs = math.floor(totalPlayTime % 60)
        local timeStr = "Play Time: " .. tostring(totalMins) .. ":" .. string.format("%02d", totalSecs)
        local timeAlpha = math.min(1, math.max(0, (victoryScreenTimer - 1.0) * 2))
        ffe.drawText(timeStr, centerTextX(timeStr, statScale), statBaseY + 30, statScale,
            0.9, 0.85, 0.7, timeAlpha)

        -- Levels completed
        local levelStr = "Levels Completed: " .. tostring(totalLevels) .. " / " .. tostring(totalLevels)
        local lvlAlpha = math.min(1, math.max(0, (victoryScreenTimer - 1.5) * 2))
        ffe.drawText(levelStr, centerTextX(levelStr, statScale), statBaseY + 60, statScale,
            0.9, 0.85, 0.7, lvlAlpha)

        -- Rating line (based on artifacts)
        local ratingAlpha = math.min(1, math.max(0, (victoryScreenTimer - 2.0) * 2))
        if ratingAlpha > 0 then
            local rating = "Adventurer"
            if totalArtifactsAll >= totalLevels then
                rating = "Master Explorer"
            elseif totalArtifactsAll >= 2 then
                rating = "Seasoned Traveler"
            end
            local ratingStr = "Rank: " .. rating
            ffe.drawText(ratingStr, centerTextX(ratingStr, statScale), statBaseY + 95, statScale,
                1.0, 0.9, 0.4, ratingAlpha)
        end

        -- Bottom gold border line
        ffe.drawRect(borderX, sh * 0.85, borderW, 3, 0.95, 0.8, 0.3, 0.5)

        -- Prompt to return to menu (blinking after delay)
        if victoryScreenTimer > 2.0 then
            local blinkAlpha = 0.4 + 0.6 * math.max(0, math.sin(victoryScreenTimer * 2.5))
            local menuText = "Press ENTER to return to menu"
            ffe.drawText(menuText, centerTextX(menuText, 3), math.floor(sh * 0.88), 3,
                0.8, 0.7, 0.5, blinkAlpha)
        end

        -- Credits line
        local creditText = "Built with FastFreeEngine"
        ffe.drawText(creditText, centerTextX(creditText, 2), sh - 32, 2, 0.3, 0.25, 0.45, 0.5)

    elseif gameState == STATE_GAME_OVER then
        ffe.drawRect(0, sh / 2 - 60, sw, 120, 0, 0, 0, 0.75)
        drawBorder(sw / 4, sh / 2 - 60, sw / 2, 120, 0.8, 0.15, 0.1, 0.6, 2)
        local goText = "GAME OVER"
        ffe.drawText(goText, centerTextX(goText, 4), sh / 2 - 40, 4, 1, 0.2, 0.2, 1)
        local retryText = "Press ENTER to retry | ESC for menu"
        ffe.drawText(retryText, centerTextX(retryText, 2), sh / 2 + 20, 2, 0.8, 0.8, 0.8, 1)
    end

    -- Title bar (during gameplay states, not on victory)
    if gameState ~= STATE_VICTORY then
        ffe.drawRect(0, 0, sw, 30, 0, 0, 0, 0.5)
        ffe.drawText("ECHOES OF THE ANCIENTS", 12, 6, 2, 0.9, 0.75, 0.4, 1)
    end
end

--------------------------------------------------------------------
-- shutdown() — called by engine before Lua state closes
--------------------------------------------------------------------
function shutdown()
    ffe.log("[Showcase] Shutdown")
    ffe.setCursorCaptured(false)  -- Ensure cursor is released on exit
    if Player then Player.cleanup() end
    -- Clean up vegetation BEFORE unloading terrain
    if grassHandle then
        ffe.removeVegetationPatch(grassHandle)
        grassHandle = nil
    end
    if treesActive then
        ffe.clearTrees()
        treesActive = false
    end
    if waterHandle then
        ffe.destroyWater(waterHandle)
        waterHandle = nil
    end
    ffe.unloadTerrain()
    ffe.disablePostProcessing()
    ffe.disableSSAO()
    ffe.stopMusic()
    ffe.disableFog()
    ffe.disableShadows()
    ffe.removePointLight(0)
    ffe.removePointLight(1)
    ffe.removePointLight(2)
    ffe.removePointLight(3)
    ffe.removePointLight(4)
    ffe.removePointLight(5)
    ffe.removePointLight(6)
    ffe.removePointLight(7)
    ffe.unloadSkybox()
end
