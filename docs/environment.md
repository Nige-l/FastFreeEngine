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
- xvfb, pkg-config, curl, wget, unzip

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

## vcpkg

- **Version:** vcpkg package management program version 2026-03-04-4b3e4c276b5b87a649e66341e11553e8c577459c
- **Location:** `$HOME/vcpkg`
- **Environment variables:** `VCPKG_ROOT` and `PATH` configured in `~/.bashrc`

## vcpkg Dependencies

The following libraries are declared in `vcpkg.json`:

| Library | Description |
|---------|-------------|
| entt | Entity-Component-System framework |
| joltphysics | Physics engine |
| sol2 | C++ binding for Lua/LuaJIT |
| glm | OpenGL Mathematics library |
| imgui | Immediate-mode GUI |
| stb | Single-header image loading and utilities |
| nlohmann-json | JSON for Modern C++ |
| tracy | Frame profiler |
| catch2 | Unit testing framework |
| vulkan-memory-allocator | Vulkan memory allocation library |
