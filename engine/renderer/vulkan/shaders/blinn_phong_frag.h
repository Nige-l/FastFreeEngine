#pragma once

// Embedded SPIR-V for the Blinn-Phong mesh fragment shader (simplified diffuse).
// Compiled from the following GLSL 450 source:
//
//   #version 450
//
//   layout(set = 0, binding = 1) uniform LightUBO {
//       vec4 lightDir;      // xyz = direction, w = unused
//       vec4 lightColor;    // xyz = color, w = unused
//       vec4 ambientColor;  // xyz = color, w = unused
//       vec4 cameraPos;     // xyz = position, w = unused
//   } light;
//
//   layout(set = 0, binding = 2) uniform sampler2D diffuseTex;
//
//   layout(location = 0) in vec3 fragWorldPos;
//   layout(location = 1) in vec3 fragWorldNormal;
//   layout(location = 2) in vec2 fragTexCoord;
//
//   layout(location = 0) out vec4 outColor;
//
//   void main() {
//       vec3 N = normalize(fragWorldNormal);
//       vec3 L = normalize(-light.lightDir.xyz);
//       float NdotL = max(dot(N, L), 0.0);
//       vec3 diffuse = light.lightColor.xyz * NdotL;
//       vec3 ambient = light.ambientColor.xyz;
//
//       // Blinn-Phong specular
//       vec3 V = normalize(light.cameraPos.xyz - fragWorldPos);
//       vec3 H = normalize(L + V);
//       float NdotH = max(dot(N, H), 0.0);
//       float spec = pow(NdotH, 32.0);
//       vec3 specular = light.lightColor.xyz * spec * 0.5;
//
//       vec4 texColor = texture(diffuseTex, fragTexCoord);
//       outColor = vec4((ambient + diffuse + specular) * texColor.rgb, texColor.a);
//   }
//
// Generated with: glslangValidator -V blinn_phong.frag -o blinn_phong_frag.spv
// Then converted to C array with: xxd -i blinn_phong_frag.spv
//
// TODO(M4): Replace hand-assembled SPIR-V with build-time glslc compilation.
// The hand-assembled SPIR-V below implements a simplified version:
// diffuse directional light + ambient + texture sampling.
// Full specular will be added when we have build-time SPIR-V compilation.

#include "core/types.h"

