-- pong.lua -- Classic Pong for FFE
--
-- Two-player Pong: left paddle (W/S), right paddle (UP/DOWN).
-- Press SPACE to serve. First to 5 wins.
--
-- Exercises: input, collision, entity lifecycle, HUD, audio (music + SFX),
-- transforms, visual effects (trail, flash, color shift).
--
-- Coordinate system: centered origin, x: -640..640, y: -360..360.
-- Window: 1280x720.

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local HALF_W         = 640
local HALF_H         = 360
local PADDLE_W       = 16
local PADDLE_H       = 100
local PADDLE_SPEED   = 350.0
local PADDLE_X       = 580       -- distance from center
local BALL_SIZE      = 16
local BALL_SPEED     = 400.0
local BALL_SPEED_INC = 25.0      -- speed increase per rally hit
local MAX_BALL_SPEED = 800.0
local WIN_SCORE      = 5

-- Trail
local TRAIL_COUNT    = 6
local TRAIL_SIZE     = 12

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------
local leftPaddle     = nil
local rightPaddle    = nil
local ball           = nil
local whiteTex       = nil
local sfxPaddle      = nil
local sfxWall        = nil
local sfxScore       = nil
local musicHandle    = nil
local musicPlaying   = false

local scoreLeft      = 0
local scoreRight     = 0
local ballVx         = 0
local ballVy         = 0
local ballSpeed      = BALL_SPEED
local serving        = true      -- waiting for SPACE
local serveDir       = 1         -- 1 = serve right, -1 = serve left
local gameOver       = false
local gameTime       = 0
local gameState      = "title"  -- "title", "playing"

local transformBuf   = {}        -- reusable table for fillTransform

-- Trail pool
local trail          = {}
local trailIndex     = 1
local trailTimer     = 0

-- Paddle flash
local leftFlashTimer  = 0
local rightFlashTimer = 0
local leftBaseColor   = {0.4, 0.7, 1.0}
local rightBaseColor  = {1.0, 0.5, 0.4}

-- Goal flash entities
local goalFlashLeft  = nil
local goalFlashRight = nil
local goalFlashTimer = 0
local goalFlashSide  = nil

-- ---------------------------------------------------------------------------
-- Helper: update HUD
-- ---------------------------------------------------------------------------
local function updateHud()
    if gameOver then
        local winner = scoreLeft >= WIN_SCORE and "LEFT" or "RIGHT"
        ffe.setHudText(winner .. " WINS!  " .. scoreLeft .. " - " .. scoreRight ..
                       "  |  SPACE restart  |  M music  |  ESC quit")
    elseif serving then
        ffe.setHudText(scoreLeft .. " - " .. scoreRight ..
                       "  |  SPACE serve  |  W/S  Up/Down  |  M music  |  ESC quit")
    else
        ffe.setHudText(scoreLeft .. " - " .. scoreRight ..
                       "  |  W/S  Up/Down  |  M music  |  ESC quit")
    end
end

-- ---------------------------------------------------------------------------
-- Helper: create a white rectangle entity at (x,y) with given size and color
-- ---------------------------------------------------------------------------
local function createRect(x, y, w, h, r, g, b, layer)
    local id = ffe.createEntity()
    if not id then return nil end
    ffe.addTransform(id, x, y, 0, 1, 1)
    ffe.addSprite(id, whiteTex or 1, w, h, r, g, b, 1, layer or 1)
    ffe.addPreviousTransform(id)
    return id
end

-- ---------------------------------------------------------------------------
-- Trail system
-- ---------------------------------------------------------------------------
local function initTrail()
    for i = 1, TRAIL_COUNT do
        local id = createRect(-9999, -9999, TRAIL_SIZE, TRAIL_SIZE, 1, 1, 1, 2)
        trail[i] = { id = id, x = -9999, y = -9999 }
    end
end

