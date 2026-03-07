-- lib/menus.lua -- Menu system for "Echoes of the Ancients"
--
-- Provides title screen and pause menu rendering + input handling.
-- Uses only ffe.drawText, ffe.drawRect, and input queries.
--
-- Globals set: Menus (table with public API)

Menus = {}

--------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------
-- Colors (gold palette)
local GOLD_R, GOLD_G, GOLD_B       = 0.95, 0.8, 0.3
local GOLD_DIM_R, GOLD_DIM_G, GOLD_DIM_B = 0.7, 0.6, 0.25
local WHITE_R, WHITE_G, WHITE_B     = 1.0, 1.0, 1.0
local GREY_R, GREY_G, GREY_B       = 0.5, 0.55, 0.6
local HIGHLIGHT_R, HIGHLIGHT_G, HIGHLIGHT_B = 1.0, 0.9, 0.4
local BG_R, BG_G, BG_B             = 0.04, 0.03, 0.06

-- Pause menu options
local PAUSE_OPTIONS = { "Resume", "Restart Level", "Quit to Menu" }

--------------------------------------------------------------------
-- Internal state
--------------------------------------------------------------------
local titleTimer = 0   -- accumulated time for animations on title
local pauseSelected = 1
local pauseInputCooldown = 0  -- prevent rapid scrolling

--------------------------------------------------------------------
-- Helper: center text horizontally
-- charWidth at scale 1 = 8px, so at scale S = 8*S
--------------------------------------------------------------------
local function centerX(text, scale)
    local sw = ffe.getScreenWidth()
    local charW = 8 * scale
    local textW = #text * charW
    return math.floor((sw - textW) / 2)
end

--------------------------------------------------------------------
-- Helper: draw a decorative horizontal line (gold)
--------------------------------------------------------------------
local function drawGoldLine(y, width, alpha)
    local sw = ffe.getScreenWidth()
    local x = math.floor((sw - width) / 2)
    ffe.drawRect(x, y, width, 2, GOLD_R, GOLD_G, GOLD_B, alpha or 0.6)
end

--------------------------------------------------------------------
-- Helper: draw a border frame (four thin rects)
--------------------------------------------------------------------
local function drawBorder(x, y, w, h, r, g, b, a, thickness)
    thickness = thickness or 2
    -- Top
    ffe.drawRect(x, y, w, thickness, r, g, b, a)
    -- Bottom
    ffe.drawRect(x, y + h - thickness, w, thickness, r, g, b, a)
    -- Left
    ffe.drawRect(x, y, thickness, h, r, g, b, a)
    -- Right
    ffe.drawRect(x + w - thickness, y, thickness, h, r, g, b, a)
end

--------------------------------------------------------------------
-- Menus.resetTitle()
-- Call when transitioning to the title screen to reset animations.
--------------------------------------------------------------------
function Menus.resetTitle()
    titleTimer = 0
end

--------------------------------------------------------------------
-- Menus.resetPause()
-- Call when entering the pause menu to reset selection.
--------------------------------------------------------------------
function Menus.resetPause()
    pauseSelected = 1
    pauseInputCooldown = 0
end

