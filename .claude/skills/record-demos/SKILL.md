---
name: record-demos
description: Build and record GIF gameplay recordings of all FFE demos on the real display/GPU. Use after engine changes to visually verify demos, or before commits.
argument-hint: [demo-name or "all"]
allowed-tools: Bash, Agent, Read
---

# Record Demo GIFs

Record short gameplay GIFs of FFE demos on the user's **real display and GPU** (not headless).

Available demos: !`ls -1 build/examples/*/ffe_* 2>/dev/null | xargs -I{} basename {} | sed 's/ffe_//' || echo "(build first)"`

See [instructions.md](instructions.md) for the full recording workflow, demo commands, and input scripts.

## Quick usage

- `/record-demos all` — record all demos
- `/record-demos showcase` — record just the showcase levels
- `/record-demos pong` — record just pong

Dispatch this to the **ops** agent. Ops builds first, then records each demo sequentially on `DISPLAY=:0`, reporting file sizes.
