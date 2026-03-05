# Define FFE_TIER_<name> preprocessor macro based on the selected tier
# Exactly one of these will be defined at compile time.

if(FFE_TIER STREQUAL "RETRO")
    add_compile_definitions(FFE_TIER_RETRO=1 FFE_TIER_VALUE=0)
elseif(FFE_TIER STREQUAL "LEGACY")
    add_compile_definitions(FFE_TIER_LEGACY=1 FFE_TIER_VALUE=1)
elseif(FFE_TIER STREQUAL "STANDARD")
    add_compile_definitions(FFE_TIER_STANDARD=1 FFE_TIER_VALUE=2)
elseif(FFE_TIER STREQUAL "MODERN")
    add_compile_definitions(FFE_TIER_MODERN=1 FFE_TIER_VALUE=3)
else()
    message(FATAL_ERROR "Invalid FFE_TIER: ${FFE_TIER}. Must be RETRO, LEGACY, STANDARD, or MODERN.")
endif()

message(STATUS "FFE Hardware Tier: ${FFE_TIER}")
