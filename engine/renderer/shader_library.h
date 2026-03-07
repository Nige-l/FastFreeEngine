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
