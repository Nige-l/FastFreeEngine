# FastFreeEngine

**A performance-first, open-source C++ game engine that runs on old hardware.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

FastFreeEngine (FFE) is a complete game development platform -- engine, editor, networking, scripting, and AI-native documentation -- built so that anyone can make games, regardless of their hardware budget. It guarantees 60 fps on hardware from 2012. MIT licensed, forever.

![3D Demo](docs/recordings/3d_demo.gif)

## Features

- **2D + 3D rendering** -- OpenGL 3.3 default, Vulkan backend, PBR materials, shadow mapping, post-processing (bloom, SSAO, FXAA)
- **Lua scripting** -- Sandboxed LuaJIT with 200+ `ffe.*` bindings across all subsystems
- **Multiplayer networking** -- Client-server UDP with prediction, reconciliation, and lag compensation
- **3D physics** -- Jolt Physics integration with rigid bodies, collisions, and raycasting
- **Standalone editor** -- Scene hierarchy, inspector, viewport gizmos, undo/redo, asset browser, play-in-editor
- **Terrain system** -- Heightmap rendering with splat-map texturing, LOD, and water planes
- **Skeletal animation** -- GPU skinning with crossfade blending and root motion
- **AI-native docs** -- Every subsystem ships a `.context.md` file so AI assistants generate correct FFE code immediately

## Quick Start

```bash
# Prerequisites: C++20 compiler (Clang-18 or GCC-13), CMake, Ninja, vcpkg
# See docs for full dependency list: https://nige-l.github.io/FastFreeEngine/

# Clone and build
git clone https://github.com/Nige-L/FastFreeEngine.git
cd FastFreeEngine
cmake -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DFFE_TIER=LEGACY
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure --parallel $(nproc)

# Run the 3D showcase game
./build/examples/showcase/ffe_showcase
```

## Build Requirements

| Tool | Version |
|------|---------|
| C++ Standard | C++20 |
| Primary compiler | Clang-18 |
| Secondary compiler | GCC-13 |
| Build system | CMake + Ninja |
| Linker | mold |
| Dependencies | vcpkg (see `vcpkg.json`) |

Platforms: Linux (primary), Windows (MinGW cross-compile), macOS (paused -- upstream LuaJIT issue).

## Links

- **Documentation** -- [nige-l.github.io/FastFreeEngine](https://nige-l.github.io/FastFreeEngine/)
- **License** -- [MIT](LICENSE)
- **Contributing** -- [CONTRIBUTING.md](CONTRIBUTING.md)
- **Roadmap** -- [docs/ROADMAP.md](docs/ROADMAP.md)
- **Dev Log** -- [docs/devlog.md](docs/devlog.md)
