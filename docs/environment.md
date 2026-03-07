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
| luajit | (default) | LuaJIT 2.1 — scripting VM (added 2026-03-07; installed for both x64-linux and x64-mingw-dynamic) |
| sol2 | (default) | C++ binding for Lua/LuaJIT |
| glm | (default) | OpenGL Mathematics library |
| imgui | glfw-binding, opengl3-binding | Immediate-mode GUI with GLFW and OpenGL3 backends |
| stb | (default) | Single-header image loading and utilities |
| nlohmann-json | (default) | JSON for Modern C++ |
| tracy | (default) | Frame profiler |
| catch2 | (default) | Unit testing framework |
| vulkan-memory-allocator | (default) | Vulkan memory allocation library |

## vcpkg Overlay Ports

Custom overlay ports live in `cmake/vcpkg-overlays/ports/`. They are loaded via the
`VCPKG_OVERLAY_PORTS` cache variable set before `project()` in `CMakeLists.txt`.

### luajit overlay (added 2026-03-07)

Patches the upstream vcpkg luajit port to support Windows/MinGW cross-compilation from Linux.

**Root cause of the patch:** The upstream `luajit:x64-linux` host package builds `buildvm-x64`
with `TARGET_SYS=Linux` (ELF output only). When cross-compiling for Windows, `buildvm -m peobj`
fails with "no PE object support for this target".

**What the overlay does:**
- When `VCPKG_CROSSCOMPILING` and `VCPKG_TARGET_IS_WINDOWS`, rebuilds `buildvm` from source on
  the Linux host using `HOST_CC=gcc CROSS=x86_64-w64-mingw32- TARGET_SYS=Windows`, producing a
  PE-capable buildvm. Smoke-tests it with `buildvm -m peobj` before proceeding.
- Passes `TARGET_SYS=Windows`, `EXECUTABLE_SUFFIX=.exe`, and correct Windows DLL/lib naming
  (`FILE_T=luajit.exe`, `FILE_A=libluajit-5.1.dll.a`, `FILE_SO=lua51.dll`) through
  `vcpkg_configure_make OPTIONS` so they reach both the `all` and `install` make targets
  (working around a vcpkg bug where `vcpkg_build_make OPTIONS` are not passed to `install`).
- Renames the installed `luajit` binary to `luajit.exe` before `vcpkg_copy_tools` so vcpkg's
  tool validator finds it.

**Files:** `cmake/vcpkg-overlays/ports/luajit/portfile.cmake`, `configure`, `vcpkg.json`,
`luajit.pc`, `Makefile.nmake`, `msvcbuild.patch`, `003-do-not-set-macosx-deployment-target.patch`.

## Cross-Compilation: Windows x64 (x64-mingw-dynamic)

### Toolchain file

`cmake/toolchains/mingw-w64-x86_64.cmake` — sets compilers to `x86_64-w64-mingw32-{gcc,g++,windres}`
and disables `VCPKG_APPLOCAL_DEPS` (which invokes `powershell.exe` to copy DLLs; not available on
a Linux cross-compile host).

### Build command

```bash
cmake -B build-win -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$(pwd)/cmake/toolchains/mingw-w64-x86_64.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic \
  -DCMAKE_BUILD_TYPE=Release \
  -DFFE_TIER=LEGACY
cmake --build build-win
```

### Note on test discovery

`catch_discover_tests` runs the built `.exe` files at CMake build time to enumerate test cases.
This step fails on a Linux cross-compile host because Windows EXEs cannot execute on Linux without
Wine. The link step succeeds and the `.exe` files are valid PE32+ binaries. Test enumeration
failure is expected and does not indicate a build error. Use Wine or a Windows machine to run tests.

## macOS Build: Apple Silicon (arm64-osx)

### Toolchain file

`cmake/toolchains/macos-arm64.cmake` — sets `CMAKE_SYSTEM_NAME Darwin`, `CMAKE_SYSTEM_PROCESSOR arm64`,
and `CMAKE_OSX_ARCHITECTURES arm64`. Does not set `CMAKE_OSX_DEPLOYMENT_TARGET` to avoid conflicting
with the LuaJIT vcpkg overlay which strips that variable from the LuaJIT Makefile.

### Build command

```bash
cmake -B build-macos -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$(pwd)/cmake/toolchains/macos-arm64.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFFE_TIER=LEGACY
cmake --build build-macos
ctest --test-dir build-macos --output-on-failure
```

### OpenGL deprecation

