# API Reference

Complete API documentation for FastFreeEngine, covering both the Lua scripting API (`ffe.*` bindings) and the C++ engine API.

## How These Pages Are Generated

Every engine subsystem ships a `.context.md` file in its directory. These files are the **source of truth** for API documentation -- they are written for both humans and AI assistants, and are kept in sync with the engine code by the `api-designer` agent.

The pages in this section are **auto-generated** from those `.context.md` files by the extraction pipeline. Do not edit the subsystem pages by hand -- your changes will be overwritten on the next generation run.

To regenerate these pages from the latest engine source:

```bash
python website/scripts/generate_api_docs.py
```

The script reads each `.context.md`, adds MkDocs front matter and an auto-generation notice, and writes the result to `website/docs/api/`. See `website/.context.md` for full pipeline details.

## Subsystems

| Subsystem | Description | Status |
|-----------|-------------|--------|
| [Core](core.md) | ECS, Application lifecycle, Input (keyboard/mouse/gamepad), Timers, Arena Allocator | Active |
| [Renderer](renderer.md) | Sprites, Text, Particles, Tilemap, 3D Mesh, Materials, Camera, Shadows, Skybox | Active |
| [Audio](audio.md) | Sound effects, music, 3D spatial audio | Active |
| [Physics](physics.md) | 2D collision detection (AABB/Circle), spatial hash, layer/mask filtering | Active |
| [Scripting](scripting.md) | LuaJIT sandbox, `ffe.*` API bindings, instruction budget, save/load | Active |
| [Networking](networking.md) | Client-server architecture, messages, replication | Planned |
| [Editor](editor.md) | Dear ImGui overlay, entity inspector, editor-hosted mode | In Progress |
| [Scene](scene.md) | Scene graph, serialisation, scene management | Planned |

## For AI Assistants

If you are an LLM helping a developer build a game with FastFreeEngine, these pages contain everything you need to generate correct, idiomatic code. Each subsystem page includes:

- **Public API** with full function signatures, parameter types, and return types
- **Common usage patterns** with working code examples
- **Anti-patterns** -- what NOT to do and why
- **Hardware tier support** -- which tiers each feature supports
- **Dependencies** -- what other subsystems are required

Start with [Core](core.md) for the fundamentals (ECS, Application, Input), then [Scripting](scripting.md) for the Lua game API.
