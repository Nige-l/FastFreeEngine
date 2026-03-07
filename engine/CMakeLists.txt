add_subdirectory(core)
add_subdirectory(renderer)
add_subdirectory(audio)
add_subdirectory(physics)
add_subdirectory(scripting)
add_subdirectory(editor)

# Umbrella target that links all engine modules.
#
# The engine static libraries have mutual dependencies — application.cpp in
# ffe_core calls into ffe_renderer, ffe_physics, and ffe_scripting.  On Linux
# the mold linker resolves these without ordering constraints.  On Windows with
# MinGW's GNU ld (single-pass), the archives must be wrapped in
# --start-group/--end-group so the linker rescans them until all references
# resolve.  $<LINK_GROUP:RESCAN,...> maps to those flags on GNU ld (both Linux
# and Windows/MinGW) and is a no-op on linkers that don't require it.
add_library(ffe_engine INTERFACE)
target_link_libraries(ffe_engine INTERFACE
    "$<LINK_GROUP:RESCAN,ffe_core,ffe_renderer,ffe_audio,ffe_physics,ffe_scripting,ffe_editor>"
)
