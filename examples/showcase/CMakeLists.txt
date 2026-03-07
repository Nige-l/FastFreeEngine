# Showcase game: "Echoes of the Ancients"
# Pure Lua project — no C++ target. Uses ffe_runtime to run.
# This CMakeLists copies Lua scripts to the build directory so the game
# can be launched from the build tree:
#   ./build/examples/showcase/game.lua  (via ffe_runtime)
#
# Usage:
#   ./build/examples/runtime/ffe_runtime build/examples/showcase/game.lua

# Collect all Lua files in the showcase tree.
file(GLOB_RECURSE SHOWCASE_LUA_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.lua"
)

# Copy each Lua file to the corresponding location in the build dir,
# preserving directory structure (lib/, levels/).
foreach(LUA_FILE ${SHOWCASE_LUA_FILES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${LUA_FILE}")
    configure_file(
        "${LUA_FILE}"
        "${CMAKE_CURRENT_BINARY_DIR}/${REL_PATH}"
        COPYONLY
    )
endforeach()

# Custom target so "make showcase" or "ninja showcase" triggers the copy.
add_custom_target(showcase ALL
    COMMENT "Copying showcase Lua scripts to build directory"
    DEPENDS ${SHOWCASE_LUA_FILES}
)

# Re-copy on changes (configure_file only runs at cmake time, so we add
# a build-time copy rule for each file).
foreach(LUA_FILE ${SHOWCASE_LUA_FILES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${LUA_FILE}")
    add_custom_command(
        TARGET showcase POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LUA_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${REL_PATH}"
        COMMENT "Copying ${REL_PATH}"
    )
endforeach()
