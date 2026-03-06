-- breakout.lua -- Classic Breakout for FFE
--
-- Paddle (A/D or LEFT/RIGHT), ball, destructible bricks.
-- Press SPACE to launch. Clear all bricks to win.
--
-- Demonstrates: mass entity destruction, entity creation, input, audio, HUD.
--
-- Coordinate system: centered origin, x: -640..640, y: -360..360.
-- Window: 1280x720.

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local HALF_W         = 640
local HALF_H         = 360

local PADDLE_W       = 120
local PADDLE_H       = 16
local PADDLE_Y       = -300
local PADDLE_SPEED   = 500.0

local BALL_SIZE      = 12
local BALL_SPEED     = 350.0

local BRICK_W        = 80
local BRICK_H        = 24
local BRICK_COLS     = 14
local BRICK_ROWS     = 6
local BRICK_GAP      = 4
local BRICK_TOP_Y    = 280

local LIVES_START    = 3

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------
local whiteTex       = nil
local sfxBrick       = nil
local sfxPaddle      = nil
local sfxWall        = nil
local sfxLose        = nil
local musicHandle    = nil
local musicPlaying   = false

local paddle         = nil
local ball           = nil
local bricks         = {}
local brickCount     = 0

local ballVx         = 0
local ballVy         = 0
local ballLaunched   = false
local lives          = LIVES_START
local score          = 0
local gameOver       = false
local gameWon        = false

local transformBuf   = {}

-- ---------------------------------------------------------------------------
-- Brick colors by row (top to bottom: red, orange, yellow, green, cyan, blue)
-- ---------------------------------------------------------------------------
local ROW_COLORS = {
    {1.0, 0.3, 0.3},  -- red
    {1.0, 0.6, 0.2},  -- orange
    {1.0, 0.9, 0.3},  -- yellow
    {0.3, 0.9, 0.3},  -- green
    {0.3, 0.8, 0.9},  -- cyan
    {0.4, 0.5, 1.0},  -- blue
}

-- Points per row (top rows worth more)
local ROW_POINTS = {6, 5, 4, 3, 2, 1}

-- ---------------------------------------------------------------------------
-- Helper: update HUD
-- ---------------------------------------------------------------------------
local function updateHud()
    if gameOver then
        if gameWon then
            ffe.setHudText("YOU WIN!  Score: " .. score ..
                           "  |  SPACE restart  |  ESC quit")
        else
            ffe.setHudText("GAME OVER  Score: " .. score ..
                           "  |  SPACE restart  |  ESC quit")
        end
    elseif not ballLaunched then
        ffe.setHudText("Score: " .. score .. "  Lives: " .. lives ..
                       "  |  SPACE to launch  |  A/D move  |  ESC quit")
    else
        ffe.setHudText("Score: " .. score .. "  Lives: " .. lives ..
                       "  |  A/D move  |  ESC quit")
    end
end

-- ---------------------------------------------------------------------------
-- Helper: create a colored rectangle
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
-- Helper: create all bricks
-- ---------------------------------------------------------------------------
local function createBricks()
    bricks = {}
    brickCount = 0
    local totalW = BRICK_COLS * (BRICK_W + BRICK_GAP) - BRICK_GAP
    local startX = -totalW / 2 + BRICK_W / 2

    for row = 1, BRICK_ROWS do
        local y = BRICK_TOP_Y - (row - 1) * (BRICK_H + BRICK_GAP)
        local c = ROW_COLORS[row] or {1, 1, 1}
        for col = 1, BRICK_COLS do
            local x = startX + (col - 1) * (BRICK_W + BRICK_GAP)
            local id = createRect(x, y, BRICK_W, BRICK_H, c[1], c[2], c[3], 1)
            if id then
                bricks[id] = {row = row, x = x, y = y}
                brickCount = brickCount + 1
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Helper: reset ball to paddle
-- ---------------------------------------------------------------------------
local function resetBall()
    ballLaunched = false
    ballVx = 0
    ballVy = 0
    -- Position ball above paddle
    if ball then
        ffe.setTransform(ball, 0, PADDLE_Y + PADDLE_H / 2 + BALL_SIZE / 2 + 2, 0, 1, 1)
    end
end

