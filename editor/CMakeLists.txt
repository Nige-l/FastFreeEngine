# editor/CMakeLists.txt
# Standalone editor application (ffe_editor_app)
# Separate from the in-game debug overlay (engine/editor/ffe_editor).

add_executable(ffe_editor_app
    main.cpp
    editor_app.cpp
    play_mode.cpp
    commands/command_history.cpp
    commands/entity_commands.cpp
    panels/file_dialog.cpp
    panels/viewport_panel.cpp
    rendering/editor_framebuffer.cpp
)

target_include_directories(ffe_editor_app PRIVATE
    ${CMAKE_SOURCE_DIR}/editor
    ${CMAKE_SOURCE_DIR}/engine
)

find_package(glfw3 3.3 REQUIRED)

target_link_libraries(ffe_editor_app PRIVATE
    ffe_engine
    imgui::imgui
    glad
    glfw
)

target_compile_definitions(ffe_editor_app PRIVATE FFE_EDITOR_APP=1)
