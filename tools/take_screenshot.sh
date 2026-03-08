#!/usr/bin/env bash
# tools/take_screenshot.sh — Capture a screenshot of an FFE demo binary
#
# Modes:
#   Real display (auto, default when DISPLAY is set): Launches demo on existing X display,
#     captures with scrot/ImageMagick. Reflects real GPU rendering. Preferred for local dev.
#   Headless (auto when DISPLAY is unset, or --headless flag): Xvfb + llvmpipe. For CI.
#
# Usage:
#   ./tools/take_screenshot.sh [--headless] <demo_binary> <output_png> [wait_seconds] [lua_script]
#
# Arguments:
#   --headless    Force headless mode (Xvfb + llvmpipe) even if DISPLAY is set
#   demo_binary   Path to the compiled demo executable (e.g., build/clang-release/examples/3d_demo/ffe_3d_demo)
#   output_png    Path where the screenshot PNG will be saved (e.g., docs/screenshots/3d_demo.png)
#   wait_seconds  How long to let the demo run before capture (default: 3)
#   lua_script    Optional Lua script path for ffe_runtime (passed as first arg to the demo)
#
# Requirements (headless mode):
#   xvfb, xwd, imagemagick (convert)
#   Install with: sudo apt-get install -y xvfb imagemagick
#
# Requirements (real display mode):
#   scrot (preferred) or imagemagick (convert)
#   Install with: sudo apt-get install -y scrot
#
# How it works (real display mode):
#   1. Detects a usable X display via DISPLAY + xdpyinfo
#   2. Launches the demo binary on the existing display
#   3. Waits the specified time for the demo to initialise and render
#   4. Finds the demo window by PID via xdotool, captures it with ImageMagick import
#      (falls back to scrot --window, then position-based scrot as last resort)
#   5. Kills the demo
#   6. Reports success or failure
#
# How it works (headless mode):
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

# --- Flag parsing ---
FORCE_HEADLESS=0
if [ "${1:-}" = "--headless" ]; then
    FORCE_HEADLESS=1
    shift
fi