local function updateTrail(bx, by, dt)
    trailTimer = trailTimer + dt
    if trailTimer >= 0.016 then
        trailTimer = 0
        trail[trailIndex].x = bx
        trail[trailIndex].y = by
        trailIndex = (trailIndex % TRAIL_COUNT) + 1
    end
    local speedRatio = (ballSpeed - BALL_SPEED) / (MAX_BALL_SPEED - BALL_SPEED)
    speedRatio = math.max(0, math.min(1, speedRatio))
    for i = 1, TRAIL_COUNT do
        local t = trail[i]
        if t.id then
            ffe.setTransform(t.id, t.x, t.y, 0, 1, 1)
            local age = ((trailIndex - i - 1) % TRAIL_COUNT) / TRAIL_COUNT
            local alpha = 0.1 + age * 0.25
            ffe.setSpriteColor(t.id, 1, 1 - speedRatio * 0.5, 1 - speedRatio * 0.7, alpha)
        end
    end
end

local function hideTrail()
    for i = 1, TRAIL_COUNT do
        if trail[i] and trail[i].id then
            ffe.setTransform(trail[i].id, -9999, -9999, 0, 1, 1)
            trail[i].x = -9999
            trail[i].y = -9999
        end
    end
end

-- ---------------------------------------------------------------------------
-- Helper: reset ball to center, waiting for serve
-- ---------------------------------------------------------------------------
local function resetBall()
    if ball then
        ffe.setTransform(ball, 0, 0, 0, 1, 1)
        ffe.setSpriteColor(ball, 1, 1, 1, 1)
    end
    ballVx   = 0
    ballVy   = 0
    ballSpeed = BALL_SPEED
    serving  = true
    hideTrail()
    updateHud()
end

-- ---------------------------------------------------------------------------
-- Helper: serve the ball
-- ---------------------------------------------------------------------------
local function serveBall()
    local angle = (math.random() * 60 - 30) * math.pi / 180
    ballVx = math.cos(angle) * ballSpeed * serveDir
    ballVy = math.sin(angle) * ballSpeed
    serving = false
    updateHud()
end

-- ---------------------------------------------------------------------------
-- Helper: score a point
-- ---------------------------------------------------------------------------
local function scorePoint(side)
    if side == "left" then
        scoreLeft = scoreLeft + 1
        goalFlashSide = "right"
    else
        scoreRight = scoreRight + 1
        goalFlashSide = "left"
    end
    goalFlashTimer = 0.3

    if sfxScore then ffe.playSound(sfxScore, 0.6) end
    ffe.cameraShake(1.0, 0.08)

    if scoreLeft >= WIN_SCORE or scoreRight >= WIN_SCORE then
        gameOver = true
        local winner = scoreLeft >= WIN_SCORE and "LEFT" or "RIGHT"
        ffe.log("Game over! " .. winner .. " wins " .. scoreLeft .. "-" .. scoreRight)
    else
        serveDir = side == "left" and 1 or -1
    end

    resetBall()
end

-- ---------------------------------------------------------------------------
-- Helper: restart the game
-- ---------------------------------------------------------------------------
local function restartGame()
    scoreLeft  = 0
    scoreRight = 0
    gameOver   = false
    serveDir   = 1
    resetBall()
end

-- ---------------------------------------------------------------------------
-- Scene init
-- ---------------------------------------------------------------------------

-- Load assets
whiteTex    = ffe.loadTexture("textures/white.png")
sfxPaddle   = ffe.loadSound("audio/sfx_pong_paddle.wav")
sfxWall     = ffe.loadSound("audio/sfx_pong_wall.wav")
sfxScore    = ffe.loadSound("audio/sfx_pong_score.wav")
musicHandle = ffe.loadMusic("audio/music_pixelcrown.ogg")

-- Start background music
if musicHandle then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.2)
    musicPlaying = true
    ffe.log("Music started (vol=0.2, M to toggle)")
end

-- Create paddles
leftPaddle  = createRect(-PADDLE_X, 0, PADDLE_W, PADDLE_H, 0.4, 0.7, 1.0, 2)
rightPaddle = createRect( PADDLE_X, 0, PADDLE_W, PADDLE_H, 1.0, 0.5, 0.4, 2)

