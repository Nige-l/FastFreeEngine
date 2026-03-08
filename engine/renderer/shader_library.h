#pragma once

#include "renderer/rhi_types.h"

namespace ffe::renderer {

// Pre-defined shader IDs
enum class BuiltinShader : u8 {
    SOLID                = 0,
    TEXTURED             = 1,
    SPRITE               = 2,
    MESH_BLINN_PHONG     = 3,  // 3D Blinn-Phong lighting shader (ADR-007)
    SHADOW_DEPTH         = 4,  // Depth-only pass for directional light shadow mapping
    SKYBOX               = 5,  // Cubemap skybox rendering
    MESH_SKINNED         = 6,  // Blinn-Phong + bone skinning (skeletal animation)
    SHADOW_DEPTH_SKINNED = 7,  // Depth-only + bone skinning (skeletal animation shadow pass)
    MESH_PBR             = 8,  // PBR metallic-roughness (Cook-Torrance BRDF)
    MESH_PBR_SKINNED     = 9,  // PBR + bone skinning (skeletal animation)
    POST_THRESHOLD       = 10, // Bloom bright-pixel extract (luminance > threshold)
    POST_BLUR            = 11, // Separable Gaussian blur (horizontal/vertical via uniform)
    POST_FINAL           = 12, // Final composite: bloom + tone map + gamma
    MESH_BLINN_PHONG_INSTANCED = 13, // Blinn-Phong instanced (model matrix from attribs)
    MESH_PBR_INSTANCED         = 14, // PBR instanced (model matrix from attribs)
    SHADOW_DEPTH_INSTANCED     = 15, // Shadow depth instanced (model matrix from attribs)
    POST_FXAA                  = 16, // FXAA 3.11 anti-aliasing (post tone-map, pre gamma)
    SSAO_PASS                  = 17, // Screen-space ambient occlusion (hemisphere sampling)
    SSAO_BLUR                  = 18, // 4x4 box blur for SSAO noise reduction
    TERRAIN                    = 19, // Terrain splat-map texturing with triplanar projection
    WATER                      = 20, // Planar water with reflection, fresnel, animated distortion
    VEGETATION                 = 21, // GPU-instanced billboard grass (cylindrical Y-axis billboard, alpha-test)
    COUNT
};

struct ShaderLibrary {
    rhi::ShaderHandle handles[static_cast<u32>(BuiltinShader::COUNT)];
};

// Load all built-in shaders from embedded source strings.
// Returns false if any fail to compile.
bool initShaderLibrary(ShaderLibrary& library);

// Destroy all built-in shaders.
void shutdownShaderLibrary(ShaderLibrary& library);

// Get a shader handle by enum.
inline rhi::ShaderHandle getShader(const ShaderLibrary& library, const BuiltinShader id) {
    return library.handles[static_cast<u32>(id)];
}

} // namespace ffe::renderer
