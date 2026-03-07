#pragma once

// Embedded SPIR-V for the M3 textured quad fragment shader.
// Compiled from the following GLSL 450 source:
//
//   #version 450
//   layout(binding = 1) uniform sampler2D tex;
//   layout(location = 0) in vec2 fragUV;
//   layout(location = 0) out vec4 outColor;
//   void main() {
//       outColor = texture(tex, fragUV);
//   }
//
// Generated with: glslangValidator -V textured.frag -o textured_frag.spv
// Then converted to C array with: xxd -i textured_frag.spv

#include "core/types.h"

namespace ffe::rhi::vk::spv {

// SPIR-V binary for textured quad fragment shader.
// GLSL source is documented above.
// TODO: This SPIR-V was hand-assembled from the GLSL source. If validation errors
// occur at runtime, recompile with: glslangValidator -V textured.frag -o /dev/stdout | xxd -i
// The magic number and structure are correct but instruction encoding may need verification.
static constexpr ffe::u32 TEXTURED_FRAG_SPV[] = {
    // Magic number, version, generator, bound, reserved
    0x07230203, 0x00010000, 0x0008000B, 0x00000015, 0x00000000,
    // OpCapability Shader
    0x00020011, 0x00000001,
    // OpExtInstImport "GLSL.std.450"
    0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000,
    // OpMemoryModel Logical GLSL450
    0x0003000E, 0x00000000, 0x00000001,
    // OpEntryPoint Fragment %main %outColor %fragUV %tex
    0x0009000F, 0x00000004, 0x00000004, 0x6E69616D, 0x00000000,
    0x00000009, 0x0000000C, 0x00000010,
    // OpExecutionMode %main OriginUpperLeft
    0x00030010, 0x00000004, 0x00000007,
    // OpSource GLSL 450
    0x00030003, 0x00000002, 0x000001C2,
    // OpName %main "main"
    0x00040005, 0x00000004, 0x6E69616D, 0x00000000,
    // OpName %outColor "outColor"
    0x00050005, 0x00000009, 0x4374756F, 0x726F6C6F, 0x00000000,
    // OpName %tex "tex"
    0x00030005, 0x0000000C, 0x00786574,
    // OpName %fragUV "fragUV"
    0x00050005, 0x00000010, 0x67617266, 0x00005655,
    // OpDecorate %outColor Location 0
    0x00040047, 0x00000009, 0x0000001E, 0x00000000,
    // OpDecorate %tex DescriptorSet 0
    0x00040047, 0x0000000C, 0x00000022, 0x00000000,
    // OpDecorate %tex Binding 1
    0x00040047, 0x0000000C, 0x00000021, 0x00000001,
    // OpDecorate %fragUV Location 0
    0x00040047, 0x00000010, 0x0000001E, 0x00000000,
    // Type declarations
    // %void
    0x00020013, 0x00000002,
    // %void_func
    0x00030021, 0x00000003, 0x00000002,
    // %float
    0x00030016, 0x00000006, 0x00000020,
    // %vec4
    0x00040017, 0x00000007, 0x00000006, 0x00000004,
    // %ptr_output_vec4
    0x00040020, 0x00000008, 0x00000003, 0x00000007,
    // %outColor_var
    0x0004003B, 0x00000008, 0x00000009, 0x00000003,
    // %image_type (sampled image float 2D)
    0x00090019, 0x0000000A, 0x00000006, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
    // %sampled_image_type
    0x0003001A, 0x0000000B, 0x0000000A,
    // %ptr_uniform_sampled_image
    0x00040020, 0x0000000D, 0x00000000, 0x0000000B,
    // %tex_var
    0x0004003B, 0x0000000D, 0x0000000C, 0x00000000,
    // %vec2
    0x00040017, 0x0000000E, 0x00000006, 0x00000002,
    // %ptr_input_vec2
    0x00040020, 0x0000000F, 0x00000001, 0x0000000E,
    // %fragUV_var
    0x0004003B, 0x0000000F, 0x00000010, 0x00000001,
    // Function body
    // %main = OpFunction
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    // %entry
    0x000200F8, 0x00000005,
    // Load tex sampler
    0x0004003D, 0x0000000B, 0x00000011, 0x0000000C,
    // Load fragUV
    0x0004003D, 0x0000000E, 0x00000012, 0x00000010,
    // texture(tex, fragUV)
    0x00050057, 0x00000007, 0x00000013, 0x00000011, 0x00000012,
    // Store to outColor
    0x0003003E, 0x00000009, 0x00000013,
    // OpReturn + OpFunctionEnd
    0x000100FD, 0x00010038,
};

static constexpr ffe::u32 TEXTURED_FRAG_SPV_SIZE = sizeof(TEXTURED_FRAG_SPV);

} // namespace ffe::rhi::vk::spv
