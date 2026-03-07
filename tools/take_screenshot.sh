#!/usr/bin/env bash
# tools/take_screenshot.sh — Capture a screenshot of an FFE demo running under Xvfb
#
# Usage:
#   ./tools/take_screenshot.sh <demo_binary> <output_png> [wait_seconds] [lua_script]
#
# Arguments:
#   demo_binary   Path to the compiled demo executable (e.g., build/clang-release/examples/3d_demo/ffe_3d_demo)
#   output_png    Path where the screenshot PNG will be saved (e.g., docs/screenshots/3d_demo.png)
#   wait_seconds  How long to let the demo run before capture (default: 3)
#   lua_script    Optional Lua script path for ffe_runtime (passed as first arg to the demo)
#
# Requirements:
#   xvfb, xwd, imagemagick (convert), xdotool
#   All installed via: sudo apt-get install -y xvfb xdotool imagemagick
#
# How it works:
#   1. Starts Xvfb on an auto-selected display
#   2. Launches the demo binary against that display
#   3. Waits the specified time for the demo to initialise and render
#   4. Captures the X root window via xwd + convert (reliable, no compositor needed)
#   5. Crops the capture to the engine window (1280x720 default) from top-left
#   6. Kills the demo and Xvfb
#   7. Reports success or failure
#
# Exit codes:
#   0 = success (PNG produced)
#   1 = missing arguments or tools
#   2 = demo failed to start
#   3 = capture failed

set -euo pipefail

# --- Argument parsing ---
if [ $# -lt 2 ]; then
    echo "Usage: $0 <demo_binary> <output_png> [wait_seconds] [lua_script]"
    echo ""
    echo "Examples:"
    echo "  $0 build/clang-release/examples/3d_demo/ffe_3d_demo docs/screenshots/3d_demo.png"
    echo "  $0 build/clang-release/examples/runtime/ffe_runtime docs/screenshots/showcase.png 5 examples/showcase/game.lua"
    exit 1
fi

DEMO_BINARY="$1"
OUTPUT_PNG="$2"
WAIT_SECONDS="${3:-3}"
LUA_SCRIPT="${4:-}"

# --- Validate inputs ---
if [ ! -x "$DEMO_BINARY" ]; then
    echo "ERROR: Demo binary not found or not executable: $DEMO_BINARY"
    exit 1
fi

# Check required tools
for tool in Xvfb xwd convert; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: Required tool not found: $tool"
        echo "Install with: sudo apt-get install -y xvfb imagemagick"
        exit 1
    fi
done

# Create output directory if needed
OUTPUT_DIR="$(dirname "$OUTPUT_PNG")"
mkdir -p "$OUTPUT_DIR"

# --- Start Xvfb ---
# Find a free display number
DISPLAY_NUM=99
while [ -e "/tmp/.X${DISPLAY_NUM}-lock" ]; do
    DISPLAY_NUM=$((DISPLAY_NUM + 1))
    if [ "$DISPLAY_NUM" -gt 199 ]; then
        echo "ERROR: Could not find a free X display (tried :99 to :199)"
        exit 2
    fi
done

echo "[screenshot] Starting Xvfb on :${DISPLAY_NUM} (1920x1080x24)"
Xvfb ":${DISPLAY_NUM}" -screen 0 1920x1080x24 &>/dev/null &
XVFB_PID=$!

# Cleanup function
cleanup() {
    local exit_code=$?
    if [ -n "${DEMO_PID:-}" ] && kill -0 "$DEMO_PID" 2>/dev/null; then
        kill "$DEMO_PID" 2>/dev/null || true
        wait "$DEMO_PID" 2>/dev/null || true
    fi
    if [ -n "${XVFB_PID:-}" ] && kill -0 "$XVFB_PID" 2>/dev/null; then
        kill "$XVFB_PID" 2>/dev/null || true
        wait "$XVFB_PID" 2>/dev/null || true
    fi
    # Clean up temp file
    rm -f "${TEMP_XWD:-}" 2>/dev/null || true
    exit "$exit_code"
}
trap cleanup EXIT

# Wait for Xvfb to be ready
sleep 0.5
if ! kill -0 "$XVFB_PID" 2>/dev/null; then
    echo "ERROR: Xvfb failed to start"
    exit 2
fi

export DISPLAY=":${DISPLAY_NUM}"

# --- Launch demo ---
echo "[screenshot] Launching: $DEMO_BINARY ${LUA_SCRIPT:+$LUA_SCRIPT}"
if [ -n "$LUA_SCRIPT" ]; then
    "$DEMO_BINARY" "$LUA_SCRIPT" &>/dev/null &
else
    "$DEMO_BINARY" &>/dev/null &
fi
DEMO_PID=$!

# Wait for the demo to initialize and render
echo "[screenshot] Waiting ${WAIT_SECONDS}s for demo to render..."
sleep "$WAIT_SECONDS"

# Verify demo is still running
if ! kill -0 "$DEMO_PID" 2>/dev/null; then
    echo "ERROR: Demo process exited before capture (PID $DEMO_PID)"
    echo "       Try running the demo manually to diagnose:"
    echo "       xvfb-run -a $DEMO_BINARY ${LUA_SCRIPT:-}"
    exit 2
fi

# --- Capture screenshot ---
echo "[screenshot] Capturing via xwd + convert..."

TEMP_XWD="$(mktemp /tmp/ffe_capture_XXXXXX.xwd)"

# Capture the full X root window
if ! xwd -root -silent -display ":${DISPLAY_NUM}" > "$TEMP_XWD" 2>/dev/null; then
    echo "ERROR: xwd capture failed"
    # Fallback: try scrot
    if command -v scrot &>/dev/null; then
        echo "[screenshot] Trying scrot fallback..."
        if scrot "$OUTPUT_PNG" 2>/dev/null; then
            echo "[screenshot] Captured via scrot fallback: $OUTPUT_PNG"
            exit 0
        fi
    fi
    exit 3
fi

# Convert xwd to PNG. The engine window is 1280x720 and placed at top-left by the WM.
# Crop to the engine viewport area to avoid capturing empty Xvfb desktop.
if ! convert "xwd:${TEMP_XWD}" -crop 1280x720+0+0 +repage "$OUTPUT_PNG" 2>/dev/null; then
    # If crop fails, try without crop
    if ! convert "xwd:${TEMP_XWD}" "$OUTPUT_PNG" 2>/dev/null; then
        echo "ERROR: convert (ImageMagick) failed to produce PNG"
        exit 3
    fi
    echo "[screenshot] WARNING: Crop failed, saved full 1920x1080 capture"
fi

rm -f "$TEMP_XWD"
TEMP_XWD=""  # Prevent double-delete in cleanup

# --- Verify output ---
if [ ! -f "$OUTPUT_PNG" ]; then
    echo "ERROR: Output file was not created: $OUTPUT_PNG"
    exit 3
fi

FILE_SIZE=$(stat -c%s "$OUTPUT_PNG" 2>/dev/null || echo 0)
if [ "$FILE_SIZE" -lt 1000 ]; then
    echo "WARNING: Output file is suspiciously small (${FILE_SIZE} bytes): $OUTPUT_PNG"
    echo "         The capture may be blank. Check the image manually."
fi

# Get image dimensions
DIMENSIONS=$(identify "$OUTPUT_PNG" 2>/dev/null | awk '{print $3}' || echo "unknown")

echo "[screenshot] SUCCESS: $OUTPUT_PNG (${DIMENSIONS}, ${FILE_SIZE} bytes)"
exit 0
