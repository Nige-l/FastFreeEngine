# engine/scripting — Lua scripting subsystem
#
# Dependencies (new in Session 5 — flagged per CLAUDE.md Section 4):
#   - sol2 (already in vcpkg.json, header-only binding layer)
#   - LuaJIT (vcpkg: luajit — found via find_path/find_library for cross-build portability)
#
# LuaJIT is used as the Lua backend (ADR-004 Decision 2).
# sol2 auto-detects LuaJIT when LUAJIT_VERSION is defined via luajit headers.
#
# LuaJIT discovery uses find_path + find_library instead of pkg_check_modules so that
# CMAKE_FIND_ROOT_PATH correctly redirects to the vcpkg-installed Windows-target libraries
# during MinGW cross-compilation. pkg_check_modules always queries the host pkg-config
# binary, which finds Linux system libraries regardless of CMAKE_FIND_ROOT_PATH.

find_path(LUAJIT_INCLUDE_DIR
    NAMES luajit.h
    PATH_SUFFIXES luajit-2.1 luajit-2.0 luajit
    REQUIRED
)

# On Linux (native) vcpkg installs luajit as libluajit-5.1.
# On Windows/MinGW vcpkg installs it as lua51 (matching the .pc libname=lua${abiver}).
find_library(LUAJIT_LIBRARY
    NAMES luajit-5.1 lua51 luajit
    REQUIRED
)

if(NOT LUAJIT_INCLUDE_DIR)
    message(FATAL_ERROR "LuaJIT headers not found. Ensure luajit is in vcpkg.json and vcpkg install has run.")
endif()
if(NOT LUAJIT_LIBRARY)
    message(FATAL_ERROR "LuaJIT library not found. Ensure luajit is in vcpkg.json and vcpkg install has run.")
endif()

message(STATUS "Found LuaJIT: ${LUAJIT_LIBRARY} (include: ${LUAJIT_INCLUDE_DIR})")

add_library(ffe_scripting STATIC
    script_engine.cpp
)

target_include_directories(ffe_scripting
    PUBLIC
        ${CMAKE_SOURCE_DIR}/engine   # so includes are: #include "scripting/script_engine.h"
    PRIVATE
        ${LUAJIT_INCLUDE_DIR}        # lua.h, luajit.h — private: only the .cpp sees them
)

target_link_libraries(ffe_scripting
    PUBLIC
        ffe_core                     # logging, types, ECS, input
        ffe_renderer                 # Transform and Sprite component types
        ffe_audio                    # audio music API (playMusic, stopMusic, etc.)
        ffe_physics                  # Collider2D, CollisionEventList, CollisionCallbackRef
        ffe_scene                    # scene serialiser for ffe.loadSceneJSON binding
        ffe_networking               # networking bindings (startServer, connect, etc.)
    PRIVATE
        sol2::sol2                   # header-only sol2 — private, not exposed to users
        ${LUAJIT_LIBRARY}            # -lluajit-5.1 (Linux) or -llua51 (Windows/MinGW)
)

# sol2 requires C++17 minimum; we use C++20 throughout.
target_compile_features(ffe_scripting PUBLIC cxx_std_20)

# Safety defines for sol2
target_compile_definitions(ffe_scripting
    PRIVATE
        SOL_ALL_SAFETIES_ON=1
)

# sol2 with LuaJIT generates some warnings we cannot fix (their code, not ours).
# Only suppress what is unavoidable; -Wno-unused-parameter is needed because
# sol2 internal callbacks declare parameters they don't always use.
# All other -Wall -Wextra warnings remain active.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ffe_scripting PRIVATE -Wno-unused-parameter)
endif()