-- ---------------------------------------------------------------------------
-- Helper: launch ball
-- ---------------------------------------------------------------------------
local function launchBall()
    local angle = (math.random() * 60 - 30) * math.pi / 180
    ballVx = math.sin(angle) * BALL_SPEED
    ballVy = math.cos(angle) * BALL_SPEED  -- always go up
    ballLaunched = true
end

-- ---------------------------------------------------------------------------
-- Helper: lose a life
-- ---------------------------------------------------------------------------
local function loseLife()
    lives = lives - 1
    if sfxLose then ffe.playSound(sfxLose, 0.5) end

    if lives <= 0 then
        gameOver = true
        gameWon  = false
        ffe.log("Game over! Final score: " .. score)
    else
        resetBall()
    end
    updateHud()
end

-- ---------------------------------------------------------------------------
-- Helper: restart the game
-- ---------------------------------------------------------------------------
local function restartGame()
    -- Destroy existing bricks
    for id, _ in pairs(bricks) do
        ffe.destroyEntity(id)
    end

    score      = 0
    lives      = LIVES_START
    gameOver   = false
    gameWon    = false

    createBricks()
    resetBall()
    updateHud()
end

-- ---------------------------------------------------------------------------
-- Scene init
-- ---------------------------------------------------------------------------
whiteTex    = ffe.loadTexture("white.png")
sfxBrick    = ffe.loadSound("sfx_pong_paddle.wav")
sfxPaddle   = ffe.loadSound("sfx_pong_wall.wav")
sfxWall     = ffe.loadSound("sfx_pong_wall.wav")
sfxLose     = ffe.loadSound("sfx_pong_score.wav")
musicHandle = ffe.loadMusic("music_pixelcrown.ogg")

-- Start background music
if musicHandle then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.15)
    musicPlaying = true
end

-- Create paddle
paddle = createRect(0, PADDLE_Y, PADDLE_W, PADDLE_H, 0.9, 0.9, 0.9, 2)

-- Create ball
ball = createRect(0, PADDLE_Y + PADDLE_H / 2 + BALL_SIZE / 2 + 2,
                  BALL_SIZE, BALL_SIZE, 1, 1, 1, 3)

-- Create walls (visual only — collision handled in code)
createRect(0, HALF_H - 2, HALF_W * 2, 4, 0.3, 0.3, 0.3, 0)    -- top
createRect(-HALF_W + 2, 0, 4, HALF_H * 2, 0.3, 0.3, 0.3, 0)   -- left
createRect(HALF_W - 2, 0, 4, HALF_H * 2, 0.3, 0.3, 0.3, 0)    -- right

-- Create bricks
math.randomseed(42)
createBricks()

ffe.log("Breakout ready! Press SPACE to launch.")
updateHud()

