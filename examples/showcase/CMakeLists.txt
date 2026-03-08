# Showcase game: "Echoes of the Ancients"
# Standalone executable — run directly: ./build/examples/showcase/ffe_showcase
#
# The C++ host creates the engine, sets asset/script roots, and loads game.lua.
# Assets and Lua scripts are copied to the build directory next to the binary.

# --- Executable target ---
add_executable(ffe_showcase main.cpp)
target_link_libraries(ffe_showcase PRIVATE ffe_engine)

# --- Copy Lua scripts and assets to build dir on every build ---
# Using copy_directory so new files are picked up without re-running cmake.
ffe_copy_example_dir(
    TARGET ffe_showcase
    SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    DST_DIR "${CMAKE_CURRENT_BINARY_DIR}/lib"
)
ffe_copy_example_dir(
    TARGET ffe_showcase
    SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/levels"
    DST_DIR "${CMAKE_CURRENT_BINARY_DIR}/levels"
)
ffe_copy_example_dir(
    TARGET ffe_showcase
    SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    DST_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets"
)
ffe_copy_example_lua(
    TARGET ffe_showcase
    FILES game.lua
)
