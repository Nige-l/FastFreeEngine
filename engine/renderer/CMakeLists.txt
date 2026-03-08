# engine/renderer/CMakeLists.txt

# GLFW — system package (libglfw3-dev)
find_package(glfw3 3.3 REQUIRED)

# Backend-agnostic renderer sources (no direct GL calls)
set(RENDERER_SOURCES
    render_queue.cpp
    camera.cpp
    sprite_batch.cpp
    texture_atlas.cpp
    render_system.cpp
    shader_library.cpp
    text_renderer.cpp
    font.cpp
    animation_system.cpp
    frustum.cpp
)

# Sources that currently use OpenGL directly (GL calls, glad includes).
# These are compiled for the OpenGL backend only. The Vulkan backend stubs
# their functionality via the RHI layer — M2+ will add Vulkan implementations.
set(RENDERER_GL_DEPENDENT_SOURCES
    texture_loader.cpp
    mesh_loader.cpp
    mesh_renderer.cpp
    terrain.cpp
    terrain_renderer.cpp
    water.cpp
    vegetation.cpp
    shadow_map.cpp
    screenshot.cpp
    post_process.cpp
    ssao.cpp
)

# Backend selection: FFE_BACKEND controls which RHI implementation is compiled.
# Default: OPENGL. Set -DFFE_BACKEND=VULKAN for Vulkan builds.
if(FFE_BACKEND STREQUAL "VULKAN")
    list(APPEND RENDERER_SOURCES
        vulkan/rhi_vulkan.cpp
        vulkan/vk_init.cpp
        vulkan/vk_buffer.cpp
        vulkan/vk_shader.cpp
        vulkan/vk_pipeline.cpp
        vulkan/vk_texture.cpp
        vulkan/vk_descriptor.cpp
        vulkan/vk_uniform.cpp
        vulkan/vk_resource_manager.cpp
    )
else()
    # OPENGL backend (default for RETRO, LEGACY, STANDARD)
    list(APPEND RENDERER_SOURCES
        ${RENDERER_GL_DEPENDENT_SOURCES}
        opengl/rhi_opengl.cpp
        opengl/gl_debug.cpp
    )
endif()

add_library(ffe_renderer STATIC ${RENDERER_SOURCES})

