#!/usr/bin/env bash
# tools/capture_all_screenshots.sh — Capture screenshots of all FFE demos for documentation
#
# Usage:
#   ./tools/capture_all_screenshots.sh [build_dir]
#
# Arguments:
#   build_dir   Path to the build directory (default: build/clang-release)
#
# Output:
#   docs/screenshots/<demo_name>.png for each demo
#
# This script calls tools/take_screenshot.sh for each demo. It reports which
# demos succeeded and which failed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SCREENSHOT_SCRIPT="$SCRIPT_DIR/take_screenshot.sh"
OUTPUT_DIR="$PROJECT_DIR/docs/screenshots"
BUILD_DIR="${1:-$PROJECT_DIR/build/clang-release}"

# Validate
if [ ! -x "$SCREENSHOT_SCRIPT" ]; then
    echo "ERROR: Screenshot script not found: $SCREENSHOT_SCRIPT"
    exit 1
fi

if [ ! -d "$BUILD_DIR/examples" ]; then
    echo "ERROR: Build directory not found or has no examples: $BUILD_DIR/examples"
    echo "       Run a build first, then re-run this script."
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

echo "========================================"
echo " FFE Screenshot Capture"
echo "========================================"
echo "Build dir:  $BUILD_DIR"
echo "Output dir: $OUTPUT_DIR"
echo ""

# Track results
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0
RESULTS=""

# Helper function
capture() {
    local name="$1"
    local binary="$2"
    local wait="$3"
    local lua_script="${4:-}"
    local output="$OUTPUT_DIR/${name}.png"

    TOTAL=$((TOTAL + 1))

    if [ ! -x "$binary" ]; then
        echo "[SKIP] $name — binary not found: $binary"
        SKIPPED=$((SKIPPED + 1))
        RESULTS="${RESULTS}\n  SKIP  $name (binary not found)"
        return
    fi

    echo "--- Capturing: $name ---"
    if [ -n "$lua_script" ]; then
        if "$SCREENSHOT_SCRIPT" "$binary" "$output" "$wait" "$lua_script"; then
            PASSED=$((PASSED + 1))
            RESULTS="${RESULTS}\n  PASS  $name -> $output"
        else
            FAILED=$((FAILED + 1))
            RESULTS="${RESULTS}\n  FAIL  $name"
        fi
    else
        if "$SCREENSHOT_SCRIPT" "$binary" "$output" "$wait"; then
            PASSED=$((PASSED + 1))
            RESULTS="${RESULTS}\n  PASS  $name -> $output"
        else
            FAILED=$((FAILED + 1))
            RESULTS="${RESULTS}\n  FAIL  $name"
        fi
    fi
    echo ""
}

# --- Capture each demo ---

# 3D Demo: Has cubes, lighting, physics — give it time to set up scene
capture "3d_demo" \
    "$BUILD_DIR/examples/3d_demo/ffe_3d_demo" \
    4

# Pong: 2D game, loads fast
capture "pong" \
    "$BUILD_DIR/examples/pong/ffe_pong" \
    3

# Breakout: 2D game, loads fast
capture "breakout" \
    "$BUILD_DIR/examples/breakout/ffe_breakout" \
    3

# Showcase: Multi-level game, starts at menu screen
capture "showcase" \
    "$BUILD_DIR/examples/showcase/ffe_showcase" \
    4

# Showcase via runtime: Same game via ffe_runtime
capture "showcase_runtime" \
    "$BUILD_DIR/examples/runtime/ffe_runtime" \
    4 \
    "$PROJECT_DIR/examples/showcase/game.lua"

# Hello Sprites: Basic sprite rendering
capture "hello_sprites" \
    "$BUILD_DIR/examples/hello_sprites/ffe_hello_sprites" \
    3

# Lua Demo: Scripted demo
capture "lua_demo" \
    "$BUILD_DIR/examples/lua_demo/ffe_lua_demo" \
    3

# Interactive Demo: Feature showcase
capture "interactive_demo" \
    "$BUILD_DIR/examples/interactive_demo/ffe_interactive_demo" \
    3

# --- Summary ---
echo "========================================"
echo " Results: $PASSED passed, $FAILED failed, $SKIPPED skipped (of $TOTAL)"
echo "========================================"
echo -e "$RESULTS"
echo ""

if [ "$FAILED" -gt 0 ]; then
    echo "Some captures failed. Check the output above for details."
    exit 1
fi

echo "All screenshots saved to: $OUTPUT_DIR/"
ls -la "$OUTPUT_DIR/"*.png 2>/dev/null || true
exit 0