-- Create ball
ball = createRect(0, 0, BALL_SIZE, BALL_SIZE, 1, 1, 1, 3)

-- Create center line decorations (dashed line effect)
for i = -HALF_H + 20, HALF_H - 20, 40 do
    createRect(0, i, 4, 20, 0.3, 0.3, 0.3, 0)
end

-- Create top and bottom wall indicators
createRect(0, HALF_H - 2, HALF_W * 2, 4, 0.2, 0.2, 0.2, 0)
createRect(0, -(HALF_H - 2), HALF_W * 2, 4, 0.2, 0.2, 0.2, 0)

-- Goal flash panels (tall rectangles at the edges, initially invisible)
goalFlashLeft  = createRect(-HALF_W + 20, 0, 40, HALF_H * 2, 0.4, 0.7, 1.0, 0)
goalFlashRight = createRect( HALF_W - 20, 0, 40, HALF_H * 2, 1.0, 0.5, 0.4, 0)
if goalFlashLeft  then ffe.setSpriteColor(goalFlashLeft,  0.4, 0.7, 1.0, 0) end
if goalFlashRight then ffe.setSpriteColor(goalFlashRight, 1.0, 0.5, 0.4, 0) end

initTrail()

ffe.setBackgroundColor(0.05, 0.05, 0.1)
ffe.log("Pong ready! Press SPACE to serve.")
updateHud()

math.randomseed(12345)

