-- pong.lua -- Classic Pong for FFE
--
-- Two-player Pong: left paddle (W/S), right paddle (UP/DOWN).
-- Press SPACE to serve. First to 5 wins.
--
-- Exercises: input, collision, entity lifecycle, HUD, audio (music + SFX), transforms.
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

local transformBuf   = {}        -- reusable table for fillTransform

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
-- Helper: reset ball to center, waiting for serve
-- ---------------------------------------------------------------------------
local function resetBall()
    if ball then
        ffe.setTransform(ball, 0, 0, 0, 1, 1)
    end
    ballVx   = 0
    ballVy   = 0
    ballSpeed = BALL_SPEED
    serving  = true
    updateHud()
end

-- ---------------------------------------------------------------------------
-- Helper: serve the ball
-- ---------------------------------------------------------------------------
local function serveBall()
    -- Angle between -30 and +30 degrees
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
    else
        scoreRight = scoreRight + 1
    end

    if sfxScore then ffe.playSound(sfxScore, 0.6) end

    if scoreLeft >= WIN_SCORE or scoreRight >= WIN_SCORE then
        gameOver = true
        local winner = scoreLeft >= WIN_SCORE and "LEFT" or "RIGHT"
        ffe.log("Game over! " .. winner .. " wins " .. scoreLeft .. "-" .. scoreRight)
    else
        serveDir = side == "left" and 1 or -1  -- loser serves
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
whiteTex    = ffe.loadTexture("white.png")
sfxPaddle   = ffe.loadSound("sfx_pong_paddle.wav")
sfxWall     = ffe.loadSound("sfx_pong_wall.wav")
sfxScore    = ffe.loadSound("sfx_pong_score.wav")
musicHandle = ffe.loadSound("music_pixelcrown.ogg")

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

ffe.log("Pong ready! Press SPACE to serve.")
updateHud()

math.randomseed(12345)

-- ---------------------------------------------------------------------------
-- update(entityId, dt)
-- ---------------------------------------------------------------------------
function update(entityId, dt)
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

    -- SPACE to serve or restart
    if ffe.isKeyPressed(ffe.KEY_SPACE) then
        if gameOver then
            restartGame()
        elseif serving then
            serveBall()
        end
    end

    -- Move left paddle (W/S)
    if ffe.fillTransform(leftPaddle, transformBuf) then
        local py = transformBuf.y
        if ffe.isKeyHeld(ffe.KEY_W) then py = py + PADDLE_SPEED * dt end
        if ffe.isKeyHeld(ffe.KEY_S) then py = py - PADDLE_SPEED * dt end
        -- Clamp to screen
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

        -- Check paddle collisions (manual AABB since paddles don't use collision system)
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
                    -- Hit! Reflect and adjust angle based on hit position
                    bx = px + halfPadW + halfBall
                    local hitPos = (by - py) / halfPadH  -- -1 to 1
                    local angle = hitPos * 60 * math.pi / 180  -- up to 60 degrees
                    ballSpeed = math.min(MAX_BALL_SPEED, ballSpeed + BALL_SPEED_INC)
                    ballVx = math.cos(angle) * ballSpeed
                    ballVy = math.sin(angle) * ballSpeed
                    if sfxPaddle then ffe.playSound(sfxPaddle, 0.5) end
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
    end
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
