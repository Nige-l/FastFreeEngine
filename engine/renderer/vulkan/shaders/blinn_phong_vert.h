#pragma once

// Embedded SPIR-V for the Blinn-Phong mesh vertex shader.
// Compiled from the following GLSL 450 source:
//
//   #version 450
//
//   layout(set = 0, binding = 0) uniform SceneUBO {
//       mat4 model;
//       mat4 view;
//       mat4 projection;
//       mat4 normalMatrix;  // mat3 padded to mat4 for std140
//   } scene;
//
//   layout(location = 0) in vec3 inPosition;
//   layout(location = 1) in vec3 inNormal;
//   layout(location = 2) in vec2 inTexCoord;
//
//   layout(location = 0) out vec3 fragWorldPos;
//   layout(location = 1) out vec3 fragWorldNormal;
//   layout(location = 2) out vec2 fragTexCoord;
//
//   void main() {
//       vec4 worldPos = scene.model * vec4(inPosition, 1.0);
//       fragWorldPos = worldPos.xyz;
//       fragWorldNormal = mat3(scene.normalMatrix) * inNormal;
//       fragTexCoord = inTexCoord;
//       gl_Position = scene.projection * scene.view * worldPos;
//   }
//
// Generated with: glslangValidator -V blinn_phong.vert -o blinn_phong_vert.spv
// Then converted to C array with: xxd -i blinn_phong_vert.spv
//
// TODO(M4): Replace hand-assembled SPIR-V with build-time glslc compilation.
// For M4, we use the simpler textured+MVP shader as a placeholder pipeline,
// and the actual Blinn-Phong SPIR-V below. If validation errors occur,
// recompile with glslangValidator.

#include "core/types.h"

