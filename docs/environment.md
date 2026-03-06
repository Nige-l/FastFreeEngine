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
- mingw-w64 (2026-03-06: MinGW-w64 cross-compilation toolchain for Windows builds)

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
| x86_64-w64-mingw32-g++ | GCC 13-win32 (mingw-w64 11.0.1-3build1) |
| x86_64-w64-mingw32-windres | GNU Binutils 2.41.90.20240122 |

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

### 2026-03-06: MinGW-w64 cross-compilation toolchain (Windows build prep — Session 39 Phase 1/2)

**What changed:**
- Installed system package `mingw-w64` (11.0.1-3build1) via `apt-get install mingw-w64`
  - Provides `x86_64-w64-mingw32-gcc`, `x86_64-w64-mingw32-g++` (GCC 13-win32), `x86_64-w64-mingw32-windres`
- Created `cmake/toolchains/mingw-w64-x86_64.cmake`: CMake toolchain file for cross-compiling to Windows x64
- Modified `cmake/CompilerFlags.cmake`: wrapped `find_program(MOLD_LINKER mold)` and `-fuse-ld=mold` flags inside `if(NOT WIN32)` guard — mold is Linux-only and does not exist on Windows
- Modified `engine/core/CMakeLists.txt`: added `if(WIN32) target_link_libraries(ffe_core PRIVATE ws2_32 opengl32) endif()` — Winsock (ws2_32) and OpenGL (opengl32) must be linked explicitly on Windows; on Linux the system linker resolves these automatically
- Modified `engine/audio/CMakeLists.txt`: added `$<$<PLATFORM_ID:Windows>:winmm>` to `ffe_audio` link libraries — miniaudio on Windows uses DirectSound/WASAPI which requires winmm; the existing Linux-only dl/pthread/m entries were already guarded with `$<$<PLATFORM_ID:Linux>:...>` generator expressions so no change was needed there

**Why:**
- Phase 1/2 of Session 39: prepare the build system for Windows cross-compilation from Linux using the `x64-mingw-dynamic` vcpkg triplet
- Without the mold guard, the Windows cross-build would fail immediately trying to invoke `-fuse-ld=mold` which does not exist in the MinGW-w64 toolchain
- Without ws2_32/opengl32/winmm, the Windows link step would produce undefined reference errors for every Winsock, OpenGL, and multimedia call

**POSIX audit results (for engine-dev to address — see below):**
See Section "POSIX-Specific Calls Requiring Windows Guards" for the full list.

**Verification:**
- `x86_64-w64-mingw32-g++ --version` confirms GCC 13-win32 is on PATH
- `x86_64-w64-mingw32-windres --version` confirms windres is on PATH
- `cmake/CompilerFlags.cmake`: `grep -n "NOT WIN32"` confirms the mold guard is present at line 13
- `engine/core/CMakeLists.txt`: `grep -n "WIN32"` confirms ws2_32/opengl32 block is present
- `engine/audio/CMakeLists.txt`: `grep -n "Windows"` confirms winmm generator expression is present
- Linux builds are unaffected: the `if(NOT WIN32)` guard leaves all existing Linux behaviour intact

## POSIX-Specific Calls Requiring Windows Guards

These are the calls engine-dev must wrap in `#ifdef`/`#else` guards before a Windows cross-build can succeed. None of these are in hot paths — they are all in load-time I/O functions.

### `realpath()` — not available on MinGW/Windows

Windows equivalent: `_fullpath(char* absPath, const char* relPath, size_t maxLength)` from `<stdlib.h>`.
Signature difference: `_fullpath` takes a max-length argument; `realpath` uses `PATH_MAX` (4096).
On failure: `_fullpath` returns `nullptr` just like `realpath`.

**Files and lines:**