--------------------------------------------------------------------
-- Menus.drawTitle(dt)
-- Renders the full title screen. Call every frame while in MENU.
--------------------------------------------------------------------
function Menus.drawTitle(dt)
    titleTimer = titleTimer + dt

    local sw = ffe.getScreenWidth()
    local sh = ffe.getScreenHeight()

    -- Dark atmospheric background
    ffe.drawRect(0, 0, sw, sh, BG_R, BG_G, BG_B, 1.0)

    -- Subtle animated "fog" bands (dark rectangles with varying alpha)
    for i = 0, 3 do
        local fogY = sh * 0.3 + i * 60 + math.sin(titleTimer * 0.4 + i * 1.7) * 20
        local fogAlpha = 0.03 + 0.02 * math.sin(titleTimer * 0.3 + i * 2.1)
        ffe.drawRect(0, fogY, sw, 40, 0.15, 0.1, 0.25, fogAlpha)
    end

    -- Top decorative line
    local topLineY = sh * 0.18
    drawGoldLine(topLineY, 500, 0.4)

    -- Main title: "ECHOES OF THE ANCIENTS"
    local titleScale = 5
    local titleText = "ECHOES OF THE ANCIENTS"
    local titleX = centerX(titleText, titleScale)
    local titleY = math.floor(sh * 0.22)

    -- Gold glow pulse on title
    local titlePulse = 0.85 + 0.15 * math.sin(titleTimer * 1.8)
    ffe.drawText(titleText, titleX, titleY, titleScale,
        GOLD_R * titlePulse, GOLD_G * titlePulse, GOLD_B * titlePulse, 1.0)

    -- Bottom decorative line under title
    drawGoldLine(titleY + titleScale * 8 + 8, 500, 0.4)

    -- Subtitle: "A FastFreeEngine Showcase"
    local subScale = 3
    local subText = "A FastFreeEngine Showcase"
    local subX = centerX(subText, subScale)
    local subY = titleY + titleScale * 8 + 24
    ffe.drawText(subText, subX, subY, subScale,
        GOLD_DIM_R, GOLD_DIM_G, GOLD_DIM_B, 0.85)

    -- "Press ENTER to Start" — blinking via sine alpha pulse
    local promptScale = 3
    local promptText = "Press ENTER to Start"
    local promptX = centerX(promptText, promptScale)
    local promptY = math.floor(sh * 0.55)
    local promptAlpha = 0.4 + 0.6 * math.max(0, math.sin(titleTimer * 2.5))
    ffe.drawText(promptText, promptX, promptY, promptScale,
        WHITE_R, WHITE_G, WHITE_B, promptAlpha)

    -- "Press START on Gamepad" — shown if gamepad 0 is connected
    if ffe.isGamepadConnected(0) then
        local gpScale = 2
        local gpText = "Press START on Gamepad"
        local gpX = centerX(gpText, gpScale)
        local gpY = promptY + promptScale * 8 + 12
        ffe.drawText(gpText, gpX, gpY, gpScale,
            GREY_R, GREY_G, GREY_B, promptAlpha * 0.8)
    end

    -- Controls help section at bottom
    local helpY = math.floor(sh * 0.75)
    local helpPanelH = 120
    ffe.drawRect(0, helpY - 8, sw, helpPanelH, 0, 0, 0, 0.5)

    -- Decorative line above controls
    drawGoldLine(helpY - 8, sw * 0.6, 0.3)

    local helpScale = 2
    local helpAlpha = 0.7

    -- Keyboard controls
    local kbLine1 = "WASD - Move | Mouse - Look | E - Interact"
    local kbLine2 = "SPACE - Jump | LMB - Attack | ESC - Pause"
    ffe.drawText(kbLine1, centerX(kbLine1, helpScale), helpY + 8, helpScale,
        GREY_R, GREY_G, GREY_B, helpAlpha)
    ffe.drawText(kbLine2, centerX(kbLine2, helpScale), helpY + 32, helpScale,
        GREY_R, GREY_G, GREY_B, helpAlpha)

    -- Gamepad controls
    local gpLine = "Gamepad: Left Stick - Move | Right Stick - Look | A - Jump | X - Attack | Y - Interact"
    ffe.drawText(gpLine, centerX(gpLine, helpScale), helpY + 64, helpScale,
        GREY_R, GREY_G, GREY_B, helpAlpha * 0.7)

    -- Decorative line below controls
    drawGoldLine(helpY + helpPanelH - 8, sw * 0.6, 0.3)

    -- Bottom credit line
    local creditScale = 2
    local creditText = "Built with FastFreeEngine"
    local creditX = centerX(creditText, creditScale)
    local creditY = sh - 32
    ffe.drawText(creditText, creditX, creditY, creditScale, 0.3, 0.25, 0.4, 0.5)
end

