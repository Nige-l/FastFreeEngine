find_package(glfw3 3.3 REQUIRED)

add_library(ffe_core STATIC
    application.cpp
    arena_allocator.cpp
    input.cpp
    logging.cpp
)

target_include_directories(ffe_core
    PUBLIC ${CMAKE_SOURCE_DIR}/engine  # so includes are: #include "core/types.h"
)

target_link_libraries(ffe_core
    PUBLIC
        EnTT::EnTT
        glm::glm
        Tracy::TracyClient
    PRIVATE
        glad
        glfw
)

# Windows requires explicit link against Winsock (ws2_32) and OpenGL (opengl32).
# On Linux these are handled by the system linker and libGL from mesa.
if(WIN32)
    target_link_libraries(ffe_core PRIVATE ws2_32 opengl32)
endif()

# Enable test hooks when building tests
if(FFE_BUILD_TESTS)
    target_compile_definitions(ffe_core PUBLIC FFE_TEST)
endif()