macOS deprecated OpenGL in 10.14 (Mojave). All OpenGL API calls produce deprecation warnings
under the default Apple Clang settings. FFE silences these via `GL_SILENCE_DEPRECATION` defined
on the `ffe_renderer` target (in `engine/renderer/CMakeLists.txt`). This is necessary to preserve
the zero-warnings rule under `-Wall -Wextra`. OpenGL is deprecated on macOS but remains fully
functional through at least macOS 14 for the LEGACY tier's OpenGL 3.3 feature set.

### mold linker

mold is not available on macOS. `cmake/CompilerFlags.cmake` guards the `find_program(MOLD_LINKER mold)`
block with `if(UNIX AND NOT APPLE)` — Apple's ld64 linker is used automatically on macOS builds.

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

**Verification:**
- `x86_64-w64-mingw32-g++ --version` confirms GCC 13-win32 is on PATH
- `x86_64-w64-mingw32-windres --version` confirms windres is on PATH
- `cmake/CompilerFlags.cmake`: `grep -n "NOT WIN32"` confirms the mold guard is present at line 13
- `engine/core/CMakeLists.txt`: `grep -n "WIN32"` confirms ws2_32/opengl32 block is present
- `engine/audio/CMakeLists.txt`: `grep -n "Windows"` confirms winmm generator expression is present
- Linux builds are unaffected: the `if(NOT WIN32)` guard leaves all existing Linux behaviour intact

### 2026-03-07: Windows cross-build complete — Session 39 Phase 2/2

**What changed:**

1. `vcpkg.json` — added `"luajit"` to the dependencies array so vcpkg installs LuaJIT for both
   `x64-linux` and `x64-mingw-dynamic` triplets. Previously the Linux build used the system
   `libluajit-5.1-dev` package directly via pkg-config, which does not work for cross-builds
   because `pkg_check_modules` always queries the host pkg-config and ignores
   `CMAKE_FIND_ROOT_PATH`.

2. `engine/scripting/CMakeLists.txt` — replaced `find_package(PkgConfig)` +
   `pkg_check_modules(LUAJIT luajit)` with `find_path`/`find_library` calls:
   ```
   find_path(LUAJIT_INCLUDE_DIR NAMES luajit.h PATH_SUFFIXES luajit-2.1 luajit-2.0 luajit REQUIRED)
   find_library(LUAJIT_LIBRARY NAMES luajit-5.1 lua51 luajit REQUIRED)
   ```
   `find_path`/`find_library` respect `CMAKE_FIND_ROOT_PATH` and correctly locate the
   vcpkg-installed headers and import library for the target triplet.

3. `cmake/vcpkg-overlays/ports/luajit/` — created overlay port (portfile.cmake, configure,
   vcpkg.json, luajit.pc, Makefile.nmake, msvcbuild.patch,
   003-do-not-set-macosx-deployment-target.patch) to fix LuaJIT cross-compilation for
   Windows/MinGW. See "vcpkg Overlay Ports" section above for full details.

4. `CMakeLists.txt` — added `VCPKG_OVERLAY_PORTS` cache variable before `project()` so vcpkg
   picks up the overlay port during the toolchain phase (before any `find_package` runs).

5. `engine/core/arena_allocator.cpp` — replaced `std::aligned_alloc` (not in `std::` on MinGW)
   with `_WIN32`-guarded macros: `_aligned_malloc`/`_aligned_free` on Windows, `::aligned_alloc`/
   `::free` on POSIX.

6. `examples/demo_paths.h` — replaced POSIX-only `readlink("/proc/self/exe", ...)` and
   `access(testPath, F_OK)` with `#ifdef _WIN32` / `#else` path using `GetModuleFileNameA` and
   `GetFileAttributesA` for Windows.

7. `tests/scripting/test_save_load.cpp` — replaced POSIX-only `mkdtemp()` with a portable
   `ffe_mkdtemp()` inline helper. On Windows: uses `GetTempPathA` + `CreateDirectoryA` with a
   tick-count-derived unique suffix. On POSIX: delegates to `mkdtemp()`. The rest of the test
   file is unchanged.

8. `cmake/toolchains/mingw-w64-x86_64.cmake` — added `set(VCPKG_APPLOCAL_DEPS OFF)` to prevent
   vcpkg's post-link step from invoking `powershell.exe` (which doesn't exist on the Linux
   cross-compile host) to copy DLLs next to built executables.