-- ---------------------------------------------------------------------------
-- update(entityId, dt)
-- ---------------------------------------------------------------------------
function update(entityId, dt)
    gameTime = gameTime + dt

    -- ESC to quit
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
        return
    end

    -- M to toggle music
    if musicHandle and ffe.isKeyPressed(ffe.KEY_M) then
        if musicPlaying then
            ffe.stopMusic()
            musicPlaying = false
        else
            ffe.playMusic(musicHandle, true)
            musicPlaying = true
        end
    end

    -- Title screen
    if gameState == "title" then
        local sw = ffe.getScreenWidth()
        ffe.drawText("PONG", sw / 2 - 80, 160, 5, 0.4, 0.7, 1.0, 1)
        ffe.drawText("A FastFreeEngine Demo", sw / 2 - 168, 240, 2, 0.6, 0.6, 0.7, 0.8)
        ffe.drawText("Player 1: W / S", sw / 2 - 120, 320, 2, 0.4, 0.7, 1.0, 0.9)
        ffe.drawText("Player 2: UP / DOWN", sw / 2 - 152, 350, 2, 1.0, 0.5, 0.4, 0.9)
        local alpha = 0.5 + 0.5 * math.sin(gameTime * 3)
        ffe.drawText("PRESS SPACE TO START", sw / 2 - 160, 440, 2, 1, 1, 1, alpha)
        ffe.drawText("M music | ESC quit", sw / 2 - 144, 520, 2, 0.4, 0.4, 0.5, 0.6)
        if ffe.isKeyPressed(ffe.KEY_SPACE) then
            gameState = "playing"
        end
        return
    end

    -- SPACE to serve or restart
    if ffe.isKeyPressed(ffe.KEY_SPACE) then
        if gameOver then
            restartGame()
        elseif serving then
            serveBall()
        end
    end

    -- Goal flash decay
    if goalFlashTimer > 0 then
        goalFlashTimer = goalFlashTimer - dt
        local alpha = math.max(0, goalFlashTimer / 0.3) * 0.4
        if goalFlashSide == "left" and goalFlashLeft then
            ffe.setSpriteColor(goalFlashLeft, 0.4, 0.7, 1.0, alpha)
        elseif goalFlashSide == "right" and goalFlashRight then
            ffe.setSpriteColor(goalFlashRight, 1.0, 0.5, 0.4, alpha)
        end
    end

    -- Paddle flash decay
    if leftFlashTimer > 0 then
        leftFlashTimer = leftFlashTimer - dt
        if leftFlashTimer <= 0 then
            ffe.setSpriteColor(leftPaddle, leftBaseColor[1], leftBaseColor[2], leftBaseColor[3], 1)
        else
            local f = leftFlashTimer / 0.1
            ffe.setSpriteColor(leftPaddle, 1, 1, 1, 0.7 + f * 0.3)
        end
    end
    if rightFlashTimer > 0 then
        rightFlashTimer = rightFlashTimer - dt
        if rightFlashTimer <= 0 then
            ffe.setSpriteColor(rightPaddle, rightBaseColor[1], rightBaseColor[2], rightBaseColor[3], 1)
        else
            local f = rightFlashTimer / 0.1
            ffe.setSpriteColor(rightPaddle, 1, 1, 1, 0.7 + f * 0.3)
        end
    end

    -- Ball pulsing while serving
    if serving and not gameOver and ball then
        local pulse = 0.9 + 0.1 * math.sin(gameTime * 6)
        ffe.setTransform(ball, 0, 0, 0, pulse, pulse)
    end

    -- Move left paddle (W/S)
    if ffe.fillTransform(leftPaddle, transformBuf) then
        local py = transformBuf.y
        if ffe.isKeyHeld(ffe.KEY_W) then py = py + PADDLE_SPEED * dt end
        if ffe.isKeyHeld(ffe.KEY_S) then py = py - PADDLE_SPEED * dt end
        local halfPad = PADDLE_H / 2
        py = math.max(-(HALF_H - halfPad - 4), math.min(HALF_H - halfPad - 4, py))
        ffe.setTransform(leftPaddle, transformBuf.x, py, 0, 1, 1)
    end

    -- Move right paddle (UP/DOWN)
    if ffe.fillTransform(rightPaddle, transformBuf) then
        local py = transformBuf.y
        if ffe.isKeyHeld(ffe.KEY_UP)   then py = py + PADDLE_SPEED * dt end
        if ffe.isKeyHeld(ffe.KEY_DOWN) then py = py - PADDLE_SPEED * dt end
        local halfPad = PADDLE_H / 2
        py = math.max(-(HALF_H - halfPad - 4), math.min(HALF_H - halfPad - 4, py))
        ffe.setTransform(rightPaddle, transformBuf.x, py, 0, 1, 1)
    end

    -- Move ball
    if not serving and not gameOver and ball then
        if not ffe.fillTransform(ball, transformBuf) then return end

        local bx = transformBuf.x + ballVx * dt
        local by = transformBuf.y + ballVy * dt

        -- Bounce off top/bottom walls
        local halfBall = BALL_SIZE / 2
        if by > HALF_H - halfBall - 4 then
            by = HALF_H - halfBall - 4
            ballVy = -ballVy
            if sfxWall then ffe.playSound(sfxWall, 0.3) end
        elseif by < -(HALF_H - halfBall - 4) then
            by = -(HALF_H - halfBall - 4)
            ballVy = -ballVy
            if sfxWall then ffe.playSound(sfxWall, 0.3) end
        end

        -- Check paddle collisions
        local halfPadW = PADDLE_W / 2
        local halfPadH = PADDLE_H / 2

        -- Left paddle check
        if ballVx < 0 then
            if ffe.fillTransform(leftPaddle, transformBuf) then
                local px, py = transformBuf.x, transformBuf.y
                if bx - halfBall < px + halfPadW and
                   bx + halfBall > px - halfPadW and
                   by + halfBall > py - halfPadH and
                   by - halfBall < py + halfPadH then
                    bx = px + halfPadW + halfBall
                    local hitPos = (by - py) / halfPadH
                    local angle = hitPos * 60 * math.pi / 180
                    ballSpeed = math.min(MAX_BALL_SPEED, ballSpeed + BALL_SPEED_INC)
                    ballVx = math.cos(angle) * ballSpeed
                    ballVy = math.sin(angle) * ballSpeed
                    if sfxPaddle then ffe.playSound(sfxPaddle, 0.5) end
                    leftFlashTimer = 0.1
                    ffe.setSpriteColor(leftPaddle, 1, 1, 1, 1)
                end
            end
        end

        -- Right paddle check
        if ballVx > 0 then
            if ffe.fillTransform(rightPaddle, transformBuf) then
                local px, py = transformBuf.x, transformBuf.y
                if bx + halfBall > px - halfPadW and
                   bx - halfBall < px + halfPadW and
                   by + halfBall > py - halfPadH and
                   by - halfBall < py + halfPadH then
                    bx = px - halfPadW - halfBall
                    local hitPos = (by - py) / halfPadH
                    local angle = hitPos * 60 * math.pi / 180
                    ballSpeed = math.min(MAX_BALL_SPEED, ballSpeed + BALL_SPEED_INC)
                    ballVx = -math.cos(angle) * ballSpeed
                    ballVy = math.sin(angle) * ballSpeed
                    if sfxPaddle then ffe.playSound(sfxPaddle, 0.5) end
                    rightFlashTimer = 0.1
                    ffe.setSpriteColor(rightPaddle, 1, 1, 1, 1)
                end
            end
        end

        -- Score: ball past left or right edge
        if bx < -HALF_W then
            scorePoint("right")
            return
        elseif bx > HALF_W then
            scorePoint("left")
            return
        end

        ffe.setTransform(ball, bx, by, 0, 1, 1)

        -- Ball color shift with speed
        local speedRatio = (ballSpeed - BALL_SPEED) / (MAX_BALL_SPEED - BALL_SPEED)
        speedRatio = math.max(0, math.min(1, speedRatio))
        ffe.setSpriteColor(ball, 1, 1 - speedRatio * 0.5, 1 - speedRatio * 0.7, 1)

        -- Update trail
        updateTrail(bx, by, dt)
    end

    -- HUD: score display
    ffe.drawRect(530, 20, 220, 50, 0, 0, 0, 0.5)
    ffe.drawText(tostring(scoreLeft), 560, 25, 5, 0.4, 0.7, 1.0, 0.9)
    ffe.drawText("-", 630, 25, 5, 0.5, 0.5, 0.5, 0.7)
    ffe.drawText(tostring(scoreRight), 680, 25, 5, 1.0, 0.5, 0.4, 0.9)

    if serving and not gameOver then
        ffe.drawRect(450, 390, 380, 36, 0, 0, 0, 0.5)
        ffe.drawText("SPACE TO SERVE", 480, 394, 3, 0.7, 0.7, 0.8, 0.6 + 0.4 * math.sin(gameTime * 4))
    end
    if gameOver then
        local winner = scoreLeft >= WIN_SCORE and "LEFT" or "RIGHT"
        ffe.drawRect(400, 310, 480, 130, 0, 0, 0, 0.6)
        ffe.drawText(winner .. " WINS!", 440, 316, 5, 1, 1, 0.3, 1)
        ffe.drawText("SPACE TO RESTART", 460, 414, 3, 0.7, 0.7, 0.8, 0.6 + 0.4 * math.sin(gameTime * 4))
    end
    ffe.drawRect(300, 682, 680, 28, 0, 0, 0, 0.4)
    ffe.drawText("W/S | UP/DOWN | M music | ESC quit", 330, 686, 2, 0.4, 0.4, 0.5, 0.6)
end

-- ---------------------------------------------------------------------------
-- shutdown()
-- ---------------------------------------------------------------------------
function shutdown()
    ffe.log("Pong final score: " .. scoreLeft .. " - " .. scoreRight)

    -- Stop music
    if musicHandle then
        if ffe.isMusicPlaying() then ffe.stopMusic() end
        ffe.unloadSound(musicHandle)
        musicHandle = nil
    end

    -- Unload SFX
    if sfxPaddle then ffe.unloadSound(sfxPaddle); sfxPaddle = nil end
    if sfxWall   then ffe.unloadSound(sfxWall);   sfxWall   = nil end
    if sfxScore  then ffe.unloadSound(sfxScore);  sfxScore  = nil end

    -- Unload texture
    if whiteTex then
        ffe.unloadTexture(whiteTex)
        whiteTex = nil
    end

    ffe.log("Pong shutdown complete")
end
