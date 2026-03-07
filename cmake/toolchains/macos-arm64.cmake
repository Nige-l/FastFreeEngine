# macOS Apple Silicon (arm64) toolchain for FastFreeEngine
# Usage:
#   cmake -B build-macos -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/macos-arm64.cmake \
#     -DVCPKG_TARGET_TRIPLET=arm64-osx \
#     -DFFE_TIER=LEGACY \
#     -DCMAKE_BUILD_TYPE=Debug
#
# Prerequisites: Xcode Command Line Tools, Homebrew, ninja, vcpkg
# GLFW is provided by vcpkg (arm64-osx triplet) — do not brew install glfw.
# mold linker is not used on macOS — Apple ld64 is used automatically.

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)
# Do not set CMAKE_OSX_DEPLOYMENT_TARGET here — the LuaJIT vcpkg overlay
# strips it from the LuaJIT Makefile and setting it here conflicts.
