# FastFreeEngine Development Environment

This document records the installed toolchain and library versions for the FastFreeEngine project.

## System Packages

The following packages were installed via `apt-get`:

- build-essential, gcc-13, g++-13
- clang-18, clangd-18, clang-tidy-18, clang-format-18, lldb-18
- cmake, ninja-build
- mold (linker), ccache
- valgrind, linux-tools-common, linux-tools-generic, hotspot
- libgl1-mesa-dev, mesa-utils, libvulkan-dev, vulkan-tools
- vulkan-utility-libraries-dev (replaces vulkan-validationlayers-dev)
- glslang-tools, spirv-tools
- libglfw3-dev, libasound2-dev, libpulse-dev
- luajit, libluajit-5.1-dev
- sox, libsox-fmt-all (audio asset generation)
- xvfb, pkg-config, curl, wget, unzip
- libxinerama-dev, libxcursor-dev, xorg-dev, libglu1-mesa-dev (2026-03-06: required by vcpkg glfw3 build for imgui glfw-binding feature)

## Toolchain

| Tool | Version |
|------|---------|
| gcc-13 | gcc-13 (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 |
| g++-13 | g++-13 (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 |
| clang-18 | Ubuntu clang version 18.1.3 (1ubuntu1) |
| cmake | cmake version 3.28.3 |
| ninja | 1.11.1 |
| mold | mold 2.30.0 (compatible with GNU ld) |
| ccache | ccache version 4.9.1 |
| valgrind | valgrind-3.22.0 |
| luajit | LuaJIT 2.1.1703358377 |
| pkg-config | 1.8.1 |
| sox | SoX v14.4.2 |

## vcpkg

- **Version:** vcpkg package management program version 2026-03-04-4b3e4c276b5b87a649e66341e11553e8c577459c
- **Location:** `$HOME/vcpkg`
- **Environment variables:** `VCPKG_ROOT` and `PATH` configured in `~/.bashrc`

## vcpkg Dependencies

The following libraries are declared in `vcpkg.json`:

| Library | Features | Description |
|---------|----------|-------------|
| entt | (default) | Entity-Component-System framework |
| joltphysics | (default) | Physics engine |
| sol2 | (default) | C++ binding for Lua/LuaJIT |
| glm | (default) | OpenGL Mathematics library |
| imgui | glfw-binding, opengl3-binding | Immediate-mode GUI with GLFW and OpenGL3 backends |
| stb | (default) | Single-header image loading and utilities |
| nlohmann-json | (default) | JSON for Modern C++ |
| tracy | (default) | Frame profiler |
| catch2 | (default) | Unit testing framework |
| vulkan-memory-allocator | (default) | Vulkan memory allocation library |

## Change Log

### 2026-03-06: Audio demo assets and sox installation

**What changed:**
- Installed `sox` (v14.4.2) and `libsox-fmt-all` for generating audio test assets
- Created `assets/audio/sfx_beep.wav`: 0.15s 440Hz sine wave beep, 16-bit mono 48kHz PCM, 14KB
- Created `assets/audio/music_loop.ogg`: 8s C major chord (C4+E4+G4 mixed), Vorbis mono 48kHz, 23KB
- Copied audio files to `assets/textures/` as `sfx.wav` and `music.ogg` (names expected by `examples/lua_demo/game.lua` which uses `renderer::getAssetRoot()` pointing to `assets/textures/`)

**Why:**
- The lua_demo's `game.lua` calls `ffe.loadSound("music.ogg")` and `ffe.loadSound("sfx.wav")` but no audio files existed in the repo
- The demo's asset root is `assets/textures/` (set in `examples/lua_demo/main.cpp`), so audio files must reside there for the Lua `loadSound` calls to find them
- Canonical copies kept in `assets/audio/` with descriptive names for future use when a separate audio asset root is added

**Verification:**
- `file` command confirms correct formats (RIFF WAV PCM 16-bit, Ogg Vorbis)
- `sox --i` confirms expected duration, sample rate, and encoding for both files
- Both files under 50KB each

### 2026-03-06: Dear ImGui with GLFW/OpenGL3 backends

**What changed:**
- Updated `vcpkg.json`: imgui dependency now requests `glfw-binding` and `opengl3-binding` features (vcpkg version 1.91.9)
- Added `find_package(imgui CONFIG REQUIRED)` to top-level `CMakeLists.txt`
- Updated `engine/editor/CMakeLists.txt`: `ffe_editor` INTERFACE target now links `imgui::imgui` and defines `FFE_EDITOR` for Debug builds
- Updated `engine/CMakeLists.txt`: `ffe_engine` umbrella target now links `ffe_editor`
- Moved `option(FFE_BUILD_TESTS)` and `option(FFE_BUILD_EXAMPLES)` before `add_subdirectory(engine)` in top-level `CMakeLists.txt` to fix a pre-existing bug where `FFE_TEST` was not defined on `ffe_core` (the option was declared after engine/ was processed)
- Installed system packages: `libxinerama-dev`, `libxcursor-dev`, `xorg-dev`, `libglu1-mesa-dev` (required by vcpkg to build glfw3 from source for the imgui glfw-binding feature)

**Why:**
- Editor subsystem requires Dear ImGui for debug overlays and tooling
- `FFE_EDITOR` compile definition gates editor code to Debug builds only
- The GLFW and OpenGL3 backends match the engine's existing windowing (GLFW) and rendering (OpenGL 3.3) stack

**Verification:**
- Full build passes (clang++-18, Debug, LEGACY tier)
- All 263 tests pass
- Standalone compile+link test of `imgui.h`, `imgui_impl_glfw.h`, `imgui_impl_opengl3.h` with `ImGui::GetVersion()` succeeds

### 2026-03-06: Spritesheet test asset for animation system

**What changed:**
- Created `assets/textures/spritesheet.png`: 128x64 RGBA PNG, 4 columns x 2 rows grid of 32x32 pixel frames
- Each frame contains a uniquely colored circle (red, orange, yellow, green, cyan, blue, purple, magenta) with a white rotating line indicator and frame number overlay
- Generated with Python3 + Pillow (both already installed on system)

**Why:**
- Needed a multi-frame spritesheet asset to test the grid-based sprite animation system
- The rotating indicator and color cycling make it visually obvious whether animation frame sequencing is correct
- 8 distinct frames provide enough variation to verify looping, frame stepping, and UV coordinate calculations

**Verification:**
- `file` confirms: PNG image data, 128 x 64, 8-bit/color RGBA, non-interlaced
- Pillow confirms: Size (128, 64), Mode RGBA
