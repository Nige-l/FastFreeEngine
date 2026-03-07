cmake_minimum_required(VERSION 3.25)

# vcpkg toolchain — must be set before project()
# Users set VCPKG_ROOT env var or pass -DCMAKE_TOOLCHAIN_FILE
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
endif()

# vcpkg overlay ports — must be set before project() so vcpkg install picks them up.
# The luajit overlay fixes cross-compilation for Windows/MinGW by rebuilding buildvm
# with PE object support (the upstream vcpkg luajit host package only provides an
# ELF-mode buildvm that cannot generate Windows PE objects).
#
# IMPORTANT: The overlay completely shadows the upstream vcpkg luajit port for ALL
# platforms. It must ONLY be activated for MinGW cross-compilation — macOS and native
# Linux must use the upstream port. VCPKG_TARGET_TRIPLET is set via -D on the command
# line for cross-builds; if it contains "mingw", we need the overlay.
if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "mingw")
    set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vcpkg-overlays/ports"
        CACHE STRING "vcpkg overlay ports directory")
endif()

project(FastFreeEngine
    VERSION 0.1.0
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Hardware tier selection ---
set(FFE_TIER "LEGACY" CACHE STRING "Hardware tier: RETRO, LEGACY, STANDARD, MODERN")
set_property(CACHE FFE_TIER PROPERTY STRINGS RETRO LEGACY STANDARD MODERN)

include(cmake/CompilerFlags.cmake)
include(cmake/TierDefinitions.cmake)

# --- Build options (declared before subdirectories so all targets see them) ---
option(FFE_BUILD_EXAMPLES "Build example programs" ON)
option(FFE_BUILD_TESTS "Build unit tests" ON)
option(FFE_BUILD_EDITOR "Build the standalone editor application" ON)

# --- Find dependencies ---
find_package(EnTT CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
# stb, joltphysics, vulkan-memory-allocator found when needed

# --- Third-party libraries ---
add_subdirectory(third_party/glad)

# --- Engine library ---
add_subdirectory(engine)

# --- Examples ---
if(FFE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Standalone editor ---
if(FFE_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

# --- Tests ---
if(FFE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
