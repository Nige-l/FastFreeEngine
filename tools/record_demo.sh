#!/usr/bin/env bash
# tools/record_demo.sh — Record a short gameplay GIF of an FFE demo
#
# Captures individual frames using ImageMagick import, then stitches into GIF.
# Lighter weight than ffmpeg x11grab — avoids resource contention with the demo.
#
# Usage:
#   ./tools/record_demo.sh <demo_binary> <output_gif> [duration_seconds] [lua_script] [input_script]
#
# Example:
#   ./tools/record_demo.sh build/legacy-debug/examples/3d_demo/ffe_3d_demo docs/recordings/3d_demo.gif 8
#   ./tools/record_demo.sh build/legacy-debug/examples/showcase/ffe_showcase docs/recordings/level1.gif 10 level1 tools/inputs/showcase_explore.sh
#
# Per-level input scripts (use these to avoid camera clipping in indoor/outdoor levels):
#   tools/inputs/showcase_level1.sh  -- The Courtyard (open outdoor, 4s forward safe)
#   tools/inputs/showcase_level2.sh  -- The Temple (indoor, 1.5s forward max to avoid walls)
#   tools/inputs/showcase_level3.sh  -- The Summit (open elevated, 3s forward safe)
#
# Example with per-level script:
#   ./tools/record_demo.sh build/legacy-debug/examples/showcase/ffe_showcase docs/recordings/level2.gif 10 level2 tools/inputs/showcase_level2.sh

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <demo_binary> <output_gif> [duration_seconds] [lua_script] [input_script]"
    exit 1
fi

DEMO_BINARY="$1"
OUTPUT_GIF="$2"
DURATION="${3:-8}"
LUA_SCRIPT="${4:-}"
INPUT_SCRIPT="${5:-}"
FPS=5  # frames per second (keep low to avoid resource pressure)

if [ ! -x "$DEMO_BINARY" ]; then
    echo "ERROR: Demo binary not found or not executable: $DEMO_BINARY"
    exit 1
fi

# Kill stale FFE processes
killall -9 ffe_showcase ffe_3d_demo ffe_lua_demo ffe_pong ffe_breakout ffe_net_demo ffe_runtime 2>/dev/null || true
sleep 1

export DISPLAY="${DISPLAY:-:0}"
OUTPUT_DIR="$(dirname "$OUTPUT_GIF")"
mkdir -p "$OUTPUT_DIR"

FRAME_DIR="$(mktemp -d /tmp/ffe_frames_XXXXXX)"
DEMO_PID=""
INPUT_PID=""

cleanup() {
    [ -n "${INPUT_PID:-}" ] && kill "$INPUT_PID" 2>/dev/null || true
    [ -n "${DEMO_PID:-}" ] && kill "$DEMO_PID" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$FRAME_DIR" 2>/dev/null || true
}
trap cleanup EXIT

# Launch demo
echo "[record] Launching: $DEMO_BINARY ${LUA_SCRIPT:+$LUA_SCRIPT}"
if [ -n "$LUA_SCRIPT" ]; then
    "$DEMO_BINARY" "$LUA_SCRIPT" &>/dev/null &
else
    "$DEMO_BINARY" &>/dev/null &
fi
DEMO_PID=$!

# Wait for window to appear
echo "[record] Waiting for window..."
sleep 3

if ! kill -0 "$DEMO_PID" 2>/dev/null; then
    echo "ERROR: Demo exited before recording started"
    exit 2
fi

# Find and focus the demo window
WINDOW_ID=""
if command -v xdotool &>/dev/null; then
    WINDOW_ID=$(xdotool search --onlyvisible --pid "$DEMO_PID" 2>/dev/null | head -1)
fi

if [ -n "$WINDOW_ID" ]; then
    echo "[record] Found window: $WINDOW_ID"
    xdotool windowfocus "$WINDOW_ID" 2>/dev/null || true
    xdotool windowactivate "$WINDOW_ID" 2>/dev/null || true
    sleep 0.5
fi

# Start input script if provided
if [ -n "$INPUT_SCRIPT" ] && [ -x "$INPUT_SCRIPT" ]; then
    echo "[record] Running input script: $INPUT_SCRIPT"
    "$INPUT_SCRIPT" "${WINDOW_ID:-}" &
    INPUT_PID=$!
fi

# Capture frames
TOTAL_FRAMES=$((DURATION * FPS))
INTERVAL=$(python3 -c "print(1.0/$FPS)")
echo "[record] Capturing $TOTAL_FRAMES frames at ${FPS}fps over ${DURATION}s..."

for i in $(seq -w 1 "$TOTAL_FRAMES"); do
    if ! kill -0 "$DEMO_PID" 2>/dev/null; then
        echo "[record] Demo exited at frame $i"
        break
    fi
    FRAME_FILE="$FRAME_DIR/frame_${i}.png"
    if [ -n "$WINDOW_ID" ]; then
        import -window "$WINDOW_ID" -resize 640x360 "$FRAME_FILE" 2>/dev/null || true
    else
        import -window root -crop 1280x720+0+0 -resize 640x360 "$FRAME_FILE" 2>/dev/null || true
    fi
    sleep "$INTERVAL"
done

# Kill demo
kill "$DEMO_PID" 2>/dev/null || true
wait "$DEMO_PID" 2>/dev/null || true
DEMO_PID=""

# Count captured frames
FRAME_COUNT=$(ls "$FRAME_DIR"/frame_*.png 2>/dev/null | wc -l)
echo "[record] Captured $FRAME_COUNT frames"

if [ "$FRAME_COUNT" -lt 2 ]; then
    echo "ERROR: Too few frames captured"
    exit 3
fi

# Stitch frames into GIF
DELAY=$((100 / FPS))  # centiseconds between frames
echo "[record] Stitching GIF (delay=${DELAY}cs)..."
convert -delay "$DELAY" -loop 0 "$FRAME_DIR"/frame_*.png "$OUTPUT_GIF" 2>/dev/null

if [ -f "$OUTPUT_GIF" ]; then
    FILE_SIZE=$(stat -c%s "$OUTPUT_GIF" 2>/dev/null || echo 0)
    echo "[record] SUCCESS: $OUTPUT_GIF (${FILE_SIZE} bytes, ${FRAME_COUNT} frames)"
else
    echo "ERROR: Output GIF was not created"
    exit 3
fi
