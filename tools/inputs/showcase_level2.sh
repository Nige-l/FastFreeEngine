#!/usr/bin/env bash
# Input script for showcase Level 2 — "The Temple" (indoor)
# Receives window ID as $1
#
# Level 2 layout reference:
#   Player spawns on the south platform at z~-15 (6x5 units).
#   Central platform is 8x8, centred at origin; reached via a narrow bridge.
#   Perimeter walls at z=-20 (south), z=+20 (north), x=±18 (east/west).
#   Central pillars at (±5, ±5). Altar/crystals at the four cardinal platforms
#   (z=-13 blue, x=13 cyan, z=13 purple, x=-13 green).
#   DO NOT walk forward more than 1.5s — the south wall is only ~5 units
#   behind spawn; the narrow bridge to the centre begins ~3 units ahead.
#   Strafe left/right to pan across the indoor space without hitting walls.
#
# Camera is mouse-look only (GLFW cursor capture); xdotool mouse moves
# don't reach a captured cursor, so all navigation uses WASD movement keys.
WID="${1:-}"

focus() {
    [ -n "$WID" ] && xdotool windowfocus "$WID" 2>/dev/null || true
}

focus

# Wait for scene to load (temple geometry + lava pit)
sleep 2

# Activate window so key events reach the GLFW window
xdotool windowactivate --sync "$WID" 2>/dev/null || true

# Walk forward onto the south platform (short — indoor, walls close)
xdotool keydown --window "$WID" w
sleep 1.5
xdotool keyup --window "$WID" w

# Strafe right to see the altar from the side and east pillar
xdotool keydown --window "$WID" d
sleep 1
xdotool keyup --window "$WID" d

# Strafe left to pan across the room (east wall -> west pillar)
xdotool keydown --window "$WID" a
sleep 2
xdotool keyup --window "$WID" a

# Press F (attack) near enemies/crystals
xdotool key --window "$WID" f
sleep 0.3

# Walk forward 1s further along the bridge toward centre
xdotool keydown --window "$WID" w
sleep 1
xdotool keyup --window "$WID" w

# Press F again to show combat at range
xdotool key --window "$WID" f
sleep 0.3
