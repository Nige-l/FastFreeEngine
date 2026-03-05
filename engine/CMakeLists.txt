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
    # ffe_renderer   — added when renderer is implemented
    # ffe_audio      — added when audio is implemented
    # ffe_physics    — added when physics is implemented
    # ffe_scripting  — added when scripting is implemented
    # ffe_editor     — added when editor is implemented
)
