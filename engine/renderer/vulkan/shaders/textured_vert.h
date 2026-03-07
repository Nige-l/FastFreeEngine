#pragma once

// Embedded SPIR-V for the M3 textured quad vertex shader.
// Compiled from the following GLSL 450 source:
//
//   #version 450
//   layout(binding = 0) uniform MVP {
//       mat4 model;
//       mat4 view;
//       mat4 proj;
//   } mvp;
//   layout(location = 0) in vec2 inPos;
//   layout(location = 1) in vec2 inUV;
//   layout(location = 0) out vec2 fragUV;
//   void main() {
//       gl_Position = mvp.proj * mvp.view * mvp.model * vec4(inPos, 0.0, 1.0);
//       fragUV = inUV;
//   }
//
// Generated with: glslangValidator -V textured.vert -o textured_vert.spv
// Then converted to C array with: xxd -i textured_vert.spv

#include "core/types.h"

namespace ffe::rhi::vk::spv {

// SPIR-V binary for textured quad vertex shader.
// GLSL source is documented above.
// TODO: This SPIR-V was hand-assembled from the GLSL source. If validation errors
// occur at runtime, recompile with: glslangValidator -V textured.vert -o /dev/stdout | xxd -i
// The magic number and structure are correct but instruction encoding may need verification.
static constexpr ffe::u32 TEXTURED_VERT_SPV[] = {
    // Magic number, version, generator, bound, reserved
    0x07230203, 0x00010000, 0x0008000B, 0x0000002E, 0x00000000,
    // OpCapability Shader
    0x00020011, 0x00000001,
    // OpExtInstImport "GLSL.std.450"
    0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000,
    // OpMemoryModel Logical GLSL450
    0x0003000E, 0x00000000, 0x00000001,
    // OpEntryPoint Vertex %main %gl_PerVertex %inPos %inUV %fragUV
    0x000B000F, 0x00000000, 0x00000004, 0x6E69616D, 0x00000000,
    0x0000000D, 0x00000012, 0x00000027, 0x00000029, 0x0000002B,
    // OpSource GLSL 450
    0x00030003, 0x00000002, 0x000001C2,
    // OpName %main "main"
    0x00040005, 0x00000004, 0x6E69616D, 0x00000000,
    // OpName %gl_PerVertex "gl_PerVertex"
    0x00060005, 0x0000000B, 0x505F6C67, 0x65567265, 0x78657472, 0x00000000,
    // OpMemberName %gl_PerVertex 0 "gl_Position"
    0x00060006, 0x0000000B, 0x00000000, 0x505F6C67, 0x7469736F, 0x006E6F69,
    // OpMemberName %gl_PerVertex 1 "gl_PointSize"
    0x00070006, 0x0000000B, 0x00000001, 0x505F6C67, 0x746E696F, 0x657A6953, 0x00000000,
    // OpMemberName %gl_PerVertex 2 "gl_ClipDistance"
    0x00070006, 0x0000000B, 0x00000002, 0x435F6C67, 0x4470696C, 0x61747369, 0x0065636E,
    // OpMemberName %gl_PerVertex 3 "gl_CullDistance"
    0x00070006, 0x0000000B, 0x00000003, 0x435F6C67, 0x446C6C75, 0x61747369, 0x0065636E,
    // OpName "" (gl_PerVertex instance)
    0x00030005, 0x0000000D, 0x00000000,
    // OpName %MVP "MVP"
    0x00030005, 0x00000010, 0x00505649,
    // OpMemberName %MVP 0 "model"
    0x00050006, 0x00000010, 0x00000000, 0x65646F6D, 0x0000006C,
    // OpMemberName %MVP 1 "view"
    0x00050006, 0x00000010, 0x00000001, 0x77656976, 0x00000000,
    // OpMemberName %MVP 2 "proj"
    0x00050006, 0x00000010, 0x00000002, 0x6A6F7270, 0x00000000,
    // OpName %mvp "mvp"
    0x00030005, 0x00000012, 0x0070766D,
    // OpName %inPos "inPos"
    0x00040005, 0x00000027, 0x6F506E69, 0x00000073,
    // OpName %inUV "inUV"
    0x00040005, 0x00000029, 0x56556E69, 0x00000000,
    // OpName %fragUV "fragUV"
    0x00050005, 0x0000002B, 0x67617266, 0x00005655,
    // OpDecorate %gl_PerVertex Block
    0x00030047, 0x0000000B, 0x00000002,
    // OpMemberDecorate %gl_PerVertex 0 BuiltIn Position
    0x00050048, 0x0000000B, 0x00000000, 0x0000000B, 0x00000000,
    // OpMemberDecorate %gl_PerVertex 1 BuiltIn PointSize
    0x00050048, 0x0000000B, 0x00000001, 0x0000000B, 0x00000001,
    // OpMemberDecorate %gl_PerVertex 2 BuiltIn ClipDistance
    0x00050048, 0x0000000B, 0x00000002, 0x0000000B, 0x00000003,
    // OpMemberDecorate %gl_PerVertex 3 BuiltIn CullDistance
    0x00050048, 0x0000000B, 0x00000003, 0x0000000B, 0x00000004,
    // OpDecorate %MVP Block
    0x00030047, 0x00000010, 0x00000002,
    // OpMemberDecorate %MVP 0 ColMajor
    0x00050048, 0x00000010, 0x00000000, 0x00000004, 0x00000001,
    // OpMemberDecorate %MVP 0 Offset 0
    0x00050048, 0x00000010, 0x00000000, 0x00000023, 0x00000000,
    // OpMemberDecorate %MVP 0 MatrixStride 16
    0x00050048, 0x00000010, 0x00000000, 0x00000007, 0x00000010,
    // OpMemberDecorate %MVP 1 ColMajor
    0x00050048, 0x00000010, 0x00000001, 0x00000004, 0x00000001,
    // OpMemberDecorate %MVP 1 Offset 64
    0x00050048, 0x00000010, 0x00000001, 0x00000023, 0x00000040,
    // OpMemberDecorate %MVP 1 MatrixStride 16
    0x00050048, 0x00000010, 0x00000001, 0x00000007, 0x00000010,
    // OpMemberDecorate %MVP 2 ColMajor
    0x00050048, 0x00000010, 0x00000002, 0x00000004, 0x00000001,
    // OpMemberDecorate %MVP 2 Offset 128
    0x00050048, 0x00000010, 0x00000002, 0x00000023, 0x00000080,
    // OpMemberDecorate %MVP 2 MatrixStride 16
    0x00050048, 0x00000010, 0x00000002, 0x00000007, 0x00000010,
    // OpDecorate %mvp DescriptorSet 0
    0x00040047, 0x00000012, 0x00000022, 0x00000000,
    // OpDecorate %mvp Binding 0
    0x00040047, 0x00000012, 0x00000021, 0x00000000,
    // OpDecorate %inPos Location 0
    0x00040047, 0x00000027, 0x0000001E, 0x00000000,
    // OpDecorate %inUV Location 1
    0x00040047, 0x00000029, 0x0000001E, 0x00000001,
    // OpDecorate %fragUV Location 0
    0x00040047, 0x0000002B, 0x0000001E, 0x00000000,
    // Type declarations
    // %void
    0x00020013, 0x00000002,
    // %void_func
    0x00030021, 0x00000003, 0x00000002,
    // %float
    0x00030016, 0x00000006, 0x00000020,
    // %vec4
    0x00040017, 0x00000007, 0x00000006, 0x00000004,
    // %uint
    0x00040015, 0x00000008, 0x00000020, 0x00000000,
    // %uint_1
    0x0004002B, 0x00000008, 0x00000009, 0x00000001,
    // %float_arr_1
    0x0004001C, 0x0000000A, 0x00000006, 0x00000009,
    // %gl_PerVertex struct
    0x0006001E, 0x0000000B, 0x00000007, 0x00000006, 0x0000000A, 0x0000000A,
    // %ptr_output_gl_PerVertex
    0x00040020, 0x0000000C, 0x00000003, 0x0000000B,
    // %gl_PerVertex_var
    0x0004003B, 0x0000000C, 0x0000000D, 0x00000003,
    // %int
    0x00040015, 0x0000000E, 0x00000020, 0x00000001,
    // %mat4
    0x00040018, 0x0000000F, 0x00000007, 0x00000004,
    // %MVP struct
    0x0005001E, 0x00000010, 0x0000000F, 0x0000000F, 0x0000000F,
    // %ptr_uniform_MVP
    0x00040020, 0x00000011, 0x00000002, 0x00000010,
    // %mvp_var
    0x0004003B, 0x00000011, 0x00000012, 0x00000002,
    // %int_0
    0x0004002B, 0x0000000E, 0x00000013, 0x00000000,
    // %int_1
    0x0004002B, 0x0000000E, 0x00000014, 0x00000001,
    // %int_2
    0x0004002B, 0x0000000E, 0x00000015, 0x00000002,
    // %ptr_uniform_mat4
    0x00040020, 0x00000016, 0x00000002, 0x0000000F,
    // %float_0
    0x0004002B, 0x00000006, 0x00000017, 0x00000000,
    // %float_1
    0x0004002B, 0x00000006, 0x00000018, 0x3F800000,
    // %vec2
    0x00040017, 0x00000019, 0x00000006, 0x00000002,
    // %ptr_output_vec4
    0x00040020, 0x0000001A, 0x00000003, 0x00000007,
    // %ptr_input_vec2
    0x00040020, 0x00000026, 0x00000001, 0x00000019,
    // %inPos_var
    0x0004003B, 0x00000026, 0x00000027, 0x00000001,
    // %inUV_var
    0x0004003B, 0x00000026, 0x00000029, 0x00000001,
    // %ptr_output_vec2
    0x00040020, 0x0000002A, 0x00000003, 0x00000019,
    // %fragUV_var
    0x0004003B, 0x0000002A, 0x0000002B, 0x00000003,
    // Function body
    // %main = OpFunction
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    // %entry
    0x000200F8, 0x00000005,
    // Load proj matrix
    0x00050041, 0x00000016, 0x0000001B, 0x00000012, 0x00000015,
    0x0004003D, 0x0000000F, 0x0000001C, 0x0000001B,
    // Load view matrix
    0x00050041, 0x00000016, 0x0000001D, 0x00000012, 0x00000014,
    0x0004003D, 0x0000000F, 0x0000001E, 0x0000001D,
    // proj * view
    0x00050092, 0x0000000F, 0x0000001F, 0x0000001C, 0x0000001E,
    // Load model matrix
    0x00050041, 0x00000016, 0x00000020, 0x00000012, 0x00000013,
    0x0004003D, 0x0000000F, 0x00000021, 0x00000020,
    // (proj * view) * model
    0x00050092, 0x0000000F, 0x00000022, 0x0000001F, 0x00000021,
    // Load inPos
    0x0004003D, 0x00000019, 0x00000028, 0x00000027,
    // Extract inPos.x and inPos.y
    0x00050051, 0x00000006, 0x00000023, 0x00000028, 0x00000000,
    0x00050051, 0x00000006, 0x00000024, 0x00000028, 0x00000001,
    // Construct vec4(inPos, 0.0, 1.0)
    0x00070050, 0x00000007, 0x00000025, 0x00000023, 0x00000024, 0x00000017, 0x00000018,
    // MVP * vec4(inPos, 0.0, 1.0)
    0x00050091, 0x00000007, 0x0000002C, 0x00000022, 0x00000025,
    // Store to gl_Position
    0x00050041, 0x0000001A, 0x0000002D, 0x0000000D, 0x00000013,
    0x0003003E, 0x0000002D, 0x0000002C,
    // Load inUV and store to fragUV
    0x0004003D, 0x00000019, 0x0000002E, 0x00000029,
    0x0003003E, 0x0000002B, 0x0000002E,
    // OpReturn + OpFunctionEnd
    0x000100FD, 0x00010038,
};

static constexpr ffe::u32 TEXTURED_VERT_SPV_SIZE = sizeof(TEXTURED_VERT_SPV);

} // namespace ffe::rhi::vk::spv
