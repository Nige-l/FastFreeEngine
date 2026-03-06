add_subdirectory(core)
add_subdirectory(renderer)
add_subdirectory(audio)
add_subdirectory(physics)
add_subdirectory(scripting)
add_subdirectory(editor)

# Umbrella target that links all engine modules
add_library(ffe_engine INTERFACE)
target_link_libraries(ffe_engine INTERFACE
    ffe_core
    ffe_renderer
    ffe_audio
    # ffe_physics    — added when physics is implemented
    ffe_scripting
    # ffe_editor     — added when editor is implemented
)