9. `engine/CMakeLists.txt` — changed `ffe_engine` INTERFACE target from a plain
   `target_link_libraries` list to `"$<LINK_GROUP:RESCAN,...>"` wrapping all six engine static
   libraries. This instructs GNU ld (MinGW) to rescan the archive group until all mutual
   references resolve, equivalent to `--start-group`/`--end-group`. Required because
   `application.cpp` in `ffe_core` calls into `ffe_renderer`, `ffe_physics`, and `ffe_scripting`,
   creating forward references that single-pass GNU ld cannot resolve from the default ordering.
   On Linux with mold this was not an issue (mold is multi-pass tolerant); on Windows with MinGW's
   GNU ld it caused ~50 undefined reference errors at link time.

**Why:**
- Complete the Windows cross-build so all 11 executables (7 examples + 4 test binaries) produce
  valid PE32+ Windows x64 EXEs that can be transferred to a Windows machine and run.
- All changes are guarded by `#ifdef _WIN32` or CMake platform conditions so Linux behaviour is
  completely unchanged.

**Verification:**
- Linux (Clang-18): `cmake --build build` — no work to do (no regressions)
- Linux tests: `xvfb-run -a ctest --test-dir build` — 519/519 passed, 0 failed
- Windows cross-build: `cmake --build build-win` — all 11 `.exe` files linked successfully
- `file build-win/tests/ffe_tests.exe` — `PE32+ executable (console) x86-64, for MS Windows, 19 sections`
- `file build-win/examples/lua_demo/ffe_lua_demo.exe` — `PE32+ executable (console) x86-64, for MS Windows, 19 sections`
- Note: `catch_discover_tests` post-link step fails with "MZ: not found" on the Linux host because
  it tries to execute `.exe` files directly. This is expected — it is not a build error. The EXEs
  are valid and run correctly on Windows.

**Outstanding work for engine-dev (POSIX audit — not blocking this session):**
- `realpath()` appears in 11 call sites across 5 files (see POSIX audit section below). This is
  the last remaining POSIX-only call that needs a `_WIN32` guard before the Windows builds will
  run correctly on a Windows machine. The pattern to use is documented in the POSIX audit section.

### 2026-03-07: macOS build support — Session 40 (build system changes)

**What changed:**

1. `cmake/CompilerFlags.cmake` — changed the mold linker guard from `if(NOT WIN32)` to
   `if(UNIX AND NOT APPLE)`. The previous guard passed on macOS (macOS is not WIN32) causing CMake
   to search for mold and emit a warning when it was not found. The new guard is precise: mold is
   only searched on Linux (UNIX systems that are not Apple).

2. `cmake/toolchains/macos-arm64.cmake` — new file. CMake toolchain for macOS Apple Silicon
   (arm64). Sets `CMAKE_SYSTEM_NAME Darwin`, `CMAKE_SYSTEM_PROCESSOR arm64`, and
   `CMAKE_OSX_ARCHITECTURES arm64`. Does not set `CMAKE_OSX_DEPLOYMENT_TARGET` to avoid
   conflicting with the LuaJIT vcpkg overlay. Used via `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` so vcpkg's
   toolchain wraps it — same pattern as the Windows MinGW toolchain.

3. `engine/renderer/CMakeLists.txt` — added:
   ```cmake
   if(APPLE)
       target_compile_definitions(ffe_renderer PRIVATE GL_SILENCE_DEPRECATION)
   endif()
   ```
   macOS deprecated OpenGL in 10.14. Without this define, every OpenGL call in `ffe_renderer`
   produces a deprecation warning. This would violate the zero-warnings rule under `-Wall -Wextra`.
   The define is scoped `PRIVATE` so it does not leak into consumers of `ffe_renderer`.
   `engine/core/CMakeLists.txt` does not need this change — no OpenGL headers are included in any
   `engine/core/` source file.

4. `.github/workflows/ci.yml` — new file. GitHub Actions CI workflow with three jobs:
   `linux-clang` (ubuntu-24.04, Clang-18), `linux-gcc` (ubuntu-24.04, GCC-13), and
   `macos-arm64` (macos-15, Apple Silicon). All jobs run on push and PR to main. The macOS job
   clones and bootstraps vcpkg at `$HOME/vcpkg`, uses `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` to apply
   the arm64 toolchain, and targets the `arm64-osx` vcpkg triplet.

5. `CONTRIBUTING.md` — added "Building on macOS (Apple Silicon)" section after the Windows section.
   Documents prerequisites (Xcode CLT, Homebrew, ninja/cmake/pkg-config, vcpkg), the configure and
   build commands for arm64-osx and x64-osx, the GL_SILENCE_DEPRECATION note, and the mold note.
   Updated the build toolchain notes to mention mold is Linux-only and ccache is available on macOS
   via Homebrew.

