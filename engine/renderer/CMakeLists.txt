# engine/renderer/CMakeLists.txt

# GLFW — system package (libglfw3-dev)
find_package(glfw3 3.3 REQUIRED)

set(RENDERER_SOURCES
    render_queue.cpp
    camera.cpp
    sprite_batch.cpp
    render_system.cpp
    shader_library.cpp
    texture_loader.cpp
    text_renderer.cpp
    font.cpp
    mesh_loader.cpp
    mesh_renderer.cpp
)

# Backend selection based on tier
if(FFE_TIER STREQUAL "MODERN")
    message(FATAL_ERROR "Vulkan backend not yet implemented")
else()
    # RETRO, LEGACY, STANDARD all use OpenGL (different versions)
    list(APPEND RENDERER_SOURCES
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
        glad
        glfw
        Tracy::TracyClient
)
