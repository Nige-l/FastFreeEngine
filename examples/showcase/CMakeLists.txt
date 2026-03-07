# Showcase game: "Echoes of the Ancients"
# Standalone executable — run directly: ./build/examples/showcase/ffe_showcase
#
# The C++ host creates the engine, sets asset/script roots, and loads game.lua.
# Assets and Lua scripts are copied to the build directory next to the binary.

# --- Executable target ---
add_executable(ffe_showcase main.cpp)
target_link_libraries(ffe_showcase PRIVATE ffe_engine)

# --- Collect Lua scripts and asset files ---
file(GLOB_RECURSE SHOWCASE_LUA_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.lua"
)
file(GLOB_RECURSE SHOWCASE_ASSET_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/*"
)

# --- Copy at configure time (initial population) ---
foreach(LUA_FILE ${SHOWCASE_LUA_FILES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${LUA_FILE}")
    configure_file(
        "${LUA_FILE}"
        "${CMAKE_CURRENT_BINARY_DIR}/${REL_PATH}"
        COPYONLY
    )
endforeach()

foreach(ASSET_FILE ${SHOWCASE_ASSET_FILES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${ASSET_FILE}")
    configure_file(
        "${ASSET_FILE}"
        "${CMAKE_CURRENT_BINARY_DIR}/${REL_PATH}"
        COPYONLY
    )
endforeach()

# --- Re-copy on changes at build time ---
foreach(LUA_FILE ${SHOWCASE_LUA_FILES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${LUA_FILE}")
    add_custom_command(
        TARGET ffe_showcase POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LUA_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${REL_PATH}"
        COMMENT "Copying ${REL_PATH}"
    )
endforeach()

foreach(ASSET_FILE ${SHOWCASE_ASSET_FILES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${ASSET_FILE}")
    add_custom_command(
        TARGET ffe_showcase POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSET_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${REL_PATH}"
        COMMENT "Copying asset ${REL_PATH}"
    )
endforeach()
