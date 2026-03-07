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

# Locate Apple Clang via xcrun. This is the standard macOS mechanism for finding
# the active toolchain regardless of whether the full Xcode.app or just the
# Command Line Tools are installed. Without explicit compiler paths, CMake's
# compiler detection can fail when this toolchain is chainloaded through vcpkg
# (VCPKG_CHAINLOAD_TOOLCHAIN_FILE) on CI runners where the Xcode developer
# directory may not be pre-selected.
if(NOT CMAKE_C_COMPILER)
    execute_process(
        COMMAND xcrun --find clang
        OUTPUT_VARIABLE _macos_c_compiler
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _xcrun_c_result
    )
    if(_xcrun_c_result EQUAL 0 AND _macos_c_compiler)
        set(CMAKE_C_COMPILER "${_macos_c_compiler}" CACHE FILEPATH "C compiler")
    endif()
endif()

if(NOT CMAKE_CXX_COMPILER)
    execute_process(
        COMMAND xcrun --find clang++
        OUTPUT_VARIABLE _macos_cxx_compiler
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _xcrun_cxx_result
    )
    if(_xcrun_cxx_result EQUAL 0 AND _macos_cxx_compiler)
        set(CMAKE_CXX_COMPILER "${_macos_cxx_compiler}" CACHE FILEPATH "C++ compiler")
    endif()
endif()