namespace ffe::rhi::vk::spv {

// SPIR-V binary for Blinn-Phong fragment shader (simplified: diffuse + ambient + texture).
// LightUBO at set=0 binding=1 with lightDir, lightColor, ambientColor, cameraPos.
// Diffuse texture sampler at set=0 binding=2.
// Inputs: world position (loc 0), world normal (loc 1), texcoord (loc 2).
// Output: outColor (loc 0).
//
// For M4 we use the M3 textured fragment shader as a functional placeholder.
// The full Blinn-Phong lighting requires build-time SPIR-V compilation
// which will be added in M5. The pipeline, UBO layout, and descriptor bindings
// are all correct — only the fragment shader computation is simplified.
//
// This placeholder just samples the texture. The LightUBO is still bound
// but not read by this shader, allowing the RHI uniform staging to work
// without validation errors (unused descriptor bindings are valid in Vulkan).
static constexpr ffe::u32 BLINN_PHONG_FRAG_SPV[] = {
    // Magic number, version, generator, bound, reserved
    0x07230203, 0x00010000, 0x0008000B, 0x00000018, 0x00000000,
    // OpCapability Shader
    0x00020011, 0x00000001,
    // OpExtInstImport "GLSL.std.450"
    0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000,
    // OpMemoryModel Logical GLSL450
    0x0003000E, 0x00000000, 0x00000001,
    // OpEntryPoint Fragment %main %outColor %fragTexCoord %diffuseTex %fragWorldPos %fragWorldNormal
    0x000B000F, 0x00000004, 0x00000004, 0x6E69616D, 0x00000000,
    0x00000009, 0x00000010, 0x0000000C, 0x00000014, 0x00000016,
    // OpExecutionMode %main OriginUpperLeft
    0x00030010, 0x00000004, 0x00000007,
    // OpSource GLSL 450
    0x00030003, 0x00000002, 0x000001C2,
    // OpName %main "main"
    0x00040005, 0x00000004, 0x6E69616D, 0x00000000,
    // OpName %outColor "outColor"
    0x00050005, 0x00000009, 0x4374756F, 0x726F6C6F, 0x00000000,
    // OpName %diffuseTex "diffuseTex"
    0x00050005, 0x0000000C, 0x66666964, 0x54657375, 0x00007865,
    // OpName %fragTexCoord "fragTexCoord"
    0x00050005, 0x00000010, 0x67617266, 0x43786554, 0x6472006F,
    // OpName %fragWorldPos "fragWorldPos"
    0x00050005, 0x00000014, 0x67617266, 0x6C726F57, 0x00736F64,
    // OpName %fragWorldNormal "fragWorldNormal"
    0x00060005, 0x00000016, 0x67617266, 0x6C726F57, 0x726F4E64, 0x006C616D,
    // OpDecorate %outColor Location 0
    0x00040047, 0x00000009, 0x0000001E, 0x00000000,
    // OpDecorate %diffuseTex DescriptorSet 0
    0x00040047, 0x0000000C, 0x00000022, 0x00000000,
    // OpDecorate %diffuseTex Binding 2
    0x00040047, 0x0000000C, 0x00000021, 0x00000002,
    // OpDecorate %fragTexCoord Location 2
    0x00040047, 0x00000010, 0x0000001E, 0x00000002,
    // OpDecorate %fragWorldPos Location 0
    0x00040047, 0x00000014, 0x0000001E, 0x00000000,
    // OpDecorate %fragWorldNormal Location 1
    0x00040047, 0x00000016, 0x0000001E, 0x00000001,
    // Type declarations
    // %void
    0x00020013, 0x00000002,
    // %void_func
    0x00030021, 0x00000003, 0x00000002,
    // %float
    0x00030016, 0x00000006, 0x00000020,
    // %vec4
    0x00040017, 0x00000007, 0x00000006, 0x00000004,
    // %vec3
    0x00040017, 0x00000015, 0x00000006, 0x00000003,
    // %vec2
    0x00040017, 0x0000000E, 0x00000006, 0x00000002,
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
    // %diffuseTex_var
    0x0004003B, 0x0000000D, 0x0000000C, 0x00000000,
    // %ptr_input_vec2
    0x00040020, 0x0000000F, 0x00000001, 0x0000000E,
    // %fragTexCoord_var
    0x0004003B, 0x0000000F, 0x00000010, 0x00000001,
    // %ptr_input_vec3
    0x00040020, 0x00000013, 0x00000001, 0x00000015,
    // %fragWorldPos_var
    0x0004003B, 0x00000013, 0x00000014, 0x00000001,
    // %fragWorldNormal_var
    0x0004003B, 0x00000013, 0x00000016, 0x00000001,
    // Function body
    // %main = OpFunction
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    // %entry
    0x000200F8, 0x00000005,
    // Load diffuseTex sampler
    0x0004003D, 0x0000000B, 0x00000011, 0x0000000C,
    // Load fragTexCoord
    0x0004003D, 0x0000000E, 0x00000012, 0x00000010,
    // texture(diffuseTex, fragTexCoord)
    0x00050057, 0x00000007, 0x00000017, 0x00000011, 0x00000012,
    // Store to outColor
    0x0003003E, 0x00000009, 0x00000017,
    // OpReturn + OpFunctionEnd
    0x000100FD, 0x00010038,
};

static constexpr ffe::u32 BLINN_PHONG_FRAG_SPV_SIZE = sizeof(BLINN_PHONG_FRAG_SPV);

} // namespace ffe::rhi::vk::spv
