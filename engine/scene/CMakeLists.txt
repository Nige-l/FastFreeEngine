add_library(ffe_scene STATIC
    scene_serialiser.cpp
    scene_graph.cpp
)

target_include_directories(ffe_scene
    PUBLIC ${CMAKE_SOURCE_DIR}/engine
)

target_link_libraries(ffe_scene
    PUBLIC
        ffe_core
    PRIVATE
        ffe_renderer
        nlohmann_json::nlohmann_json
)