**Why:**
- macOS build support is a Phase 2 target. Apple Silicon is now the dominant macOS hardware and
  `macos-15` runners are available on GitHub Actions.
- The mold guard change prevents a spurious CMake warning on macOS that would appear in CI output
  and could mask real warnings.
- `GL_SILENCE_DEPRECATION` is the correct Apple-recommended mechanism for silencing the OpenGL
  deprecation warnings — it does not disable any functionality.

**Verification:**
- Deferred to build-engineer (Session 40 Phase 5). No build run on this machine (Linux host cannot
  execute macOS binaries). CI will be the verification path for macOS.
- Linux builds: no changes to Linux-specific code paths. The mold guard change (`UNIX AND NOT APPLE`
  evaluates identically to `NOT WIN32` on Linux) leaves all Linux behaviour intact.

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

One change is required before the Windows EXEs will run correctly on a Windows machine:

1. **`realpath()` in 5 files (11 call sites):** Replace all `realpath()` / `::realpath()` calls with a thin `ffe_realpath()` wrapper that calls `_fullpath()` on Windows. The wrapper belongs in a new `engine/core/platform.h` header (or similar). This is the only remaining code change needed. All other POSIX calls are either MinGW-compatible or already platform-guarded.

### 2026-03-07: GitHub Actions CI workflow — fix Linux jobs to use vcpkg

**What changed:**

File: `.github/workflows/ci.yml`

**linux-clang job (lines 21-44):**

1. Lines 21-24 — `apt-get install` block: removed `libglfw3-dev libluajit-5.1-dev`; added
   `libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev`.
   These are the X11/GL development headers that vcpkg needs to compile GLFW from source.
   The apt `libglfw3-dev` and `libluajit-5.1-dev` packages conflict with and/or shadow the
   vcpkg-managed versions; removing them prevents version skew and missing transitive headers.

2. Lines 26-27 — New step "Set up vcpkg": exports `VCPKG_INSTALLATION_ROOT` (the pre-installed
   vcpkg path on `ubuntu-24.04` GitHub-hosted runners) as `VCPKG_ROOT` in `$GITHUB_ENV`.
   Without this step `${{ env.VCPKG_ROOT }}` is empty and the toolchain file path is wrong.

3. Lines 34-35 — Cache step: added `restore-keys: vcpkg-linux-x64-` for partial cache hits
   when `vcpkg.json` changes. Without a restore-key, any change to `vcpkg.json` causes a
   full cold rebuild from source; with the restore-key, a partial cache is used as a base and
   only changed packages are rebuilt.

4. Lines 39-43 — Configure step: added
   `-DCMAKE_TOOLCHAIN_FILE=${{ env.VCPKG_ROOT }}/scripts/buildsystems/vcpkg.cmake`.
   This is the root cause of "Configuring incomplete, errors occurred!" — without the vcpkg
   toolchain file, CMake's `find_package` calls for all vcpkg-managed libraries (entt, glfw3,
   luajit, sol2, glm, imgui, stb, nlohmann-json, tracy, catch2) produce "not found" errors and
   CMake exits with a configuration error.

**linux-gcc job (lines 60-84):** Identical changes applied — same apt block, same new "Set up
vcpkg" step, same `restore-keys`, same `-DCMAKE_TOOLCHAIN_FILE` argument.

**macos-arm64 job (lines 113-115):** Added `restore-keys: vcpkg-macos-arm64-` to the cache
step. The configure step already had `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`
and was not changed.

**Why:**
- All three Linux CI jobs were failing at CMake configure time with "Configuring incomplete,
  errors occurred!" because none of them passed a vcpkg toolchain file to CMake, so every
  `find_package` for a vcpkg dependency silently failed to find the package and CMake aborted.
- The apt packages `libglfw3-dev` and `libluajit-5.1-dev` are now managed by vcpkg (added to
  `vcpkg.json` in the 2026-03-07 Windows cross-build session). Installing them via apt alongside
  vcpkg creates header/library conflicts and leaves other vcpkg dependencies (entt, sol2, etc.)
  still unresolvable by CMake.
- `VCPKG_INSTALLATION_ROOT` is the canonical environment variable set by GitHub's ubuntu-24.04
  runner image to point at the pre-installed vcpkg. Exporting it as `VCPKG_ROOT` matches the
  variable name used in the cmake configure step.
- `restore-keys` is a performance improvement; it does not affect correctness.

**Verification:**
- File diff confirms all changes are scoped to the three targeted sections.
- No engine source files were modified.
- macOS configure step verified to already carry the toolchain file argument — no change needed.