--------------------------------------------------------------------
-- Menus.drawPause(dt, selectedOption)
-- Renders the pause menu overlay. selectedOption is 1-based index.
--------------------------------------------------------------------
function Menus.drawPause(dt, selectedOption)
    local sw = ffe.getScreenWidth()
    local sh = ffe.getScreenHeight()

    -- Semi-transparent dark overlay covering the whole screen
    ffe.drawRect(0, 0, sw, sh, 0, 0, 0, 0.65)

    -- Pause panel dimensions
    local panelW = 400
    local panelH = 260
    local panelX = math.floor((sw - panelW) / 2)
    local panelY = math.floor((sh - panelH) / 2)

    -- Panel background
    ffe.drawRect(panelX, panelY, panelW, panelH, 0.06, 0.04, 0.1, 0.9)

    -- Gold border around panel
    drawBorder(panelX, panelY, panelW, panelH, GOLD_R, GOLD_G, GOLD_B, 0.6, 2)

    -- "PAUSED" title
    local titleScale = 5
    local titleText = "PAUSED"
    local titleX = centerX(titleText, titleScale)
    local titleY = panelY + 20
    ffe.drawText(titleText, titleX, titleY, titleScale,
        GOLD_R, GOLD_G, GOLD_B, 1.0)

    -- Decorative line under title
    drawGoldLine(titleY + titleScale * 8 + 8, panelW - 40, 0.5)

    -- Menu options
    local optionScale = 3
    local optionStartY = titleY + titleScale * 8 + 30
    local optionSpacing = 44

    for i, optText in ipairs(PAUSE_OPTIONS) do
        local optX = centerX(optText, optionScale)
        local optY = optionStartY + (i - 1) * optionSpacing

        if i == selectedOption then
            -- Highlight background for selected option
            local hlW = #optText * 8 * optionScale + 32
            local hlX = math.floor((sw - hlW) / 2)
            ffe.drawRect(hlX, optY - 6, hlW, optionScale * 8 + 12,
                GOLD_R * 0.2, GOLD_G * 0.2, GOLD_B * 0.2, 0.5)

            -- Arrow indicators
            ffe.drawText("> ", hlX + 4, optY, optionScale,
                HIGHLIGHT_R, HIGHLIGHT_G, HIGHLIGHT_B, 1.0)

            -- Highlighted text (bright gold)
            ffe.drawText(optText, optX, optY, optionScale,
                HIGHLIGHT_R, HIGHLIGHT_G, HIGHLIGHT_B, 1.0)
        else
            -- Normal text (dimmer)
            ffe.drawText(optText, optX, optY, optionScale,
                GREY_R, GREY_G, GREY_B, 0.8)
        end
    end

    -- Navigation hint at bottom of panel
    local hintScale = 2
    local hintText = "UP/DOWN: Navigate | ENTER: Select"
    local hintX = centerX(hintText, hintScale)
    local hintY = panelY + panelH - 30
    ffe.drawText(hintText, hintX, hintY, hintScale, 0.4, 0.4, 0.5, 0.6)
end

--------------------------------------------------------------------
-- Menus.handlePauseInput(dt)
-- Processes input for pause menu navigation.
-- Returns: "resume", "restart", "quit", or nil (no action)
--------------------------------------------------------------------
function Menus.handlePauseInput(dt)
    -- Cooldown to prevent rapid scrolling
    if pauseInputCooldown > 0 then
        pauseInputCooldown = pauseInputCooldown - dt
    end

    local moved = false

    -- Keyboard navigation
    if pauseInputCooldown <= 0 then
        if ffe.isKeyPressed(ffe.KEY_UP) or ffe.isKeyPressed(ffe.KEY_W) then
            pauseSelected = pauseSelected - 1
            if pauseSelected < 1 then pauseSelected = #PAUSE_OPTIONS end
            moved = true
        elseif ffe.isKeyPressed(ffe.KEY_DOWN) or ffe.isKeyPressed(ffe.KEY_S) then
            pauseSelected = pauseSelected + 1
            if pauseSelected > #PAUSE_OPTIONS then pauseSelected = 1 end
            moved = true
        end

        -- Gamepad D-pad navigation
        if ffe.isGamepadConnected(0) then
            if ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_DPAD_UP) then
                pauseSelected = pauseSelected - 1
                if pauseSelected < 1 then pauseSelected = #PAUSE_OPTIONS end
                moved = true
            elseif ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_DPAD_DOWN) then
                pauseSelected = pauseSelected + 1
                if pauseSelected > #PAUSE_OPTIONS then pauseSelected = 1 end
                moved = true
            end
        end

        if moved then
            pauseInputCooldown = 0.15
        end
    end

    -- Selection (ENTER or gamepad A)
    local selected = false
    if ffe.isKeyPressed(ffe.KEY_ENTER) then
        selected = true
    end
    if ffe.isGamepadConnected(0) and ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_A) then
        selected = true
    end

    if selected then
        if pauseSelected == 1 then return "resume" end
        if pauseSelected == 2 then return "restart" end
        if pauseSelected == 3 then return "quit" end
    end

    -- ESC also resumes (quick unpause)
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        return "resume"
    end

    -- Gamepad START also resumes
    if ffe.isGamepadConnected(0) and ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_START) then
        return "resume"
    end

    return nil
end

--------------------------------------------------------------------
-- Menus.getPauseSelected()
-- Returns the currently selected pause option (1-based index).
--------------------------------------------------------------------
function Menus.getPauseSelected()
    return pauseSelected
end

ffe.log("[Menus] Module loaded")
