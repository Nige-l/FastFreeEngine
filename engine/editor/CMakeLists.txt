# engine/editor/CMakeLists.txt
# Dear ImGui integration for the editor overlay (Debug builds only via FFE_EDITOR)

add_library(ffe_editor STATIC
    editor.cpp
)

target_include_directories(ffe_editor PUBLIC ${CMAKE_SOURCE_DIR}/engine)

target_link_libraries(ffe_editor PUBLIC
    imgui::imgui
    ffe_core
    ffe_renderer
    ffe_audio
)

target_compile_definitions(ffe_editor PUBLIC
    $<$<CONFIG:Debug>:FFE_EDITOR>
)