target_include_directories(ffe_renderer
    PUBLIC ${CMAKE_SOURCE_DIR}/engine
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(ffe_renderer
    PUBLIC
        ffe_core
        glm::glm
    PRIVATE
        glfw
        Tracy::TracyClient
)

# Backend-specific link dependencies and compile definitions
if(FFE_BACKEND STREQUAL "VULKAN")
    find_package(volk CONFIG REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    target_link_libraries(ffe_renderer PRIVATE volk::volk GPUOpen::VulkanMemoryAllocator)
    target_compile_definitions(ffe_renderer PUBLIC FFE_BACKEND_VULKAN=1)

    # ================================================================
    # Build-time SPIR-V compilation (M5)
    # ================================================================
    # Find glslc (Vulkan SDK) or glslangValidator as SPIR-V compiler.
    # If neither is found, fall back to existing hand-assembled SPIR-V headers.
    find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin")
    find_program(GLSLANG glslangValidator HINTS "$ENV{VULKAN_SDK}/bin")

    set(FFE_SPIRV_COMPILER "")
    if(GLSLC)
        set(FFE_SPIRV_COMPILER "${GLSLC}")
        set(FFE_SPIRV_COMPILE_CMD_ARGS "")
    elseif(GLSLANG)
        set(FFE_SPIRV_COMPILER "${GLSLANG}")
        set(FFE_SPIRV_COMPILE_CMD_ARGS "-V" "--target-env" "vulkan1.0")
    endif()

    set(FFE_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/shaders")
    set(FFE_SPIRV_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${FFE_SPIRV_OUT_DIR}")

    set(FFE_SHADER_SOURCES
        triangle.vert    triangle.frag
        textured.vert    textured.frag
        blinn_phong.vert blinn_phong.frag
    )

    # Map shader file names to C array names
    set(FFE_SPV_ARRAYNAME_triangle.vert     TRIANGLE_VERT_SPV)
    set(FFE_SPV_ARRAYNAME_triangle.frag     TRIANGLE_FRAG_SPV)
    set(FFE_SPV_ARRAYNAME_textured.vert     TEXTURED_VERT_SPV)
    set(FFE_SPV_ARRAYNAME_textured.frag     TEXTURED_FRAG_SPV)
    set(FFE_SPV_ARRAYNAME_blinn_phong.vert  BLINN_PHONG_VERT_SPV)
    set(FFE_SPV_ARRAYNAME_blinn_phong.frag  BLINN_PHONG_FRAG_SPV)

    if(FFE_SPIRV_COMPILER)
        message(STATUS "SPIR-V compiler found: ${FFE_SPIRV_COMPILER}")

        set(FFE_GENERATED_SPV_HEADERS "")

        foreach(SHADER ${FFE_SHADER_SOURCES})
            # Determine output file names
            string(REPLACE "." "_" SHADER_UNDERSCORE "${SHADER}")
            set(SPV_FILE    "${FFE_SPIRV_OUT_DIR}/${SHADER}.spv")
            set(SPV_HEADER  "${FFE_SPIRV_OUT_DIR}/${SHADER_UNDERSCORE}_spv.h")
            set(ARRAY_NAME  "${FFE_SPV_ARRAYNAME_${SHADER}}")

            # Compile GLSL -> SPIR-V
            if(GLSLC)
                add_custom_command(
                    OUTPUT "${SPV_FILE}"
                    COMMAND "${FFE_SPIRV_COMPILER}" "${FFE_SHADER_DIR}/${SHADER}"
                            -o "${SPV_FILE}"
                    DEPENDS "${FFE_SHADER_DIR}/${SHADER}"
                    COMMENT "Compiling SPIR-V: ${SHADER}"
                    VERBATIM
                )
            else()
                add_custom_command(
                    OUTPUT "${SPV_FILE}"
                    COMMAND "${FFE_SPIRV_COMPILER}" ${FFE_SPIRV_COMPILE_CMD_ARGS}
                            "${FFE_SHADER_DIR}/${SHADER}"
                            -o "${SPV_FILE}"
                    DEPENDS "${FFE_SHADER_DIR}/${SHADER}"
                    COMMENT "Compiling SPIR-V: ${SHADER}"
                    VERBATIM
                )
            endif()

            # Embed SPIR-V -> C header
            add_custom_command(
                OUTPUT "${SPV_HEADER}"
                COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/cmake/embed_spirv.cmake"
                        "${SPV_FILE}" "${SPV_HEADER}" "${ARRAY_NAME}"
                DEPENDS "${SPV_FILE}"
                COMMENT "Embedding SPIR-V header: ${SHADER_UNDERSCORE}_spv.h"
                VERBATIM
            )

            list(APPEND FFE_GENERATED_SPV_HEADERS "${SPV_HEADER}")
        endforeach()

        # Custom target that all generated headers depend on
        add_custom_target(ffe_spirv_shaders DEPENDS ${FFE_GENERATED_SPV_HEADERS})
        add_dependencies(ffe_renderer ffe_spirv_shaders)

        # Make the generated headers includable
        target_include_directories(ffe_renderer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        target_compile_definitions(ffe_renderer PRIVATE FFE_SPIRV_COMPILED=1)

        message(STATUS "SPIR-V shaders will be compiled at build time")
    else()
        message(WARNING "No SPIR-V compiler found (glslc/glslangValidator) "
                        "-- using pre-compiled SPIR-V headers as fallback")
    endif()

else()
    target_link_libraries(ffe_renderer PRIVATE glad)
    target_compile_definitions(ffe_renderer PUBLIC FFE_BACKEND_OPENGL=1)
endif()

# macOS deprecated OpenGL in 10.14. Silence warnings so -Wall -Wextra
# do not violate the zero-warnings rule.
if(APPLE)
    target_compile_definitions(ffe_renderer PRIVATE GL_SILENCE_DEPRECATION)
endif()