# --- Argument parsing ---
if [ $# -lt 2 ]; then
    echo "Usage: $0 [--headless] <demo_binary> <output_png> [wait_seconds] [lua_script]"
    echo ""
    echo "Examples:"
    echo "  $0 build/clang-release/examples/3d_demo/ffe_3d_demo docs/screenshots/3d_demo.png"
    echo "  $0 build/clang-release/examples/runtime/ffe_runtime docs/screenshots/showcase.png 5 examples/showcase/game.lua"
    echo "  $0 --headless build/clang-release/examples/3d_demo/ffe_3d_demo docs/screenshots/3d_demo.png"
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

# Create output directory if needed
OUTPUT_DIR="$(dirname "$OUTPUT_PNG")"
mkdir -p "$OUTPUT_DIR"

# --- Mode detection ---
USE_REAL_DISPLAY=0
if [ "$FORCE_HEADLESS" -eq 0 ] && [ -n "${DISPLAY:-}" ]; then
    if command -v xdpyinfo &>/dev/null && xdpyinfo -display "$DISPLAY" &>/dev/null; then
        USE_REAL_DISPLAY=1
    fi
fi

# ==============================================================================
# REAL DISPLAY MODE
# ==============================================================================
if [ "$USE_REAL_DISPLAY" -eq 1 ]; then
    echo "[screenshot] Mode: real display ($DISPLAY)"

    # Check we have at least one capture tool (scrot or imagemagick import).
    # xdotool is used for window-by-PID lookup; warn if missing but don't abort
    # (the position-based fallback will be used instead).
    if ! command -v scrot &>/dev/null && ! command -v import &>/dev/null; then
        echo "ERROR: No capture tool found. Install scrot or imagemagick."
        echo "Install with: sudo apt-get install -y scrot imagemagick"
        exit 1
    fi
    if ! command -v xdotool &>/dev/null; then
        echo "[screenshot] WARNING: xdotool not found — window-by-PID capture unavailable."
        echo "             Install with: sudo apt-get install -y xdotool"
        echo "             Falling back to fixed-region capture (may capture wrong area)."
    fi

    DEMO_PID=""

    cleanup_real() {
        local exit_code=$?
        if [ -n "${DEMO_PID:-}" ] && kill -0 "$DEMO_PID" 2>/dev/null; then
            kill "$DEMO_PID" 2>/dev/null || true
            wait "$DEMO_PID" 2>/dev/null || true
        fi
        exit "$exit_code"
    }
    trap cleanup_real EXIT

    # Launch demo
    echo "[screenshot] Launching: $DEMO_BINARY ${LUA_SCRIPT:+$LUA_SCRIPT}"
    if [ -n "$LUA_SCRIPT" ]; then
        "$DEMO_BINARY" "$LUA_SCRIPT" &>/dev/null &
    else
        "$DEMO_BINARY" &>/dev/null &
    fi
    DEMO_PID=$!

    # Wait for demo to initialise and render
    echo "[screenshot] Waiting ${WAIT_SECONDS}s for demo to render..."
    sleep "$WAIT_SECONDS"

    # Verify demo is still running
    if ! kill -0 "$DEMO_PID" 2>/dev/null; then
        echo "ERROR: Demo process exited before capture (PID $DEMO_PID)"
        echo "       Try running the demo manually to diagnose:"
        echo "       $DEMO_BINARY ${LUA_SCRIPT:-}"
        exit 2
    fi

    # Capture — find the window by PID and capture it directly
    echo "[screenshot] Capturing demo window (PID $DEMO_PID)..."
    CAPTURE_OK=0
    WINDOW_ID=""

    # Find window belonging to this process
    if command -v xdotool &>/dev/null; then
        WINDOW_ID=$(xdotool search --onlyvisible --pid "$DEMO_PID" 2>/dev/null | head -1)
    fi

    if [ -n "$WINDOW_ID" ]; then
        # Capture the specific window via ImageMagick import
        if command -v import &>/dev/null; then
            if import -window "$WINDOW_ID" "$OUTPUT_PNG" 2>/dev/null; then
                CAPTURE_OK=1
                echo "[screenshot] Captured window $WINDOW_ID via ImageMagick import"
            fi
        fi
        # Fallback: scrot --window
        if [ "$CAPTURE_OK" -eq 0 ] && command -v scrot &>/dev/null; then
            if scrot --window "$WINDOW_ID" "$OUTPUT_PNG" 2>/dev/null; then
                CAPTURE_OK=1
                echo "[screenshot] Captured window $WINDOW_ID via scrot"
            fi
        fi
    fi

    # Last resort: full screen crop (position-based fallback)
    if [ "$CAPTURE_OK" -eq 0 ] && command -v scrot &>/dev/null; then
        if scrot -a 0,0,1280,720 "$OUTPUT_PNG" 2>/dev/null; then
            CAPTURE_OK=1
            echo "[screenshot] WARNING: Captured fixed region (window not found), may be wrong area"
        fi
    fi

    if [ "$CAPTURE_OK" -eq 0 ]; then
        echo "ERROR: All capture methods failed"
        exit 3
    fi

    # Kill demo (cleanup trap will also catch this)
    kill "$DEMO_PID" 2>/dev/null || true
    wait "$DEMO_PID" 2>/dev/null || true
    DEMO_PID=""

    # Verify output
    if [ ! -f "$OUTPUT_PNG" ]; then
        echo "ERROR: Output file was not created: $OUTPUT_PNG"
        exit 3
    fi

    FILE_SIZE=$(stat -c%s "$OUTPUT_PNG" 2>/dev/null || echo 0)
    if [ "$FILE_SIZE" -lt 1000 ]; then
        echo "WARNING: Output file is suspiciously small (${FILE_SIZE} bytes): $OUTPUT_PNG"
        echo "         The capture may be blank. Check the image manually."
    fi

    DIMENSIONS=$(identify "$OUTPUT_PNG" 2>/dev/null | awk '{print $3}' || echo "unknown")
    echo "[screenshot] SUCCESS: $OUTPUT_PNG (${DIMENSIONS}, ${FILE_SIZE} bytes)"
    exit 0
fi

# ==============================================================================
# HEADLESS MODE (Xvfb + llvmpipe)
# ==============================================================================
echo "[screenshot] Mode: headless (Xvfb + llvmpipe)"

# Check required tools
for tool in Xvfb xwd convert; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: Required tool not found: $tool"
        echo "Install with: sudo apt-get install -y xvfb imagemagick"
        exit 1
    fi
done

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

DEMO_PID=""
TEMP_XWD=""

cleanup_headless() {
    local exit_code=$?
    if [ -n "${DEMO_PID:-}" ] && kill -0 "$DEMO_PID" 2>/dev/null; then
        kill "$DEMO_PID" 2>/dev/null || true
        wait "$DEMO_PID" 2>/dev/null || true
    fi
    if [ -n "${XVFB_PID:-}" ] && kill -0 "$XVFB_PID" 2>/dev/null; then
        kill "$XVFB_PID" 2>/dev/null || true
        wait "$XVFB_PID" 2>/dev/null || true
    fi
    rm -f "${TEMP_XWD:-}" 2>/dev/null || true
    exit "$exit_code"
}
trap cleanup_headless EXIT

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
