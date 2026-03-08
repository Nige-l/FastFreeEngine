# Skill: Record Demo GIFs

Record short gameplay GIFs of FFE demos on the user's real display and GPU.

## When to use
- After engine/renderer changes to visually verify demos
- Before commits to capture demo state
- When the user asks to "test demos", "record GIFs", or "see how things look"

## Prerequisites
- Build must be up to date (`cmake --build build/legacy-debug --parallel $(nproc)`)
- User's real display must be available (`DISPLAY=:0`)
- `xdotool` and ImageMagick `import`/`convert` must be installed

## Recording tool
```
./tools/record_demo.sh <binary> <output_gif> [duration_seconds] [lua_script] [input_script]
```

## Important rules
- **Always use the real display** (`DISPLAY=:0`) — NOT headless. The user wants to see demos on their screen and GPU.
- **Kill stale processes** before each recording:
  ```bash
  pkill -9 -f "ffe_showcase|ffe_3d_demo|ffe_pong|ffe_breakout|ffe_lua_demo|ffe_net_demo" 2>/dev/null || true
  sleep 1
  ```
- **One demo at a time** — don't run multiple demos simultaneously
- **Use input scripts** where available to exercise gameplay

## Demo recordings

### Pong (8s)
```bash
./tools/record_demo.sh build/legacy-debug/examples/pong/ffe_pong docs/recordings/pong.gif 8 "" tools/inputs/pong_play.sh
```

### Breakout (8s)
```bash
./tools/record_demo.sh build/legacy-debug/examples/breakout/ffe_breakout docs/recordings/breakout.gif 8
```

### 3D Demo (10s)
```bash
./tools/record_demo.sh build/legacy-debug/examples/3d_demo/ffe_3d_demo docs/recordings/3d_demo.gif 10 "" tools/inputs/3d_demo_explore.sh
```

### Showcase Level 1 — The Courtyard (12s)
```bash
./tools/record_demo.sh build/legacy-debug/examples/showcase/ffe_showcase docs/recordings/showcase_level1.gif 12 level1 tools/inputs/showcase_explore.sh
```

### Showcase Level 2 — The Cavern (12s)
```bash
./tools/record_demo.sh build/legacy-debug/examples/showcase/ffe_showcase docs/recordings/showcase_level2.gif 12 level2 tools/inputs/showcase_explore.sh
```

### Showcase Level 3 — The Highlands (12s)
```bash
./tools/record_demo.sh build/legacy-debug/examples/showcase/ffe_showcase docs/recordings/showcase_level3.gif 12 level3 tools/inputs/showcase_explore.sh
```

## Input scripts available
| Script | Demo | Actions |
|--------|------|---------|
| `tools/inputs/pong_play.sh` | Pong | Start game, move paddles |
| `tools/inputs/3d_demo_explore.sh` | 3D Demo | Orbit camera, zoom, cast ray |
| `tools/inputs/showcase_explore.sh` | Showcase | Walk forward, turn, strafe |

## Viewing results
GIFs are saved to `docs/recordings/`. After recording, check file sizes — a valid GIF should be at least 50KB. Tiny files indicate capture failure.

## Dispatch pattern
This should be dispatched to the **ops** agent. The ops agent runs the build, then records each demo sequentially, then reports results with file sizes.
