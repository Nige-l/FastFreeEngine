add_library(ffe_core STATIC
    application.cpp
    arena_allocator.cpp
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
)
