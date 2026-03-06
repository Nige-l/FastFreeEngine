# engine/scripting — Lua scripting subsystem
#
# Dependencies (new in Session 5 — flagged per CLAUDE.md Section 4):
#   - sol2 (already in vcpkg.json, header-only binding layer)
#   - LuaJIT (system package: libluajit-5.1-dev, found via PkgConfig)
#
# LuaJIT is used as the Lua backend (ADR-004 Decision 2).
# sol2 auto-detects LuaJIT when LUAJIT_VERSION is defined via luajit headers.

find_package(PkgConfig REQUIRED)
pkg_check_modules(LUAJIT REQUIRED luajit)

add_library(ffe_scripting STATIC
    script_engine.cpp
)

target_include_directories(ffe_scripting
    PUBLIC
        ${CMAKE_SOURCE_DIR}/engine   # so includes are: #include "scripting/script_engine.h"
    PRIVATE
        ${LUAJIT_INCLUDE_DIRS}       # lua.h, luajit.h — private: only the .cpp sees them
)

target_link_libraries(ffe_scripting
    PUBLIC
        ffe_core                     # logging, types, ECS, input
        ffe_renderer                 # Transform and Sprite component types
        ffe_audio                    # audio music API (playMusic, stopMusic, etc.)
    PRIVATE
        sol2::sol2                   # header-only sol2 — private, not exposed to users
        ${LUAJIT_LIBRARIES}          # -lluajit-5.1
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