`engine/scripting/script_engine.cpp`:
- Line 498-502: `setSaveRoot` — `realpath(absolutePath, resolved)`
- Line 3185-3186: `saveData` — `realpath(savesDir, resolvedDir)`
- Line 3193-3194: `saveData` — `realpath(root, resolvedRoot)`
- Line 3351-3352: `loadData` — `realpath(fullPath, resolvedPath)`
- Line 3359-3360: `loadData` — `realpath(root, resolvedRoot)`
- Line 3486: `loadTexture` Lua binding — `realpath(absPath, resolvedPath)`
- Line 3493: `loadTexture` Lua binding — `realpath(assetRoot, resolvedRoot)`

`engine/renderer/texture_loader.cpp`:
- Line 178-179: `loadTexture` — `::realpath(fullPath, canonPath)`

`engine/renderer/mesh_loader.cpp`:
- Line 229-230: `loadMesh` — `::realpath(fullPath, canonPath)`

`engine/audio/audio.cpp`:
- Line 731-732: `loadSound` — `::realpath(fullPath, canonPath)`
- Line 989-990: `loadMusic` — `::realpath(fullPath, canonPath)`

**Recommended guard pattern:**
```cpp
// In a shared header (e.g., engine/core/platform.h):
#ifdef _WIN32
#  include <stdlib.h>
inline char* ffe_realpath(const char* path, char* resolved) {
    return _fullpath(resolved, path, PATH_MAX);
}
#else
#  include <stdlib.h>
inline char* ffe_realpath(const char* path, char* resolved) {
    return ::realpath(path, resolved);
}
#endif
```
Then replace all `realpath(` and `::realpath(` call sites with `ffe_realpath(`.

### `stat()` / `struct stat` — needs `_stat()` / `struct _stat` on MSVC; MinGW provides POSIX stat

**MinGW note:** MinGW-w64 provides POSIX `stat()` and `struct stat` via `<sys/stat.h>` — these work as-is on MinGW. No change needed for the MinGW cross-compilation target. This would only be an issue if targeting MSVC.

**Files (informational — no action needed for MinGW):**

`engine/scripting/script_engine.cpp`:
- Line 3212-3213: `struct stat st; stat(fullPath, &st)`
- Line 3333-3334: `struct stat st; stat(fullPath, &st)`

`engine/renderer/texture_loader.cpp`:
- Line 132-133: `struct stat st{}; ::stat(absPath, &st)`
- Line 205-206: `struct stat fileStat{}; ::stat(canonPath, &fileStat)`

`engine/renderer/mesh_loader.cpp`:
- Line 251-252: `struct stat fileStat{}; ::stat(canonPath, &fileStat)`

`engine/audio/audio.cpp`:
- Line 320-321: `struct stat st{}; ::stat(absPath, &st)`
- Line 752-753: `struct stat fileStat{}; ::stat(canonPath, &fileStat)`
- Line 1008-1009: `struct stat fileStat{}; ::stat(canonPath, &fileStat)`

### `<sys/stat.h>` include

`engine/scripting/script_engine.cpp` line 35, `engine/renderer/texture_loader.cpp` line 63, `engine/renderer/mesh_loader.cpp` line 67, `engine/audio/audio.cpp` line 144.
MinGW provides `<sys/stat.h>` — no change needed for the MinGW target.

### `dlopen` / `dlclose` / `dlsym`

Not present in any engine source file directly. miniaudio uses `dlopen` internally on Linux to load ALSA/PulseAudio/JACK. This is handled inside the miniaudio single-header with its own platform `#ifdef`s — miniaudio already has full Windows support and will use DirectSound/WASAPI instead of dl-loading ALSA on Windows. The `$<$<PLATFORM_ID:Linux>:dl>` entry in `engine/audio/CMakeLists.txt` already guards this correctly.

### Summary for engine-dev

One change is required before a successful Windows cross-build:

1. **`realpath()` in 5 files (11 call sites):** Replace all `realpath()` / `::realpath()` calls with a thin `ffe_realpath()` wrapper that calls `_fullpath()` on Windows. The wrapper belongs in a new `engine/core/platform.h` header (or similar). This is the only code change blocking a Windows build. All other POSIX calls are either MinGW-compatible or already platform-guarded.
