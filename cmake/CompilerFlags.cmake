# Compiler warning flags — non-negotiable
add_compile_options(-Wall -Wextra -Wpedantic)

# No RTTI, no exceptions in engine core (C++ only)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti> $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)

# Linker: use mold if available
find_program(MOLD_LINKER mold)
if(MOLD_LINKER)
    add_link_options(-fuse-ld=mold)
    message(STATUS "Using mold linker: ${MOLD_LINKER}")
else()
    message(WARNING "mold linker not found — falling back to default linker")
endif()

# ccache: detected automatically by CMake if CMAKE_<LANG>_COMPILER_LAUNCHER is set,
# but we set it explicitly for clarity.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "CXX compiler launcher")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()

# Per-config flags
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DFFE_DEBUG=1")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -DFFE_RELEASE=1")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG -DFFE_RELWITHDEBINFO=1")

# Tracy is enabled only in Debug and RelWithDebInfo
# In Release, TRACY_ENABLE is not defined, so all Tracy macros become no-ops
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(TRACY_ENABLE)
endif()
