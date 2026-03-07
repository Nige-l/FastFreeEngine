-- lib/hud.lua -- HUD module for "Echoes of the Ancients"
--
-- Draws on-screen UI elements:
--   Health bar (red background, green fill)
--   Artifact count
--   Level name
--   Interaction prompt when near interactive objects
--
-- Globals set: HUD (table with public API)

HUD = {}

--------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------
local BAR_X       = 16
local BAR_Y       = 50
local BAR_WIDTH   = 200
local BAR_HEIGHT  = 20
local BAR_PADDING = 2

local ARTIFACT_X  = 16
local ARTIFACT_Y  = 80

local LEVEL_X     = 16
local LEVEL_Y     = 108

local PROMPT_OFFSET_Y = 100  -- pixels above bottom of screen

--------------------------------------------------------------------
-- Interaction prompt state
--------------------------------------------------------------------
local promptText    = nil
local promptTimer   = 0

--------------------------------------------------------------------
-- HUD.draw(playerHealth, maxHealth, artifactCount, totalArtifacts, levelName)
-- Call once per frame from the main update loop.
--------------------------------------------------------------------
function HUD.draw(playerHealth, maxHealth, artifactCount, totalArtifacts, levelName)
    local sw = ffe.getScreenWidth()
    local sh = ffe.getScreenHeight()

    playerHealth  = playerHealth or 0
    maxHealth     = maxHealth or 100
    artifactCount = artifactCount or 0
    totalArtifacts = totalArtifacts or 0
    levelName     = levelName or ""

    -- Health bar background panel
    ffe.drawRect(BAR_X - BAR_PADDING, BAR_Y - BAR_PADDING,
                 BAR_WIDTH + BAR_PADDING * 2, BAR_HEIGHT + BAR_PADDING * 2,
                 0, 0, 0, 0.6)

    -- Health bar: red background (damage)
    ffe.drawRect(BAR_X, BAR_Y, BAR_WIDTH, BAR_HEIGHT, 0.6, 0.1, 0.1, 1.0)

    -- Health bar: green fill (remaining health)
    local fillFraction = 0
    if maxHealth > 0 then
        fillFraction = playerHealth / maxHealth
    end
    if fillFraction < 0 then fillFraction = 0 end
    if fillFraction > 1 then fillFraction = 1 end

    -- Color transitions: green (>50%) -> yellow (25-50%) -> red (<25%)
    local barR, barG, barB = 0.1, 0.85, 0.2
    if fillFraction < 0.25 then
        barR, barG, barB = 0.9, 0.15, 0.1
    elseif fillFraction < 0.5 then
        barR, barG, barB = 0.9, 0.7, 0.1
    end

    local fillWidth = BAR_WIDTH * fillFraction
    if fillWidth > 0 then
        ffe.drawRect(BAR_X, BAR_Y, fillWidth, BAR_HEIGHT, barR, barG, barB, 1.0)
    end

    -- Health text
    ffe.drawText("HP: " .. tostring(math.floor(playerHealth)) .. " / " .. tostring(maxHealth),
                 BAR_X + 4, BAR_Y + 3, 2, 1, 1, 1, 1)

    -- Artifact count
    ffe.drawRect(ARTIFACT_X - BAR_PADDING, ARTIFACT_Y - BAR_PADDING,
                 180, 24, 0, 0, 0, 0.5)
    ffe.drawText("ARTIFACTS: " .. tostring(artifactCount) .. " / " .. tostring(totalArtifacts),
                 ARTIFACT_X, ARTIFACT_Y, 2, 1.0, 0.85, 0.3, 1.0)

    -- Level name
    ffe.drawText(levelName, LEVEL_X, LEVEL_Y, 2, 0.7, 0.8, 0.9, 0.9)

    -- Interaction prompt (bottom center, fades after timeout)
    if promptText and promptTimer > 0 then
        local alpha = math.min(1.0, promptTimer * 2)  -- fade out
        local textLen = #promptText * 16  -- approximate width at scale 2
        local px = (sw - textLen) / 2
        local py = sh - PROMPT_OFFSET_Y

        ffe.drawRect(px - 8, py - 4, textLen + 16, 28, 0, 0, 0, 0.6 * alpha)
        ffe.drawText(promptText, px, py, 2, 1, 1, 0.7, alpha)
    end

    -- Controls hint (bottom bar)
    ffe.drawRect(0, sh - 28, sw, 28, 0, 0, 0, 0.4)
    ffe.drawText("WASD: move | SPACE: jump | MOUSE: look | LMB: attack | P: pause | ESC: quit",
                 12, sh - 22, 2, 0.4, 0.5, 0.6, 0.8)
end

--------------------------------------------------------------------
-- HUD.showPrompt(text, duration)
-- Shows a temporary interaction prompt at the bottom of the screen.
--------------------------------------------------------------------
function HUD.showPrompt(text, duration)
    promptText  = text
    promptTimer = duration or 2.0

    -- Set up a timer to decay the prompt
    ffe.after(duration or 2.0, function()
        if promptText == text then
            promptText  = nil
            promptTimer = 0
        end
    end)
end

--------------------------------------------------------------------
-- HUD.clearPrompt()
--------------------------------------------------------------------
function HUD.clearPrompt()
    promptText  = nil
    promptTimer = 0
end

ffe.log("[HUD] Module loaded")
