# CopyExampleAssets.cmake — build-time copy for example Lua scripts and assets.
#
# Usage:
#   ffe_copy_example_lua(TARGET <target> FILES file1.lua file2.lua ...)
#   ffe_copy_example_dir(TARGET <target> SRC_DIR <dir> DST_DIR <dir>)
#
# ffe_copy_example_lua: copies individual Lua files next to the binary on every
# build, using copy_if_different so unchanged files are skipped.
#
# ffe_copy_example_dir: copies an entire directory tree on every build. Use for
# subdirectories (levels/, lib/, assets/) where new files may appear without
# re-running cmake configure.

# Copy individual files relative to CMAKE_CURRENT_SOURCE_DIR into
# CMAKE_CURRENT_BINARY_DIR, preserving relative paths.
function(ffe_copy_example_lua)
    cmake_parse_arguments(ARG "" "TARGET" "FILES" ${ARGN})
    if(NOT ARG_TARGET)
        message(FATAL_ERROR "ffe_copy_example_lua: TARGET is required")
    endif()
    foreach(FILE ${ARG_FILES})
        add_custom_command(
            TARGET ${ARG_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${FILE}"
                "${CMAKE_CURRENT_BINARY_DIR}/${FILE}"
            COMMENT "Copy ${FILE}"
        )
    endforeach()
endfunction()

# Copy an entire directory tree on every build. Handles new files without
# needing a cmake reconfigure.
function(ffe_copy_example_dir)
    cmake_parse_arguments(ARG "" "TARGET;SRC_DIR;DST_DIR" "" ${ARGN})
    if(NOT ARG_TARGET OR NOT ARG_SRC_DIR OR NOT ARG_DST_DIR)
        message(FATAL_ERROR "ffe_copy_example_dir: TARGET, SRC_DIR, DST_DIR are required")
    endif()
    add_custom_command(
        TARGET ${ARG_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ARG_SRC_DIR}"
            "${ARG_DST_DIR}"
        COMMENT "Copy directory ${ARG_SRC_DIR} -> ${ARG_DST_DIR}"
    )
endfunction()
