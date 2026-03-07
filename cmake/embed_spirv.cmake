# cmake/embed_spirv.cmake
# Usage: cmake -P embed_spirv.cmake <input.spv> <output.h> <array_name>
#
# Reads a SPIR-V binary file and generates a C header with a static constexpr
# uint32_t array. The SPIR-V file must be a multiple of 4 bytes (32-bit words).
#
# Output format:
#   #pragma once
#   #include "core/types.h"
#   namespace ffe::rhi::vk::spv {
#   static constexpr ffe::u32 ARRAY_NAME[] = { 0x..., 0x..., ... };
#   static constexpr ffe::u32 ARRAY_NAME_SIZE = sizeof(ARRAY_NAME);
#   } // namespace ffe::rhi::vk::spv

# Parse arguments from CMAKE_ARGV (cmake -P script.cmake arg1 arg2 arg3)
set(INPUT_FILE  "${CMAKE_ARGV3}")
set(OUTPUT_FILE "${CMAKE_ARGV4}")
set(ARRAY_NAME  "${CMAKE_ARGV5}")

if(NOT INPUT_FILE OR NOT OUTPUT_FILE OR NOT ARRAY_NAME)
    message(FATAL_ERROR "Usage: cmake -P embed_spirv.cmake <input.spv> <output.h> <array_name>")
endif()

# Read binary file as hex
file(READ "${INPUT_FILE}" SPV_HEX HEX)

# Get file size
file(SIZE "${INPUT_FILE}" FILE_SIZE)

# Verify multiple of 4 bytes
math(EXPR REMAINDER "${FILE_SIZE} % 4")
if(NOT REMAINDER EQUAL 0)
    message(FATAL_ERROR "SPIR-V file ${INPUT_FILE} is not a multiple of 4 bytes (${FILE_SIZE} bytes)")
endif()

# Calculate number of 32-bit words
math(EXPR WORD_COUNT "${FILE_SIZE} / 4")

# Build the array contents by converting pairs of hex bytes to uint32 words.
# SPIR-V is little-endian, so bytes AABBCCDD in file become 0xDDCCBBAA.
set(WORDS "")
set(LINE_WORDS 0)
math(EXPR LAST_WORD "${WORD_COUNT} - 1")

foreach(I RANGE 0 ${LAST_WORD})
    # Each word is 4 bytes = 8 hex characters
    math(EXPR OFFSET "${I} * 8")

    # Extract 8 hex chars (4 bytes in file order)
    string(SUBSTRING "${SPV_HEX}" ${OFFSET} 8 HEX_WORD)

    # Bytes in file are little-endian. The hex string from file(READ ... HEX)
    # gives bytes in file order: B0 B1 B2 B3 -> "b0b1b2b3"
    # We need to swap to get the uint32 value: 0xB3B2B1B0
    string(SUBSTRING "${HEX_WORD}" 0 2 B0)
    string(SUBSTRING "${HEX_WORD}" 2 2 B1)
    string(SUBSTRING "${HEX_WORD}" 4 2 B2)
    string(SUBSTRING "${HEX_WORD}" 6 2 B3)

    set(SWAPPED "0x${B3}${B2}${B1}${B0}")

    if(I EQUAL 0)
        set(WORDS "    ${SWAPPED}")
    elseif(LINE_WORDS EQUAL 0)
        string(APPEND WORDS ",\n    ${SWAPPED}")
    else()
        string(APPEND WORDS ", ${SWAPPED}")
    endif()

    math(EXPR LINE_WORDS "(${LINE_WORDS} + 1) % 8")
endforeach()

# Generate the header
set(HEADER_CONTENT "#pragma once

// Auto-generated from SPIR-V binary. Do not edit.
// Source: ${INPUT_FILE}

#include \"core/types.h\"

namespace ffe::rhi::vk::spv {

static constexpr ffe::u32 ${ARRAY_NAME}[] = {
${WORDS},
};

static constexpr ffe::u32 ${ARRAY_NAME}_SIZE = sizeof(${ARRAY_NAME});

} // namespace ffe::rhi::vk::spv
")

file(WRITE "${OUTPUT_FILE}" "${HEADER_CONTENT}")
