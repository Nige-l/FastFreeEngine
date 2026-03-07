add_subdirectory(core)
add_subdirectory(renderer)
add_subdirectory(audio)
add_subdirectory(physics)
add_subdirectory(scripting)
add_subdirectory(editor)

# Umbrella target that links all engine modules.
#
# The engine static libraries have mutual dependencies — application.cpp in
# ffe_core calls into ffe_renderer, ffe_physics, and ffe_scripting.  GNU ld
# (Linux, Windows/MinGW) is single-pass and needs --start-group/--end-group
# (RESCAN) to resolve circular references.  Apple ld64 is inherently
# multi-pass, so RESCAN is unnecessary — and unsupported — on macOS.
add_library(ffe_engine INTERFACE)
if(APPLE)
    # Apple ld64 is multi-pass — no RESCAN needed, and it is not supported.
    target_link_libraries(ffe_engine INTERFACE
        ffe_core ffe_renderer ffe_audio ffe_physics ffe_scripting ffe_editor
    )
else()
    # GNU ld (Linux, Windows/MinGW) needs RESCAN for circular static lib deps.
    target_link_libraries(ffe_engine INTERFACE
        "$<LINK_GROUP:RESCAN,ffe_core,ffe_renderer,ffe_audio,ffe_physics,ffe_scripting,ffe_editor>"
    )
endif()
