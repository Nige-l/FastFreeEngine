#pragma once

// pbr_material.h -- PBR metallic-roughness material component for FFE.
//
// POD struct. No heap allocation. No pointers. No virtual functions.
// Follows the glTF 2.0 metallic-roughness workflow.
//
// Entities with PBRMaterial render via MESH_PBR shader (Cook-Torrance BRDF).
// Entities with Material3D (deprecated) continue to render via MESH_BLINN_PHONG.
// An entity must not have both; if both are present, PBRMaterial wins.
//
// Tier support: LEGACY (OpenGL 3.3 core profile).

#include "core/types.h"
#include "renderer/rhi_types.h"
#include <glm/glm.hpp>

namespace ffe::renderer {

struct PBRMaterial {
    // Base color (linear space). Alpha channel used for transparency.
    glm::vec4          albedo                = {1.0f, 1.0f, 1.0f, 1.0f};  // 16 bytes

    // Albedo texture (texture unit 0). 0 = use scalar albedo.
    rhi::TextureHandle albedoMap;                                           //  4 bytes

    // Metallic factor: 0.0 = dielectric, 1.0 = metal.
    f32                metallic              = 0.0f;                        //  4 bytes

    // Roughness factor: 0.0 = mirror, 1.0 = fully rough.
    f32                roughness             = 0.5f;                        //  4 bytes

    // Metallic-roughness map. G channel = roughness, B channel = metallic (glTF convention).
    // 0 = use scalar metallic/roughness.
    rhi::TextureHandle metallicRoughnessMap;                                //  4 bytes

    // Normal map (tangent-space). 0 = flat normal (vertex normal only).
    rhi::TextureHandle normalMap;                                           //  4 bytes

    // Normal map intensity multiplier.
    f32                normalScale           = 1.0f;                        //  4 bytes

    // Ambient occlusion map. 0 = no AO texture (ao factor used directly).
    rhi::TextureHandle aoMap;                                               //  4 bytes

    // Ambient occlusion factor (multiplied with AO map if present).
    f32                ao                    = 1.0f;                        //  4 bytes (offset 44)

    // Emissive color (linear space, HDR values allowed).
    glm::vec3          emissiveFactor        = {0.0f, 0.0f, 0.0f};         // 12 bytes

    // Emissive map. 0 = use scalar emissiveFactor.
    rhi::TextureHandle emissiveMap;                                         //  4 bytes

    // Shader override. 0 = use builtin MESH_PBR shader.
    rhi::ShaderHandle  shaderOverride;                                      //  4 bytes

    u32                _pad                  = 0;                           //  4 bytes
};
// 16 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 12 + 4 + 4 + 4 = 72 bytes
static_assert(sizeof(PBRMaterial) == 72, "PBRMaterial must be 72 bytes");

} // namespace ffe::renderer
