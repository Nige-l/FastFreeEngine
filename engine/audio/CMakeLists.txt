# engine/audio/CMakeLists.txt
#
# Audio subsystem — miniaudio v0.11.25 + stb_vorbis v1.22 (both embedded in third_party/).
# No new vcpkg dependencies. Headers are public-domain single-headers.
#
# Tiers: LEGACY (primary), STANDARD, MODERN. RETRO not supported.

add_library(ffe_audio STATIC
    audio.cpp
)

target_include_directories(ffe_audio
    PUBLIC  ${CMAKE_SOURCE_DIR}/engine         # #include "audio/audio.h"
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party    # miniaudio.h, stb_vorbis.c
)

target_link_libraries(ffe_audio
    PUBLIC
        ffe_core
    PRIVATE
        # miniaudio requires platform audio libraries on Linux
        # dl: miniaudio dlopen()s ALSA/PulseAudio/JACK dynamically
        # pthread: miniaudio uses pthreads for the audio callback thread
        # m: math functions used internally
        $<$<PLATFORM_ID:Linux>:dl>
        $<$<PLATFORM_ID:Linux>:pthread>
        $<$<PLATFORM_ID:Linux>:m>
)