-- ---------------------------------------------------------------------------
-- update(entityId, dt)
-- ---------------------------------------------------------------------------
function update(entityId, dt)
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
        return
    end

    -- Music toggle
    if musicHandle and ffe.isKeyPressed(ffe.KEY_M) then
        if musicPlaying then
            ffe.stopMusic()
            musicPlaying = false
        else
            ffe.playMusic(musicHandle, true)
            musicPlaying = true
        end
    end

    -- Restart
    if gameOver and ffe.isKeyPressed(ffe.KEY_SPACE) then
        restartGame()
        return
    end

    -- Launch ball
    if not gameOver and not ballLaunched and ffe.isKeyPressed(ffe.KEY_SPACE) then
        launchBall()
        updateHud()
    end

    -- Move paddle
    if not gameOver and paddle and ffe.fillTransform(paddle, transformBuf) then
        local px = transformBuf.x
        if ffe.isKeyHeld(ffe.KEY_A) or ffe.isKeyHeld(ffe.KEY_LEFT) then
            px = px - PADDLE_SPEED * dt
        end
        if ffe.isKeyHeld(ffe.KEY_D) or ffe.isKeyHeld(ffe.KEY_RIGHT) then
            px = px + PADDLE_SPEED * dt
        end
        -- Clamp
        local halfPad = PADDLE_W / 2
        px = math.max(-HALF_W + halfPad + 4, math.min(HALF_W - halfPad - 4, px))
        ffe.setTransform(paddle, px, PADDLE_Y, 0, 1, 1)

        -- Ball follows paddle before launch
        if not ballLaunched and not gameOver and ball then
            ffe.setTransform(ball, px, PADDLE_Y + PADDLE_H / 2 + BALL_SIZE / 2 + 2, 0, 1, 1)
        end
    end

    -- Ball physics
    if ballLaunched and not gameOver and ball then
        if not ffe.fillTransform(ball, transformBuf) then return end

        local bx = transformBuf.x + ballVx * dt
        local by = transformBuf.y + ballVy * dt
        local halfBall = BALL_SIZE / 2

        -- Top wall bounce
        if by > HALF_H - halfBall - 4 then
            by = HALF_H - halfBall - 4
            ballVy = -ballVy
            if sfxWall then ffe.playSound(sfxWall, 0.2) end
        end

        -- Side wall bounces
        if bx < -HALF_W + halfBall + 4 then
            bx = -HALF_W + halfBall + 4
            ballVx = -ballVx
            if sfxWall then ffe.playSound(sfxWall, 0.2) end
        elseif bx > HALF_W - halfBall - 4 then
            bx = HALF_W - halfBall - 4
            ballVx = -ballVx
            if sfxWall then ffe.playSound(sfxWall, 0.2) end
        end

        -- Bottom — lose a life
        if by < -HALF_H then
            loseLife()
            return
        end

        -- Paddle collision
        if ballVy < 0 and paddle then
            if ffe.fillTransform(paddle, transformBuf) then
                local px = transformBuf.x
                local halfPW = PADDLE_W / 2
                local halfPH = PADDLE_H / 2
                if bx + halfBall > px - halfPW and
                   bx - halfBall < px + halfPW and
                   by - halfBall < PADDLE_Y + halfPH and
                   by + halfBall > PADDLE_Y - halfPH then
                    by = PADDLE_Y + halfPH + halfBall
                    -- Angle based on hit position
                    local hitPos = (bx - px) / halfPW  -- -1 to 1
                    local angle = hitPos * 60 * math.pi / 180
                    ballVx = math.sin(angle) * BALL_SPEED
                    ballVy = math.abs(math.cos(angle) * BALL_SPEED)
                    if sfxPaddle then ffe.playSound(sfxPaddle, 0.4) end
                end
            end
        end

        -- Brick collisions
        for id, brick in pairs(bricks) do
            local halfBW = BRICK_W / 2
            local halfBH = BRICK_H / 2
            if bx + halfBall > brick.x - halfBW and
               bx - halfBall < brick.x + halfBW and
               by + halfBall > brick.y - halfBH and
               by - halfBall < brick.y + halfBH then
                -- Destroy brick
                ffe.destroyEntity(id)
                bricks[id] = nil
                brickCount = brickCount - 1
                score = score + (ROW_POINTS[brick.row] or 1)

                -- Determine bounce direction (side vs top/bottom)
                local overlapX = math.min(bx + halfBall - (brick.x - halfBW),
                                          (brick.x + halfBW) - (bx - halfBall))
                local overlapY = math.min(by + halfBall - (brick.y - halfBH),
                                          (brick.y + halfBH) - (by - halfBall))
                if overlapX < overlapY then
                    ballVx = -ballVx
                else
                    ballVy = -ballVy
                end

                if sfxBrick then ffe.playSound(sfxBrick, 0.3) end
                updateHud()

                -- Check win
                if brickCount <= 0 then
                    gameOver = true
                    gameWon  = true
                    ffe.log("You win! Score: " .. score)
                    updateHud()
                end

                break  -- one brick per frame to avoid double-bounce
            end
        end

        ffe.setTransform(ball, bx, by, 0, 1, 1)
    end
end

-- ---------------------------------------------------------------------------
-- shutdown()
-- ---------------------------------------------------------------------------
function shutdown()
    ffe.log("Breakout final score: " .. score)

    if musicHandle then
        if ffe.isMusicPlaying() then ffe.stopMusic() end
        ffe.unloadSound(musicHandle)
        musicHandle = nil
    end

    if sfxBrick  then ffe.unloadSound(sfxBrick);  sfxBrick  = nil end
    if sfxPaddle then ffe.unloadSound(sfxPaddle); sfxPaddle = nil end
    if sfxWall   then ffe.unloadSound(sfxWall);   sfxWall   = nil end
    if sfxLose   then ffe.unloadSound(sfxLose);   sfxLose   = nil end

    if whiteTex then
        ffe.unloadTexture(whiteTex)
        whiteTex = nil
    end

    ffe.log("Breakout shutdown complete")
end