namespace ffe::rhi::vk::spv {

// SPIR-V binary for Blinn-Phong vertex shader.
// Layout: SceneUBO at set=0 binding=0 with model, view, projection, normalMatrix.
// Vertex inputs: position (loc 0), normal (loc 1), texcoord (loc 2).
// Outputs: world position (loc 0), world normal (loc 1), texcoord (loc 2).
static constexpr ffe::u32 BLINN_PHONG_VERT_SPV[] = {
    // Magic number, version 1.0, generator, bound, reserved
    0x07230203, 0x00010000, 0x0008000B, 0x00000040, 0x00000000,
    // OpCapability Shader
    0x00020011, 0x00000001,
    // OpExtInstImport "GLSL.std.450"
    0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000,
    // OpMemoryModel Logical GLSL450
    0x0003000E, 0x00000000, 0x00000001,
    // OpEntryPoint Vertex %main %gl_PerVertex %inPosition %inNormal %inTexCoord %fragWorldPos %fragWorldNormal %fragTexCoord
    0x000D000F, 0x00000000, 0x00000004, 0x6E69616D, 0x00000000,
    0x0000000D, 0x00000027, 0x0000002A, 0x0000002D,
    0x00000030, 0x00000033, 0x00000036,
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
    // OpName %SceneUBO "SceneUBO"
    0x00050005, 0x00000010, 0x6E656353, 0x4F425565, 0x00000000,
    // OpMemberName %SceneUBO 0 "model"
    0x00050006, 0x00000010, 0x00000000, 0x65646F6D, 0x0000006C,
    // OpMemberName %SceneUBO 1 "view"
    0x00050006, 0x00000010, 0x00000001, 0x77656976, 0x00000000,
    // OpMemberName %SceneUBO 2 "projection"
    0x00060006, 0x00000010, 0x00000002, 0x6A6F7270, 0x69746365, 0x00006E6F,
    // OpMemberName %SceneUBO 3 "normalMatrix"
    0x00060006, 0x00000010, 0x00000003, 0x6D726F6E, 0x614D6C61, 0x00007274,
    // OpName %scene "scene"
    0x00040005, 0x00000012, 0x6E656373, 0x00000065,
    // OpName %inPosition "inPosition"
    0x00050005, 0x00000027, 0x6F506E69, 0x69746973, 0x00006E6F,
    // OpName %inNormal "inNormal"
    0x00050005, 0x0000002A, 0x6F4E6E69, 0x6C616D72, 0x00000000,
    // OpName %inTexCoord "inTexCoord"
    0x00050005, 0x0000002D, 0x65546E69, 0x6F6F4378, 0x00006472,
    // OpName %fragWorldPos "fragWorldPos"
    0x00050005, 0x00000030, 0x67617266, 0x6C726F57, 0x00736F64,
    // OpName %fragWorldNormal "fragWorldNormal"
    0x00060005, 0x00000033, 0x67617266, 0x6C726F57, 0x726F4E64, 0x006C616D,
    // OpName %fragTexCoord "fragTexCoord"
    0x00050005, 0x00000036, 0x67617266, 0x43786554, 0x6472006F,

    // --- Decorations ---
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
    // OpDecorate %SceneUBO Block
    0x00030047, 0x00000010, 0x00000002,
    // OpMemberDecorate %SceneUBO 0 ColMajor + Offset 0 + MatrixStride 16
    0x00050048, 0x00000010, 0x00000000, 0x00000004, 0x00000001,
    0x00050048, 0x00000010, 0x00000000, 0x00000023, 0x00000000,
    0x00050048, 0x00000010, 0x00000000, 0x00000007, 0x00000010,
    // OpMemberDecorate %SceneUBO 1 ColMajor + Offset 64 + MatrixStride 16
    0x00050048, 0x00000010, 0x00000001, 0x00000004, 0x00000001,
    0x00050048, 0x00000010, 0x00000001, 0x00000023, 0x00000040,
    0x00050048, 0x00000010, 0x00000001, 0x00000007, 0x00000010,
    // OpMemberDecorate %SceneUBO 2 ColMajor + Offset 128 + MatrixStride 16
    0x00050048, 0x00000010, 0x00000002, 0x00000004, 0x00000001,
    0x00050048, 0x00000010, 0x00000002, 0x00000023, 0x00000080,
    0x00050048, 0x00000010, 0x00000002, 0x00000007, 0x00000010,
    // OpMemberDecorate %SceneUBO 3 ColMajor + Offset 192 + MatrixStride 16
    0x00050048, 0x00000010, 0x00000003, 0x00000004, 0x00000001,
    0x00050048, 0x00000010, 0x00000003, 0x00000023, 0x000000C0,
    0x00050048, 0x00000010, 0x00000003, 0x00000007, 0x00000010,
    // OpDecorate %scene DescriptorSet 0
    0x00040047, 0x00000012, 0x00000022, 0x00000000,
    // OpDecorate %scene Binding 0
    0x00040047, 0x00000012, 0x00000021, 0x00000000,
    // OpDecorate %inPosition Location 0
    0x00040047, 0x00000027, 0x0000001E, 0x00000000,
    // OpDecorate %inNormal Location 1
    0x00040047, 0x0000002A, 0x0000001E, 0x00000001,
    // OpDecorate %inTexCoord Location 2
    0x00040047, 0x0000002D, 0x0000001E, 0x00000002,
    // OpDecorate %fragWorldPos Location 0
    0x00040047, 0x00000030, 0x0000001E, 0x00000000,
    // OpDecorate %fragWorldNormal Location 1
    0x00040047, 0x00000033, 0x0000001E, 0x00000001,
    // OpDecorate %fragTexCoord Location 2
    0x00040047, 0x00000036, 0x0000001E, 0x00000002,

    // --- Type declarations ---
    // %void
    0x00020013, 0x00000002,
    // %void_func
    0x00030021, 0x00000003, 0x00000002,
    // %float
    0x00030016, 0x00000006, 0x00000020,
    // %vec3
    0x00040017, 0x00000007, 0x00000006, 0x00000003,
    // %vec4
    0x00040017, 0x00000008, 0x00000006, 0x00000004,
    // %vec2
    0x00040017, 0x00000009, 0x00000006, 0x00000002,
    // %uint
    0x00040015, 0x0000000A, 0x00000020, 0x00000000,
    // %uint_1
    0x0004002B, 0x0000000A, 0x00000039, 0x00000001,
    // %float_arr_1
    0x0004001C, 0x0000003A, 0x00000006, 0x00000039,
    // %gl_PerVertex struct { vec4, float, float[1], float[1] }
    0x0006001E, 0x0000000B, 0x00000008, 0x00000006, 0x0000003A, 0x0000003A,
    // %ptr_output_gl_PerVertex
    0x00040020, 0x0000000C, 0x00000003, 0x0000000B,
    // %gl_PerVertex_var
    0x0004003B, 0x0000000C, 0x0000000D, 0x00000003,
    // %int
    0x00040015, 0x0000000E, 0x00000020, 0x00000001,
    // %mat4
    0x00040018, 0x0000000F, 0x00000008, 0x00000004,
    // %mat3
    0x00040018, 0x0000003B, 0x00000007, 0x00000003,
    // %SceneUBO struct { mat4, mat4, mat4, mat4 }
    0x0006001E, 0x00000010, 0x0000000F, 0x0000000F, 0x0000000F, 0x0000000F,
    // %ptr_uniform_SceneUBO
    0x00040020, 0x00000011, 0x00000002, 0x00000010,
    // %scene_var
    0x0004003B, 0x00000011, 0x00000012, 0x00000002,
    // Constants
    // %int_0
    0x0004002B, 0x0000000E, 0x00000013, 0x00000000,
    // %int_1
    0x0004002B, 0x0000000E, 0x00000014, 0x00000001,
    // %int_2
    0x0004002B, 0x0000000E, 0x00000015, 0x00000002,
    // %int_3
    0x0004002B, 0x0000000E, 0x00000016, 0x00000003,
    // %float_0
    0x0004002B, 0x00000006, 0x00000017, 0x00000000,
    // %float_1
    0x0004002B, 0x00000006, 0x00000018, 0x3F800000,
    // %ptr_uniform_mat4
    0x00040020, 0x00000019, 0x00000002, 0x0000000F,
    // %ptr_output_vec4
    0x00040020, 0x0000001A, 0x00000003, 0x00000008,
    // %ptr_output_vec3
    0x00040020, 0x0000001B, 0x00000003, 0x00000007,
    // %ptr_output_vec2
    0x00040020, 0x0000001C, 0x00000003, 0x00000009,
    // %ptr_input_vec3
    0x00040020, 0x00000026, 0x00000001, 0x00000007,
    // %ptr_input_vec2
    0x00040020, 0x0000002C, 0x00000001, 0x00000009,
    // Input variables
    // %inPosition
    0x0004003B, 0x00000026, 0x00000027, 0x00000001,
    // %inNormal
    0x0004003B, 0x00000026, 0x0000002A, 0x00000001,
    // %inTexCoord
    0x0004003B, 0x0000002C, 0x0000002D, 0x00000001,
    // Output variables
    // %fragWorldPos
    0x0004003B, 0x0000001B, 0x00000030, 0x00000003,
    // %fragWorldNormal
    0x0004003B, 0x0000001B, 0x00000033, 0x00000003,
    // %fragTexCoord
    0x0004003B, 0x0000001C, 0x00000036, 0x00000003,

    // --- Function body ---
    // %main = OpFunction %void None %void_func
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    // %entry = OpLabel
    0x000200F8, 0x00000005,

    // Load model matrix: scene.model
    0x00050041, 0x00000019, 0x00000020, 0x00000012, 0x00000013,
    0x0004003D, 0x0000000F, 0x00000021, 0x00000020,
    // Load inPosition
    0x0004003D, 0x00000007, 0x00000028, 0x00000027,
    // Construct vec4(inPosition, 1.0)
    0x00050051, 0x00000006, 0x00000023, 0x00000028, 0x00000000,
    0x00050051, 0x00000006, 0x00000024, 0x00000028, 0x00000001,
    0x00050051, 0x00000006, 0x00000025, 0x00000028, 0x00000002,
    0x00090050, 0x00000008, 0x00000029, 0x00000023, 0x00000024, 0x00000025, 0x00000018,
    // worldPos = model * vec4(inPosition, 1.0)
    0x00050091, 0x00000008, 0x0000002B, 0x00000021, 0x00000029,
    // fragWorldPos = worldPos.xyz
    0x0008004F, 0x00000007, 0x0000002E, 0x0000002B, 0x0000002B, 0x00000000, 0x00000001, 0x00000002,
    0x0003003E, 0x00000030, 0x0000002E,

    // Load normalMatrix (mat4, use as mat3)
    0x00050041, 0x00000019, 0x0000002F, 0x00000012, 0x00000016,
    0x0004003D, 0x0000000F, 0x00000031, 0x0000002F,
    // Load inNormal
    0x0004003D, 0x00000007, 0x00000032, 0x0000002A,
    // Extract mat3 from mat4: take xyz of each column
    // col0 = normalMatrix[0].xyz
    0x00050051, 0x00000008, 0x0000003C, 0x00000031, 0x00000000,
    0x0008004F, 0x00000007, 0x0000003D, 0x0000003C, 0x0000003C, 0x00000000, 0x00000001, 0x00000002,
    // col1 = normalMatrix[1].xyz
    0x00050051, 0x00000008, 0x0000003E, 0x00000031, 0x00000001,
    0x0008004F, 0x00000007, 0x0000003F, 0x0000003E, 0x0000003E, 0x00000000, 0x00000001, 0x00000002,
    // col2 = normalMatrix[2].xyz
    0x00050051, 0x00000008, 0x00000034, 0x00000031, 0x00000002,
    0x0008004F, 0x00000007, 0x00000035, 0x00000034, 0x00000034, 0x00000000, 0x00000001, 0x00000002,
    // Construct mat3
    0x00060050, 0x0000003B, 0x00000037, 0x0000003D, 0x0000003F, 0x00000035,
    // fragWorldNormal = mat3(normalMatrix) * inNormal
    0x00050091, 0x00000007, 0x00000038, 0x00000037, 0x00000032,
    0x0003003E, 0x00000033, 0x00000038,

    // Load inTexCoord and store to fragTexCoord
    0x0004003D, 0x00000009, 0x0000001D, 0x0000002D,
    0x0003003E, 0x00000036, 0x0000001D,

    // gl_Position = projection * view * worldPos
    0x00050041, 0x00000019, 0x0000001E, 0x00000012, 0x00000015,
    0x0004003D, 0x0000000F, 0x0000001F, 0x0000001E,
    0x00050041, 0x00000019, 0x00000022, 0x00000012, 0x00000014,
    0x0004003D, 0x0000000F, 0x0000003C, 0x00000022,
    // proj * view
    0x00050092, 0x0000000F, 0x0000003D, 0x0000001F, 0x0000003C,
    // (proj * view) * worldPos
    0x00050091, 0x00000008, 0x0000003E, 0x0000003D, 0x0000002B,
    // Store to gl_Position
    0x00050041, 0x0000001A, 0x0000003F, 0x0000000D, 0x00000013,
    0x0003003E, 0x0000003F, 0x0000003E,

    // OpReturn + OpFunctionEnd
    0x000100FD, 0x00010038,
};

static constexpr ffe::u32 BLINN_PHONG_VERT_SPV_SIZE = sizeof(BLINN_PHONG_VERT_SPV);

} // namespace ffe::rhi::vk::spv
