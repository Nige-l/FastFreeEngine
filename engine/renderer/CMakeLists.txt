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
)

# Sources that currently use OpenGL directly (GL calls, glad includes).
# These are compiled for the OpenGL backend only. The Vulkan backend stubs
# their functionality via the RHI layer — M2+ will add Vulkan implementations.
set(RENDERER_GL_DEPENDENT_SOURCES
    texture_loader.cpp
    mesh_loader.cpp
    mesh_renderer.cpp
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
else()
    target_link_libraries(ffe_renderer PRIVATE glad)
    target_compile_definitions(ffe_renderer PUBLIC FFE_BACKEND_OPENGL=1)
endif()

# macOS deprecated OpenGL in 10.14. Silence warnings so -Wall -Wextra
# do not violate the zero-warnings rule.
if(APPLE)
    target_compile_definitions(ffe_renderer PRIVATE GL_SILENCE_DEPRECATION)
endif()
