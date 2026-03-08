#include "renderer/shader_library.h"
#include "renderer/post_process.h"
#include "renderer/ssao.h"
#include "renderer/water.h"
#include "renderer/rhi.h"
#include "core/logging.h"

namespace ffe::renderer {

// ==================== Embedded Shader Sources ====================

// --- Solid shaders ---
static const char* const SOLID_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;

uniform mat4 u_viewProjection;
uniform mat4 u_model;

void main() {
    gl_Position = u_viewProjection * u_model * vec4(a_position, 1.0);
}
)glsl";

static const char* const SOLID_FRAG_SOURCE = R"glsl(
#version 330 core

uniform vec4 u_color;

out vec4 fragColor;

void main() {
    fragColor = u_color;
}
)glsl";

// --- Textured shaders ---
static const char* const TEXTURED_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_viewProjection;
uniform mat4 u_model;

out vec2 v_texcoord;
out vec3 v_normal;

void main() {
    v_texcoord = a_texcoord;
    v_normal = mat3(u_model) * a_normal;
    gl_Position = u_viewProjection * u_model * vec4(a_position, 1.0);
}
)glsl";

static const char* const TEXTURED_FRAG_SOURCE = R"glsl(
#version 330 core

in vec2 v_texcoord;
in vec3 v_normal;

uniform sampler2D u_texture0;
uniform vec4 u_color;

out vec4 fragColor;

void main() {
    vec4 texel = texture(u_texture0, v_texcoord);
    fragColor = texel * u_color;
}
)glsl";

// --- Sprite shaders ---
static const char* const SPRITE_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec4 a_color;

uniform mat4 u_viewProjection;

out vec2 v_texcoord;
out vec4 v_color;

void main() {
    v_texcoord = a_texcoord;
    v_color = a_color;
    gl_Position = u_viewProjection * vec4(a_position, 0.0, 1.0);
}
)glsl";

static const char* const SPRITE_FRAG_SOURCE = R"glsl(
#version 330 core

in vec2 v_texcoord;
in vec4 v_color;

uniform sampler2D u_texture0;

out vec4 fragColor;

void main() {
    vec4 texel = texture(u_texture0, v_texcoord);
    fragColor = texel * v_color;
    if (fragColor.a < 0.01) discard;
}
)glsl";

// --- 3D Blinn-Phong shaders (ADR-007 Section 9) ---
// Extended with point lights, specular maps, and normal maps (Session 44).

static const char* const MESH_BLINN_PHONG_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;
layout(location = 3) in vec3 a_tangent;

uniform mat4 u_model;
uniform mat4 u_viewProjection;
uniform mat3 u_normalMatrix;
uniform mat4 u_lightSpaceMatrix;
uniform vec4 u_clipPlane;

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_texcoord;
out vec4 v_fragPosLightSpace;
out vec3 v_tangent;

void main() {
    vec4 worldPos       = u_model * vec4(a_position, 1.0);
    v_fragPos           = worldPos.xyz;
    v_normal            = u_normalMatrix * a_normal;
    v_tangent           = u_normalMatrix * a_tangent;
    v_texcoord          = a_texcoord;
    v_fragPosLightSpace = u_lightSpaceMatrix * worldPos;
    gl_ClipDistance[0]  = (dot(u_clipPlane, u_clipPlane) < 0.001) ? 1.0 : dot(worldPos, u_clipPlane);
    gl_Position         = u_viewProjection * worldPos;
}
)glsl";

static const char* const MESH_BLINN_PHONG_FRAG_SOURCE = R"glsl(
#version 330 core

#define MAX_POINT_LIGHTS 8

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_texcoord;
in vec4 v_fragPosLightSpace;
in vec3 v_tangent;

uniform sampler2D u_diffuseTexture;
uniform vec4      u_diffuseColor;
uniform vec3      u_lightDir;
uniform vec3      u_lightColor;
uniform vec3      u_ambientColor;
uniform vec3      u_viewPos;

// Material specular properties
uniform vec3      u_specularColor;
uniform float     u_shininess;

// Normal map (texture unit 2, 0 = disabled via u_hasNormalMap)
uniform sampler2D u_normalMap;
uniform int       u_hasNormalMap;

// Specular map (texture unit 3, 0 = disabled via u_hasSpecularMap)
uniform sampler2D u_specularMap;
uniform int       u_hasSpecularMap;

// Shadow map uniforms — inactive when u_shadowsEnabled == 0
uniform sampler2D u_shadowMap;
uniform int       u_shadowsEnabled;
uniform float     u_shadowBias;

// Point lights — fixed array, only u_pointLightCount are active
uniform int   u_pointLightCount;
uniform vec3  u_pointLightPos[MAX_POINT_LIGHTS];
uniform vec3  u_pointLightColor[MAX_POINT_LIGHTS];
uniform float u_pointLightRadius[MAX_POINT_LIGHTS];

// Linear fog — inactive when u_fogEnabled == 0
uniform int   u_fogEnabled;
uniform vec3  u_fogColor;
uniform float u_fogNear;
uniform float u_fogFar;

out vec4 fragColor;

vec3 getNormal() {
    vec3 N = normalize(v_normal);
    if (u_hasNormalMap != 0) {
        // Only apply normal mapping if tangent data is available.
        // If the mesh has no tangent attribute (location 3 not enabled),
        // v_tangent will be (0,0,0) — fall back to vertex normal gracefully.
        float tangentLen = length(v_tangent);
        if (tangentLen > 0.001) {
            // Build TBN matrix from vertex normal and tangent
            vec3 T = v_tangent / tangentLen;
            // Re-orthogonalize T with respect to N (Gram-Schmidt)
            T = normalize(T - dot(T, N) * N);
            vec3 B = cross(N, T);
            mat3 TBN = mat3(T, B, N);
            // Sample normal map and remap from [0,1] to [-1,1]
            vec3 mapNormal = texture(u_normalMap, v_texcoord).rgb * 2.0 - 1.0;
            N = normalize(TBN * mapNormal);
        }
    }
    return N;
}

vec3 getSpecularFactor() {
    if (u_hasSpecularMap != 0) {
        return texture(u_specularMap, v_texcoord).rgb;
    }
    return u_specularColor;
}

void main() {
    vec3  norm      = getNormal();
    vec3  viewDir   = normalize(u_viewPos - v_fragPos);
    vec3  specFactor = getSpecularFactor();

    // --- Directional light ---
    vec3  lightDir  = normalize(-u_lightDir);

    // Diffuse
    float diff      = max(dot(norm, lightDir), 0.0);
    vec3  diffuse   = diff * u_lightColor;

    // Specular (Blinn-Phong half-vector)
    vec3  halfDir   = normalize(lightDir + viewDir);
    float spec      = pow(max(dot(norm, halfDir), 0.0), u_shininess);
    vec3  specular  = spec * u_lightColor * specFactor * 0.3;

    // Shadow factor (1.0 = fully lit, 0.0 = fully shadowed)
    float shadowFactor = 1.0;
    if (u_shadowsEnabled != 0) {
        // Perspective divide — produces NDC coords in [-1, 1]
        vec3 projCoords = v_fragPosLightSpace.xyz / v_fragPosLightSpace.w;
        // Remap to [0, 1] for texture lookup
        projCoords = projCoords * 0.5 + 0.5;
        // Only sample within the light frustum
        if (projCoords.z <= 1.0) {
            float shadow = 0.0;
            vec2 texelSize = 1.0 / vec2(textureSize(u_shadowMap, 0));
            // 3x3 PCF kernel for soft shadows
            for (int x = -1; x <= 1; ++x) {
                for (int y = -1; y <= 1; ++y) {
                    float pcfDepth = texture(u_shadowMap,
                                             projCoords.xy + vec2(x, y) * texelSize).r;
                    shadow += (projCoords.z - u_shadowBias) > pcfDepth ? 1.0 : 0.0;
                }
            }
            shadowFactor = 1.0 - (shadow / 9.0);
        }
    }

    // Directional light contribution (shadow-affected)
    vec3 dirLighting = shadowFactor * (diffuse + specular);

    // --- Point lights (additive, not shadow-mapped) ---
    vec3 pointLighting = vec3(0.0);
    for (int i = 0; i < u_pointLightCount; ++i) {
        vec3  toLight  = u_pointLightPos[i] - v_fragPos;
        float dist     = length(toLight);
        float r        = u_pointLightRadius[i];

        // Distance attenuation: 1 / (1 + (d/r)*2 + (d/r)^2)
        float dr       = dist / max(r, 0.001);
        float atten    = 1.0 / (1.0 + 2.0 * dr + dr * dr);

        vec3  pLightDir = normalize(toLight);

        // Diffuse
        float pDiff    = max(dot(norm, pLightDir), 0.0);
        vec3  pDiffuse = pDiff * u_pointLightColor[i] * atten;

        // Specular (Blinn-Phong)
        vec3  pHalf    = normalize(pLightDir + viewDir);
        float pSpec    = pow(max(dot(norm, pHalf), 0.0), u_shininess);
        vec3  pSpecular = pSpec * u_pointLightColor[i] * specFactor * 0.3 * atten;

        pointLighting += pDiffuse + pSpecular;
    }

    // Combine: ambient is never shadowed; directional + point lights
    vec4  texSample = texture(u_diffuseTexture, v_texcoord);
    vec3  lighting  = u_ambientColor + dirLighting + pointLighting;
    vec3  finalColor = lighting * texSample.rgb * u_diffuseColor.rgb;

    // Linear fog: blend towards fog color based on distance from camera
    if (u_fogEnabled != 0) {
        float dist = length(v_fragPos - u_viewPos);
        float fogFactor = clamp((u_fogFar - dist) / (u_fogFar - u_fogNear), 0.0, 1.0);
        finalColor = mix(u_fogColor, finalColor, fogFactor);
    }

    fragColor = vec4(finalColor, texSample.a * u_diffuseColor.a);
}
)glsl";

// --- Shadow depth pass shader (depth-only) ---

static const char* const SHADOW_DEPTH_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;

uniform mat4 u_lightSpaceMatrix;
uniform mat4 u_model;

void main() {
    gl_Position = u_lightSpaceMatrix * u_model * vec4(a_position, 1.0);
}
)glsl";

// Empty fragment shader — GL writes depth automatically.
static const char* const SHADOW_DEPTH_FRAG_SOURCE = R"glsl(
#version 330 core

void main() {
    // Depth written by GL automatically from gl_Position.z.
}
)glsl";

// --- Skybox shaders (cubemap environment rendering) ---

static const char* const SKYBOX_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;

uniform mat4 u_viewNoTranslation;
uniform mat4 u_projection;
uniform vec4 u_clipPlane;

out vec3 v_texCoord;

void main() {
    v_texCoord = a_position;
    vec4 pos = u_projection * u_viewNoTranslation * vec4(a_position, 1.0);
    // Skybox is always above water -- clip plane set to (0,0,0,0) when inactive.
    // When clip plane length is near zero (disabled), pass 1.0 to never clip.
    // When active, use a large positive value so skybox is never clipped.
    float clipLen = dot(u_clipPlane, u_clipPlane);
    gl_ClipDistance[0] = (clipLen < 0.001) ? 1.0 : dot(vec4(a_position * 1000.0, 1.0), u_clipPlane);
    // Set z = w so that after perspective divide, depth = 1.0 (max depth).
    // This ensures the skybox is drawn behind all other geometry.
    gl_Position = pos.xyww;
}
)glsl";

static const char* const SKYBOX_FRAG_SOURCE = R"glsl(
#version 330 core

in vec3 v_texCoord;

uniform samplerCube u_skybox;

out vec4 fragColor;

void main() {
    fragColor = texture(u_skybox, v_texCoord);
}
)glsl";

// --- Skinned mesh Blinn-Phong shaders (skeletal animation) ---
// Extends MESH_BLINN_PHONG with bone matrix vertex transformation.
// Vertex attributes: position(loc 0), normal(loc 1), texcoord(loc 2),
//                    tangent(loc 3), joints(loc 4), weights(loc 5).
// Uniform: u_boneMatrices[64] — array of bone matrices.

static const char* const MESH_SKINNED_VERT_SOURCE = R"glsl(
#version 330 core

#define MAX_BONES 64

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;
layout(location = 3) in vec3 a_tangent;
layout(location = 4) in ivec4 a_joints;
layout(location = 5) in vec4  a_weights;

uniform mat4 u_model;
uniform mat4 u_viewProjection;
uniform mat3 u_normalMatrix;
uniform mat4 u_lightSpaceMatrix;
uniform mat4 u_boneMatrices[MAX_BONES];
uniform vec4 u_clipPlane;

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_texcoord;
out vec4 v_fragPosLightSpace;
out vec3 v_tangent;

void main() {
    // Compute skinning matrix from weighted bone transforms
    mat4 skinMatrix = a_weights.x * u_boneMatrices[a_joints.x]
                    + a_weights.y * u_boneMatrices[a_joints.y]
                    + a_weights.z * u_boneMatrices[a_joints.z]
                    + a_weights.w * u_boneMatrices[a_joints.w];

    vec4 skinnedPos    = skinMatrix * vec4(a_position, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * a_normal;

    vec4 worldPos       = u_model * skinnedPos;
    v_fragPos           = worldPos.xyz;
    v_normal            = u_normalMatrix * skinnedNormal;
    v_tangent           = u_normalMatrix * (mat3(skinMatrix) * a_tangent);
    v_texcoord          = a_texcoord;
    v_fragPosLightSpace = u_lightSpaceMatrix * worldPos;
    gl_ClipDistance[0]  = (dot(u_clipPlane, u_clipPlane) < 0.001) ? 1.0 : dot(worldPos, u_clipPlane);
    gl_Position         = u_viewProjection * worldPos;
}
)glsl";

// Fragment shader is identical to MESH_BLINN_PHONG — reuse the same source.
// (Both share the same fragment shader string pointer.)

// --- Skinned shadow depth pass shader ---

static const char* const SHADOW_DEPTH_SKINNED_VERT_SOURCE = R"glsl(
#version 330 core

#define MAX_BONES 64

layout(location = 0) in vec3 a_position;
layout(location = 4) in ivec4 a_joints;
layout(location = 5) in vec4  a_weights;

uniform mat4 u_lightSpaceMatrix;
uniform mat4 u_model;
uniform mat4 u_boneMatrices[MAX_BONES];

void main() {
    mat4 skinMatrix = a_weights.x * u_boneMatrices[a_joints.x]
                    + a_weights.y * u_boneMatrices[a_joints.y]
                    + a_weights.z * u_boneMatrices[a_joints.z]
                    + a_weights.w * u_boneMatrices[a_joints.w];

    vec4 skinnedPos = skinMatrix * vec4(a_position, 1.0);
    gl_Position = u_lightSpaceMatrix * u_model * skinnedPos;
}
)glsl";

// Fragment shader for skinned shadow depth is identical to the static version.

// --- PBR metallic-roughness shaders (Cook-Torrance BRDF) ---
// Vertex shader is identical to MESH_BLINN_PHONG — same attributes:
// position(loc 0), normal(loc 1), texcoord(loc 2), tangent(loc 3).
// Fragment shader implements GGX/Smith-Schlick/Fresnel-Schlick BRDF.

static const char* const MESH_PBR_FRAG_SOURCE = R"glsl(
#version 330 core

#define MAX_POINT_LIGHTS 8
#define PI 3.14159265359

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_texcoord;
in vec4 v_fragPosLightSpace;
in vec3 v_tangent;

// --- Material uniforms ---
uniform vec4      u_albedo;
uniform float     u_metallic;
uniform float     u_roughness;
uniform float     u_normalScale;
uniform float     u_ao;
uniform vec3      u_emissiveFactor;

// --- Material texture maps ---
uniform sampler2D u_albedoMap;
uniform int       u_hasAlbedoMap;
uniform sampler2D u_metallicRoughnessMap;
uniform int       u_hasMetallicRoughnessMap;
uniform sampler2D u_normalMap;
uniform int       u_hasNormalMap;
uniform sampler2D u_aoMap;
uniform int       u_hasAoMap;
uniform sampler2D u_emissiveMap;
uniform int       u_hasEmissiveMap;

// --- Scene lighting ---
uniform vec3      u_lightDir;
uniform vec3      u_lightColor;
uniform vec3      u_ambientColor;
uniform vec3      u_viewPos;

// --- Point lights ---
uniform int   u_pointLightCount;
uniform vec3  u_pointLightPos[MAX_POINT_LIGHTS];
uniform vec3  u_pointLightColor[MAX_POINT_LIGHTS];
uniform float u_pointLightRadius[MAX_POINT_LIGHTS];

// --- Shadow map ---
uniform sampler2D u_shadowMap;
uniform int       u_shadowsEnabled;
uniform float     u_shadowBias;
uniform mat4      u_lightSpaceMatrix;

// --- Fog ---
uniform int   u_fogEnabled;
uniform vec3  u_fogColor;
uniform float u_fogNear;
uniform float u_fogFar;

// --- Skybox for IBL approximation ---
uniform samplerCube u_skybox;
uniform int         u_hasSkybox;

out vec4 fragColor;

// ----- Cook-Torrance BRDF functions -----

// GGX/Trowbridge-Reitz Normal Distribution Function
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0000001);
}

// Smith-Schlick Geometry Function (single direction)
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith Geometry Function (both view and light directions)
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1  = geometrySchlickGGX(NdotV, roughness);
    float ggx2  = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Schlick Fresnel Approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Schlick Fresnel with roughness (for IBL ambient specular)
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ----- Normal mapping -----

vec3 getNormal() {
    vec3 N = normalize(v_normal);
    if (u_hasNormalMap != 0) {
        float tangentLen = length(v_tangent);
        if (tangentLen > 0.001) {
            vec3 T = v_tangent / tangentLen;
            T = normalize(T - dot(T, N) * N);
            vec3 B = cross(N, T);
            mat3 TBN = mat3(T, B, N);
            vec3 mapNormal = texture(u_normalMap, v_texcoord).rgb * 2.0 - 1.0;
            mapNormal.xy *= u_normalScale;
            N = normalize(TBN * mapNormal);
        }
    }
    return N;
}

// ----- Shadow calculation (reused from Blinn-Phong) -----

float calcShadow(vec4 fragPosLS) {
    if (u_shadowsEnabled == 0) return 1.0;
    vec3 projCoords = fragPosLS.xyz / fragPosLS.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 1.0;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(u_shadowMap, 0));
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(u_shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (projCoords.z - u_shadowBias) > pcfDepth ? 1.0 : 0.0;
        }
    }
    return 1.0 - (shadow / 9.0);
}

// ----- Cook-Torrance reflectance for a single light -----

vec3 calcReflectance(vec3 N, vec3 V, vec3 L, vec3 radiance,
                     vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);

    float NDF = distributionGGX(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular BRDF: D * G * F / (4 * NdotL * NdotV)
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    vec3  numerator   = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3  specular    = numerator / denominator;

    // Energy conservation: diffuse = (1 - F) * (1 - metallic)
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    // Lambertian diffuse
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

void main() {
    // --- Sample material properties ---
    vec4 albedoSample = u_albedo;
    if (u_hasAlbedoMap != 0) {
        albedoSample *= texture(u_albedoMap, v_texcoord);
    }
    vec3 albedo = albedoSample.rgb;
    float alpha = albedoSample.a;

    float metallic  = u_metallic;
    float roughness = u_roughness;
    if (u_hasMetallicRoughnessMap != 0) {
        vec4 mrSample = texture(u_metallicRoughnessMap, v_texcoord);
        roughness *= mrSample.g;  // Green = roughness (glTF)
        metallic  *= mrSample.b;  // Blue  = metallic  (glTF)
    }
    // Clamp roughness to avoid division by zero in GGX
    roughness = clamp(roughness, 0.04, 1.0);

    float aoFactor = u_ao;
    if (u_hasAoMap != 0) {
        aoFactor *= texture(u_aoMap, v_texcoord).r;
    }

    vec3 emissive = u_emissiveFactor;
    if (u_hasEmissiveMap != 0) {
        emissive *= texture(u_emissiveMap, v_texcoord).rgb;
    }

    // --- Geometric terms ---
    vec3 N = getNormal();
    vec3 V = normalize(u_viewPos - v_fragPos);

    // F0: base reflectivity. Dielectrics ~ 0.04, metals use albedo.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // --- Directional light ---
    vec3 Lo = vec3(0.0);
    {
        vec3 L = normalize(-u_lightDir);
        float shadowFactor = calcShadow(v_fragPosLightSpace);
        vec3 radiance = u_lightColor * shadowFactor;
        Lo += calcReflectance(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // --- Point lights ---
    for (int i = 0; i < u_pointLightCount; ++i) {
        vec3  toLight = u_pointLightPos[i] - v_fragPos;
        float dist    = length(toLight);
        float r       = u_pointLightRadius[i];
        float dr      = dist / max(r, 0.001);
        float atten   = 1.0 / (1.0 + 2.0 * dr + dr * dr);

        vec3 L = normalize(toLight);
        vec3 radiance = u_pointLightColor[i] * atten;
        Lo += calcReflectance(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // --- Ambient / IBL approximation ---
    vec3 ambient;
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    if (u_hasSkybox != 0) {
        // IBL approximation: sample skybox at high mip for blurred result.
        // Diffuse: sample at normal direction (approximates irradiance).
        // Specular: sample at reflected direction (approximates prefiltered env).
        // This is a rough approximation — proper IBL uses precomputed irradiance
        // and prefiltered cubemaps. Sufficient for M1 visual quality.
        vec3 R = reflect(-V, N);

        // Use textureLod with roughness-based mip level for specular blur.
        // Most cubemaps have ~5 mip levels. Roughness 0 = sharp, 1 = blurry.
        float specularMip = roughness * 4.0;
        vec3 prefilteredColor = textureLod(u_skybox, R, specularMip).rgb;

        // Diffuse irradiance: sample at normal with high mip (very blurred).
        vec3 irradiance = textureLod(u_skybox, N, 4.0).rgb;

        vec3 diffuseIBL  = irradiance * albedo * kD;
        vec3 specularIBL = prefilteredColor * F;
        ambient = (diffuseIBL + specularIBL) * aoFactor;
    } else {
        // No skybox — fall back to flat ambient (same as Blinn-Phong fallback).
        ambient = u_ambientColor * albedo * kD * aoFactor;
    }

    vec3 color = ambient + Lo + emissive;

    // --- Fog ---
    if (u_fogEnabled != 0) {
        float dist = length(v_fragPos - u_viewPos);
        float fogFactor = clamp((u_fogFar - dist) / (u_fogFar - u_fogNear), 0.0, 1.0);
        color = mix(u_fogColor, color, fogFactor);
    }

    // Output HDR linear color — tone mapping is applied in a later pass (M2).
    fragColor = vec4(color, alpha);
}
)glsl";

// --- PBR skinned vertex shader ---
// Extends MESH_PBR with bone matrix vertex transformation.
// Same as MESH_SKINNED_VERT_SOURCE but for the PBR pipeline.

static const char* const MESH_PBR_SKINNED_VERT_SOURCE = R"glsl(
#version 330 core

#define MAX_BONES 64

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;
layout(location = 3) in vec3 a_tangent;
layout(location = 4) in ivec4 a_joints;
layout(location = 5) in vec4  a_weights;

uniform mat4 u_model;
uniform mat4 u_viewProjection;
uniform mat3 u_normalMatrix;
uniform mat4 u_lightSpaceMatrix;
uniform mat4 u_boneMatrices[MAX_BONES];
uniform vec4 u_clipPlane;

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_texcoord;
out vec4 v_fragPosLightSpace;
out vec3 v_tangent;

void main() {
    mat4 skinMatrix = a_weights.x * u_boneMatrices[a_joints.x]
                    + a_weights.y * u_boneMatrices[a_joints.y]
                    + a_weights.z * u_boneMatrices[a_joints.z]
                    + a_weights.w * u_boneMatrices[a_joints.w];

    vec4 skinnedPos    = skinMatrix * vec4(a_position, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * a_normal;

    vec4 worldPos       = u_model * skinnedPos;
    v_fragPos           = worldPos.xyz;
    v_normal            = u_normalMatrix * skinnedNormal;
    v_tangent           = u_normalMatrix * (mat3(skinMatrix) * a_tangent);
    v_texcoord          = a_texcoord;
    v_fragPosLightSpace = u_lightSpaceMatrix * worldPos;
    gl_ClipDistance[0]  = (dot(u_clipPlane, u_clipPlane) < 0.001) ? 1.0 : dot(worldPos, u_clipPlane);
    gl_Position         = u_viewProjection * worldPos;
}
)glsl";

// Fragment shader for PBR skinned is identical to the static PBR version.
// Both share the same fragment shader string pointer (MESH_PBR_FRAG_SOURCE).

// --- Instanced Blinn-Phong vertex shader ---
// Model matrix read from per-instance vertex attributes (slots 8-11) instead of uniform.
// Normal matrix computed in the shader from the instance model matrix.

static const char* const MESH_BLINN_PHONG_INSTANCED_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;
layout(location = 3) in vec3 a_tangent;

// Per-instance model matrix (4 vec4 columns at locations 8-11)
layout(location = 8)  in vec4 a_instanceModel0;
layout(location = 9)  in vec4 a_instanceModel1;
layout(location = 10) in vec4 a_instanceModel2;
layout(location = 11) in vec4 a_instanceModel3;

uniform mat4 u_viewProjection;
uniform mat4 u_lightSpaceMatrix;
uniform vec4 u_clipPlane;

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_texcoord;
out vec4 v_fragPosLightSpace;
out vec3 v_tangent;

void main() {
    mat4 model = mat4(a_instanceModel0, a_instanceModel1,
                      a_instanceModel2, a_instanceModel3);
    mat3 normalMatrix = transpose(inverse(mat3(model)));

    vec4 worldPos       = model * vec4(a_position, 1.0);
    v_fragPos           = worldPos.xyz;
    v_normal            = normalMatrix * a_normal;
    v_tangent           = normalMatrix * a_tangent;
    v_texcoord          = a_texcoord;
    v_fragPosLightSpace = u_lightSpaceMatrix * worldPos;
    gl_ClipDistance[0]  = (dot(u_clipPlane, u_clipPlane) < 0.001) ? 1.0 : dot(worldPos, u_clipPlane);
    gl_Position         = u_viewProjection * worldPos;
}
)glsl";

// --- Instanced PBR vertex shader ---
// Same as Blinn-Phong instanced but paired with the PBR fragment shader.
// The vertex outputs are identical — only the fragment shader differs.

static const char* const MESH_PBR_INSTANCED_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;
layout(location = 3) in vec3 a_tangent;

// Per-instance model matrix (4 vec4 columns at locations 8-11)
layout(location = 8)  in vec4 a_instanceModel0;
layout(location = 9)  in vec4 a_instanceModel1;
layout(location = 10) in vec4 a_instanceModel2;
layout(location = 11) in vec4 a_instanceModel3;

uniform mat4 u_viewProjection;
uniform mat4 u_lightSpaceMatrix;
uniform vec4 u_clipPlane;

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_texcoord;
out vec4 v_fragPosLightSpace;
out vec3 v_tangent;

void main() {
    mat4 model = mat4(a_instanceModel0, a_instanceModel1,
                      a_instanceModel2, a_instanceModel3);
    mat3 normalMatrix = transpose(inverse(mat3(model)));

    vec4 worldPos       = model * vec4(a_position, 1.0);
    v_fragPos           = worldPos.xyz;
    v_normal            = normalMatrix * a_normal;
    v_tangent           = normalMatrix * a_tangent;
    v_texcoord          = a_texcoord;
    v_fragPosLightSpace = u_lightSpaceMatrix * worldPos;
    gl_ClipDistance[0]  = (dot(u_clipPlane, u_clipPlane) < 0.001) ? 1.0 : dot(worldPos, u_clipPlane);
    gl_Position         = u_viewProjection * worldPos;
}
)glsl";

// --- Instanced shadow depth vertex shader ---
// Depth-only pass with per-instance model matrix.

static const char* const SHADOW_DEPTH_INSTANCED_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;

// Per-instance model matrix (4 vec4 columns at locations 8-11)
layout(location = 8)  in vec4 a_instanceModel0;
layout(location = 9)  in vec4 a_instanceModel1;
layout(location = 10) in vec4 a_instanceModel2;
layout(location = 11) in vec4 a_instanceModel3;

uniform mat4 u_lightSpaceMatrix;

void main() {
    mat4 model = mat4(a_instanceModel0, a_instanceModel1,
                      a_instanceModel2, a_instanceModel3);
    gl_Position = u_lightSpaceMatrix * model * vec4(a_position, 1.0);
}
)glsl";

// --- Post-processing: fullscreen triangle vertex shader ---
// Generates a fullscreen triangle from gl_VertexID (0, 1, 2).
// No VBO needed — just bind an empty VAO and glDrawArrays(GL_TRIANGLES, 0, 3).

static const char* const FULLSCREEN_VERT_SOURCE = R"glsl(
#version 330 core

out vec2 vUV;

void main() {
    // Fullscreen triangle covering [-1,+1] clip space.
    // Vertex 0: (-1,-1), Vertex 1: (3,-1), Vertex 2: (-1, 3)
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    gl_Position = vec4(x, y, 0.0, 1.0);
    vUV = vec2(x + 1.0, y + 1.0) * 0.5;
}
)glsl";

// --- Post-processing: bloom threshold extract shader ---

static const char* const POST_THRESHOLD_FRAG_SOURCE = R"glsl(
#version 330 core

in vec2 vUV;

uniform sampler2D u_scene;
uniform float     u_threshold;

out vec4 fragColor;

void main() {
    vec3 color = texture(u_scene, vUV).rgb;
    // Luminance (ITU BT.709)
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    if (lum > u_threshold) {
        fragColor = vec4(color, 1.0);
    } else {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
)glsl";

// --- Post-processing: separable Gaussian blur shader ---
// u_horizontal = 1 for horizontal pass, 0 for vertical.
// 13-tap kernel (6 + 1 + 6) for quality bloom at half resolution.

static const char* const POST_BLUR_FRAG_SOURCE = R"glsl(
#version 330 core

in vec2 vUV;

uniform sampler2D u_image;
uniform int       u_horizontal;

out vec4 fragColor;

// 13-tap Gaussian weights (sigma ~= 4.0, normalised to sum to 1.0).
const float weights[7] = float[](
    0.1964825501511404,
    0.17466632171698539,
    0.12098536225957168,
    0.06559061721935779,
    0.02773012956114892,
    0.009167927656011385,
    0.002361965422592713
);

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(u_image, 0));
    vec3 result = texture(u_image, vUV).rgb * weights[0];

    if (u_horizontal != 0) {
        for (int i = 1; i < 7; ++i) {
            result += texture(u_image, vUV + vec2(texelSize.x * float(i), 0.0)).rgb * weights[i];
            result += texture(u_image, vUV - vec2(texelSize.x * float(i), 0.0)).rgb * weights[i];
        }
    } else {
        for (int i = 1; i < 7; ++i) {
            result += texture(u_image, vUV + vec2(0.0, texelSize.y * float(i))).rgb * weights[i];
            result += texture(u_image, vUV - vec2(0.0, texelSize.y * float(i))).rgb * weights[i];
        }
    }

    fragColor = vec4(result, 1.0);
}
)glsl";

// --- Post-processing: final composite shader ---
// Combines scene + bloom, applies tone mapping, and gamma correction.

static const char* const POST_FINAL_FRAG_SOURCE = R"glsl(
#version 330 core

in vec2 vUV;

uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform sampler2D u_ssaoTex;
uniform int       u_bloomEnabled;
uniform float     u_bloomIntensity;
uniform int       u_toneMapper;    // 0 = none, 1 = Reinhard, 2 = ACES
uniform int       u_gammaEnabled;
uniform float     u_gamma;
uniform int       u_ssaoEnabled;

out vec4 fragColor;

// Reinhard tone mapping
vec3 tonemapReinhard(vec3 c) {
    return c / (c + vec3(1.0));
}

// ACES filmic (Stephen Hill fitted approximation)
vec3 tonemapACES(vec3 c) {
    return (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
}

void main() {
    vec3 color = texture(u_scene, vUV).rgb;

    // Apply SSAO: multiply scene color by ambient occlusion factor.
    // AO = 1.0 means no occlusion, 0.0 means fully occluded.
    if (u_ssaoEnabled != 0) {
        float ao = texture(u_ssaoTex, vUV).r;
        color *= ao;
    }

    // Add bloom
    if (u_bloomEnabled != 0) {
        vec3 bloom = texture(u_bloom, vUV).rgb;
        color += bloom * u_bloomIntensity;
    }

    // Tone mapping
    if (u_toneMapper == 1) {
        color = tonemapReinhard(color);
    } else if (u_toneMapper == 2) {
        color = tonemapACES(color);
    }

    // Gamma correction
    if (u_gammaEnabled != 0) {
        color = pow(max(color, vec3(0.0)), vec3(1.0 / u_gamma));
    }

    fragColor = vec4(color, 1.0);
}
)glsl";

// --- Post-processing: FXAA 3.11 shader ---
// Based on Timothy Lottes' FXAA algorithm (public domain).
// Single-pass edge detection + sub-pixel anti-aliasing.
// Runs after tone mapping, before gamma correction.
// Input: LDR scene texture. Output: anti-aliased scene.

static const char* const POST_FXAA_FRAG_SOURCE = R"glsl(
#version 330 core

in vec2 vUV;

uniform sampler2D u_scene;
uniform vec2      u_inverseScreenSize; // 1.0 / vec2(width, height)
uniform int       u_gammaEnabled;
uniform float     u_gamma;

out vec4 fragColor;

// Perceptual luma (same weights as NTSC/BT.601)
float fxaaLuma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    // Sample center and 4 neighbours
    vec3 rgbM  = texture(u_scene, vUV).rgb;
    vec3 rgbNW = texture(u_scene, vUV + vec2(-1.0, -1.0) * u_inverseScreenSize).rgb;
    vec3 rgbNE = texture(u_scene, vUV + vec2( 1.0, -1.0) * u_inverseScreenSize).rgb;
    vec3 rgbSW = texture(u_scene, vUV + vec2(-1.0,  1.0) * u_inverseScreenSize).rgb;
    vec3 rgbSE = texture(u_scene, vUV + vec2( 1.0,  1.0) * u_inverseScreenSize).rgb;

    float lumM  = fxaaLuma(rgbM);
    float lumNW = fxaaLuma(rgbNW);
    float lumNE = fxaaLuma(rgbNE);
    float lumSW = fxaaLuma(rgbSW);
    float lumSE = fxaaLuma(rgbSE);

    float lumMin = min(lumM, min(min(lumNW, lumNE), min(lumSW, lumSE)));
    float lumMax = max(lumM, max(max(lumNW, lumNE), max(lumSW, lumSE)));
    float lumRange = lumMax - lumMin;

    // Early exit: if contrast is below threshold, no AA needed.
    // Threshold tuned for LEGACY quality preset.
    float edgeThreshold    = 0.0833;  // 1/12 — visible edges
    float edgeThresholdMin = 0.0625;  // 1/16 — avoid processing dark areas

    if (lumRange < max(edgeThresholdMin, lumMax * edgeThreshold)) {
        fragColor = vec4(rgbM, 1.0);
        return;
    }

    // Compute edge direction from luma gradient
    float lumNWSE = lumNW + lumSE;
    float lumNESW = lumNE + lumSW;

    // Additional samples along cardinal directions for better quality
    vec3 rgbN = texture(u_scene, vUV + vec2( 0.0, -1.0) * u_inverseScreenSize).rgb;
    vec3 rgbS = texture(u_scene, vUV + vec2( 0.0,  1.0) * u_inverseScreenSize).rgb;
    vec3 rgbW = texture(u_scene, vUV + vec2(-1.0,  0.0) * u_inverseScreenSize).rgb;
    vec3 rgbE = texture(u_scene, vUV + vec2( 1.0,  0.0) * u_inverseScreenSize).rgb;

    float lumN = fxaaLuma(rgbN);
    float lumS = fxaaLuma(rgbS);
    float lumW = fxaaLuma(rgbW);
    float lumE = fxaaLuma(rgbE);

    float lumNS = lumN + lumS;
    float lumWE = lumW + lumE;

    // Edge-perpendicular direction
    float edgeHorz = abs(-2.0 * lumW + lumNWSE) +
                     abs(-2.0 * lumM + lumNS) * 2.0 +
                     abs(-2.0 * lumE + lumNESW);
    float edgeVert = abs(-2.0 * lumN + (lumNW + lumNE)) +
                     abs(-2.0 * lumM + lumWE) * 2.0 +
                     abs(-2.0 * lumS + (lumSW + lumSE));

    bool isHorizontal = (edgeHorz >= edgeVert);

    // Choose perpendicular step direction
    float stepLength = isHorizontal ? u_inverseScreenSize.y : u_inverseScreenSize.x;

    float lumPos = isHorizontal ? lumS : lumE;
    float lumNeg = isHorizontal ? lumN : lumW;

    float gradientPos = abs(lumPos - lumM);
    float gradientNeg = abs(lumNeg - lumM);

    if (gradientNeg > gradientPos) {
        stepLength = -stepLength;
    }

    // Sub-pixel shift: average luma of neighbourhood vs center
    float subpixNWSWNESE = lumNWSE + lumNESW;
    float lumAvg = (1.0 / 12.0) * (2.0 * lumNS + 2.0 * lumWE + subpixNWSWNESE);
    float subpixOffset = clamp(abs(lumAvg - lumM) / lumRange, 0.0, 1.0);
    float subpixSmooth = (-2.0 * subpixOffset + 3.0) * subpixOffset * subpixOffset;

    // Clamp subpixel AA strength (quality 12 preset)
    float subpixTrim = 0.25;
    subpixSmooth = min(subpixSmooth, subpixTrim);
    subpixSmooth *= (1.0 / subpixTrim);

    // Apply sub-pixel shift perpendicular to edge
    vec2 posOffset = vUV;
    if (isHorizontal) {
        posOffset.y += stepLength * subpixSmooth;
    } else {
        posOffset.x += stepLength * subpixSmooth;
    }

    // Edge search along the edge direction (up to 8 steps each way)
    vec2 edgeDir = isHorizontal ? vec2(u_inverseScreenSize.x, 0.0)
                                : vec2(0.0, u_inverseScreenSize.y);
    vec2 posP = posOffset + edgeDir;
    vec2 posN = posOffset - edgeDir;

    float lumEndP = fxaaLuma(texture(u_scene, posP).rgb) - lumM;
    float lumEndN = fxaaLuma(texture(u_scene, posN).rgb) - lumM;

    bool reachedP = abs(lumEndP) >= gradientPos * 0.5;
    bool reachedN = abs(lumEndN) >= gradientNeg * 0.5;

    // Search along edge (8 steps max for quality)
    for (int i = 0; i < 8; ++i) {
        if (!reachedP) {
            posP += edgeDir;
            lumEndP = fxaaLuma(texture(u_scene, posP).rgb) - lumM;
            reachedP = abs(lumEndP) >= gradientPos * 0.5;
        }
        if (!reachedN) {
            posN -= edgeDir;
            lumEndN = fxaaLuma(texture(u_scene, posN).rgb) - lumM;
            reachedN = abs(lumEndN) >= gradientNeg * 0.5;
        }
        if (reachedP && reachedN) break;
    }

    // Compute edge blend factor based on search distance
    float distP = isHorizontal ? (posP.x - vUV.x) : (posP.y - vUV.y);
    float distN = isHorizontal ? (vUV.x - posN.x) : (vUV.y - posN.y);

    float spanLen = distP + distN;
    float pixelOffset = -min(distN, distP) / spanLen + 0.5;

    // Final blend: take the larger of edge-based and sub-pixel offsets
    float finalOffset = max(pixelOffset * stepLength, stepLength * subpixSmooth);

    vec2 finalPos = vUV;
    if (isHorizontal) {
        finalPos.y += finalOffset;
    } else {
        finalPos.x += finalOffset;
    }

    vec3 result = texture(u_scene, finalPos).rgb;

    // Apply gamma correction (FXAA runs after tone mapping, before gamma)
    if (u_gammaEnabled != 0) {
        result = pow(max(result, vec3(0.0)), vec3(1.0 / u_gamma));
    }

    fragColor = vec4(result, 1.0);
}
)glsl";

// --- SSAO: hemisphere sampling shader ---
// Reconstructs view-space position from depth buffer, samples a hemisphere
// of points, compares depths to determine occlusion. Outputs single float.
// GLSL 330 core — all operations available in GL 3.3.

static const char* const SSAO_PASS_FRAG_SOURCE = R"glsl(
#version 330 core

in vec2 vUV;

uniform sampler2D u_depthTex;
uniform sampler2D u_noiseTex;

uniform mat4  u_projection;
uniform mat4  u_invProjection;

uniform vec3  u_samples[64];
uniform int   u_kernelSize;
uniform float u_radius;
uniform float u_bias;
uniform float u_intensity;
uniform vec2  u_noiseScale;

out float fragColor;

// Reconstruct view-space position from depth buffer value and UV.
vec3 reconstructViewPos(vec2 uv, float depth) {
    // Convert to NDC: [0,1] -> [-1,1]
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = u_invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main() {
    // Sample depth at this fragment
    float depth = texture(u_depthTex, vUV).r;

    // Skip skybox / far plane fragments
    if (depth >= 1.0) {
        fragColor = 1.0;
        return;
    }

    // Reconstruct view-space position
    vec3 fragPos = reconstructViewPos(vUV, depth);

    // Reconstruct view-space normal from depth buffer (cross product of
    // partial derivatives). This avoids needing a separate normal G-buffer.
    // The artifacts on thin geometry mentioned in the ADR are acceptable
    // for LEGACY tier.
    vec3 dFdxPos = dFdx(fragPos);
    vec3 dFdyPos = dFdy(fragPos);
    vec3 normal = normalize(cross(dFdxPos, dFdyPos));

    // Sample the noise texture for random rotation in tangent plane
    vec3 randomVec = texture(u_noiseTex, vUV * u_noiseScale).xyz;

    // Build TBN matrix to orient the hemisphere along the surface normal
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // Accumulate occlusion
    float occlusion = 0.0;

    for (int i = 0; i < u_kernelSize; ++i) {
        // Transform sample from tangent space to view space
        vec3 samplePos = TBN * u_samples[i];
        samplePos = fragPos + samplePos * u_radius;

        // Project sample to screen space
        vec4 offset = u_projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5; // NDC -> [0,1]

        // Sample depth at the projected position
        float sampleDepth = texture(u_depthTex, offset.xy).r;
        vec3 sampleViewPos = reconstructViewPos(offset.xy, sampleDepth);

        // Range check: only count occlusion from samples within the radius
        float rangeCheck = smoothstep(0.0, 1.0,
            u_radius / max(abs(fragPos.z - sampleViewPos.z), 0.001));

        // Compare: if the sample depth is closer to the camera than our
        // sample point, it is occluded.
        occlusion += (sampleViewPos.z >= samplePos.z + u_bias ? 1.0 : 0.0) * rangeCheck;
    }

    // Normalise and invert: 0 = fully occluded, 1 = no occlusion
    occlusion = 1.0 - (occlusion / float(u_kernelSize));

    // Apply intensity
    fragColor = pow(occlusion, u_intensity);
}
)glsl";

// --- SSAO: box blur shader ---
// Simple 4x4 box blur to smooth the raw SSAO output and reduce noise.
// GLSL 330 core.

static const char* const SSAO_BLUR_FRAG_SOURCE = R"glsl(
#version 330 core

in vec2 vUV;

uniform sampler2D u_ssaoInput;

out float fragColor;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(u_ssaoInput, 0));
    float result = 0.0;

    // 4x4 box blur centered on the fragment
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x) + 0.5, float(y) + 0.5) * texelSize;
            result += texture(u_ssaoInput, vUV + offset).r;
        }
    }

    fragColor = result / 16.0;
}
)glsl";

// --- Terrain splat-map texturing shader (Phase 9 M2) ---
// Vertex shader: identical to MESH_BLINN_PHONG but uses separate view/projection uniforms.
// Fragment shader: 4-layer splat-map blending with optional triplanar projection.
// GLSL 330 core (LEGACY tier).

static const char* const TERRAIN_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_model;
uniform mat4 u_viewProjection;
uniform mat3 u_normalMatrix;
uniform mat4 u_lightSpaceMatrix;
uniform vec4 u_clipPlane;

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_texcoord;
out vec4 v_fragPosLightSpace;

void main() {
    vec4 worldPos       = u_model * vec4(a_position, 1.0);
    v_fragPos           = worldPos.xyz;
    v_normal            = u_normalMatrix * a_normal;
    v_texcoord          = a_texcoord;
    v_fragPosLightSpace = u_lightSpaceMatrix * worldPos;
    gl_ClipDistance[0]  = (dot(u_clipPlane, u_clipPlane) < 0.001) ? 1.0 : dot(worldPos, u_clipPlane);
    gl_Position         = u_viewProjection * worldPos;
}
)glsl";

static const char* const TERRAIN_FRAG_SOURCE = R"glsl(
#version 330 core

#define MAX_POINT_LIGHTS 8

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_texcoord;
in vec4 v_fragPosLightSpace;

// Splat map: RGBA = weights for 4 layers (texture unit 1)
uniform sampler2D u_splatMap;

// Layer textures (texture units 2-5)
uniform sampler2D u_layer0;
uniform sampler2D u_layer1;
uniform sampler2D u_layer2;
uniform sampler2D u_layer3;

// Per-layer UV tiling scale
uniform float u_layerScale0;
uniform float u_layerScale1;
uniform float u_layerScale2;
uniform float u_layerScale3;

// Triplanar projection
uniform int   u_triplanarEnabled;
uniform float u_triplanarThreshold;

// Lighting (same as MESH_BLINN_PHONG)
uniform vec3  u_lightDir;
uniform vec3  u_lightColor;
uniform vec3  u_ambientColor;
uniform vec3  u_viewPos;

// Point lights
uniform int   u_pointLightCount;
uniform vec3  u_pointLightPos[MAX_POINT_LIGHTS];
uniform vec3  u_pointLightColor[MAX_POINT_LIGHTS];
uniform float u_pointLightRadius[MAX_POINT_LIGHTS];

// Shadow map (texture unit 0)
uniform sampler2D u_shadowMap;
uniform int       u_shadowsEnabled;
uniform float     u_shadowBias;

// Fog
uniform int   u_fogEnabled;
uniform vec3  u_fogColor;
uniform float u_fogNear;
uniform float u_fogFar;

out vec4 fragColor;

// Triplanar sampling: sample texture from 3 axis-aligned projections
// and blend by surface normal weights.
vec3 sampleLayerTriplanar(sampler2D tex, float scale, vec3 worldPos, vec3 blend) {
    vec3 xzSample = texture(tex, worldPos.xz * scale).rgb;
    vec3 xySample = texture(tex, worldPos.xy * scale).rgb;
    vec3 yzSample = texture(tex, worldPos.yz * scale).rgb;
    return xzSample * blend.y + xySample * blend.z + yzSample * blend.x;
}

// PCF 3x3 shadow calculation (same as MESH_BLINN_PHONG)
float calcShadow() {
    if (u_shadowsEnabled == 0) return 1.0;
    vec3 projCoords = v_fragPosLightSpace.xyz / v_fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 1.0;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(u_shadowMap, 0));
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(u_shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (projCoords.z - u_shadowBias) > pcfDepth ? 1.0 : 0.0;
        }
    }
    return 1.0 - (shadow / 9.0);
}

void main() {
    vec3 N = normalize(v_normal);

    // Sample splat map — RGBA = weights for layers 0-3
    vec4 splat = texture(u_splatMap, v_texcoord);
    float totalWeight = splat.r + splat.g + splat.b + splat.a;
    if (totalWeight > 0.001) {
        splat /= totalWeight; // Normalize weights
    }

    // Determine terrain color via layer blending
    vec3 terrainColor;
    if (u_triplanarEnabled != 0 && abs(N.y) < u_triplanarThreshold) {
        // Triplanar blending for steep surfaces
        vec3 blend = abs(N);
        blend = max(blend, vec3(0.00001));
        float b = blend.x + blend.y + blend.z;
        blend /= b;

        terrainColor =
            sampleLayerTriplanar(u_layer0, u_layerScale0, v_fragPos, blend) * splat.r +
            sampleLayerTriplanar(u_layer1, u_layerScale1, v_fragPos, blend) * splat.g +
            sampleLayerTriplanar(u_layer2, u_layerScale2, v_fragPos, blend) * splat.b +
            sampleLayerTriplanar(u_layer3, u_layerScale3, v_fragPos, blend) * splat.a;
    } else {
        // Standard UV sampling for flat surfaces
        terrainColor =
            texture(u_layer0, v_texcoord * u_layerScale0).rgb * splat.r +
            texture(u_layer1, v_texcoord * u_layerScale1).rgb * splat.g +
            texture(u_layer2, v_texcoord * u_layerScale2).rgb * splat.b +
            texture(u_layer3, v_texcoord * u_layerScale3).rgb * splat.a;
    }

    // --- Blinn-Phong directional light ---
    vec3 viewDir  = normalize(u_viewPos - v_fragPos);
    vec3 lightDir = normalize(-u_lightDir);
    vec3 halfDir  = normalize(lightDir + viewDir);

    float diff = max(dot(N, lightDir), 0.0);
    float spec = pow(max(dot(N, halfDir), 0.0), 32.0);

    vec3 diffuse  = diff * u_lightColor;
    vec3 specular = spec * u_lightColor * vec3(0.1); // Low specular for terrain

    // Shadow
    float shadowFactor = calcShadow();
    vec3 dirLighting = shadowFactor * (diffuse + specular);

    // --- Point lights (additive, not shadow-mapped) ---
    vec3 pointLighting = vec3(0.0);
    for (int i = 0; i < u_pointLightCount; ++i) {
        vec3  toLight  = u_pointLightPos[i] - v_fragPos;
        float dist     = length(toLight);
        float r        = u_pointLightRadius[i];
        float dr       = dist / max(r, 0.001);
        float atten    = 1.0 / (1.0 + 2.0 * dr + dr * dr);

        vec3  pLightDir = normalize(toLight);
        float pDiff     = max(dot(N, pLightDir), 0.0);
        vec3  pDiffuse  = pDiff * u_pointLightColor[i] * atten;

        vec3  pHalf     = normalize(pLightDir + viewDir);
        float pSpec     = pow(max(dot(N, pHalf), 0.0), 32.0);
        vec3  pSpecular = pSpec * u_pointLightColor[i] * vec3(0.1) * atten;

        pointLighting += pDiffuse + pSpecular;
    }

    // Combine: ambient + directional (shadow-affected) + point lights
    vec3 lighting   = u_ambientColor + dirLighting + pointLighting;
    vec3 finalColor = lighting * terrainColor;

    // Linear fog
    if (u_fogEnabled != 0) {
        float dist = length(v_fragPos - u_viewPos);
        float fogFactor = clamp((u_fogFar - dist) / (u_fogFar - u_fogNear), 0.0, 1.0);
        finalColor = mix(u_fogColor, finalColor, fogFactor);
    }

    fragColor = vec4(finalColor, 1.0);
}
)glsl";

// --- Water shader (planar water with reflection, fresnel, animated distortion) ---

static const char* const WATER_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texcoord;

uniform mat4 u_model;
uniform mat4 u_viewProjection;
uniform float u_waterLevel;

out vec3 v_worldPos;
out vec4 v_clipSpace;
out vec2 v_texCoord;

void main() {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos    = worldPos.xyz;
    v_clipSpace   = u_viewProjection * worldPos;
    v_texCoord    = a_texcoord;
    gl_Position   = v_clipSpace;
}
)glsl";

static const char* const WATER_FRAG_SOURCE = R"glsl(
#version 330 core

in vec3 v_worldPos;
in vec4 v_clipSpace;
in vec2 v_texCoord;

uniform sampler2D u_reflectionTex;
uniform sampler2D u_depthTex;
uniform int       u_hasDepthTex;
uniform float     u_time;
uniform vec3      u_cameraPos;
uniform vec4      u_shallowColor;
uniform vec4      u_deepColor;
uniform float     u_maxDepth;
uniform float     u_waveSpeed;
uniform float     u_waveScale;
uniform float     u_fresnelPower;
uniform float     u_fresnelBias;
uniform float     u_reflDistortion;
uniform float     u_nearPlane;
uniform float     u_farPlane;

// Fog uniforms (same as MESH_BLINN_PHONG)
uniform int       u_fogEnabled;
uniform vec3      u_fogColor;
uniform float     u_fogNear;
uniform float     u_fogFar;

out vec4 FragColor;

float linearizeDepth(float d) {
    float z = d * 2.0 - 1.0; // Back to NDC
    return (2.0 * u_nearPlane * u_farPlane) / (u_farPlane + u_nearPlane - z * (u_farPlane - u_nearPlane));
}

void main() {
    // 1. Procedural wave normal (no texture file needed)
    vec2 uv1 = v_texCoord * 8.0 + vec2(u_time * u_waveSpeed, u_time * u_waveSpeed * 0.7);
    vec2 uv2 = v_texCoord * 6.0 - vec2(u_time * u_waveSpeed * 0.5, u_time * u_waveSpeed * 1.1);
    vec3 waveNormal = normalize(vec3(
        sin(uv1.x * 6.28) * cos(uv2.y * 6.28) * u_waveScale,
        1.0,
        cos(uv1.y * 6.28) * sin(uv2.x * 6.28) * u_waveScale
    ));

    // 2. Projective reflection UV
    vec2 reflUV = (v_clipSpace.xy / v_clipSpace.w) * 0.5 + 0.5;
    reflUV.y = 1.0 - reflUV.y;  // flip vertically for reflection
    reflUV += waveNormal.xz * u_reflDistortion;  // distort by wave
    reflUV = clamp(reflUV, 0.001, 0.999);  // prevent edge artifacts
    vec3 reflColor = texture(u_reflectionTex, reflUV).rgb;

    // 3. Depth-based transparency (edge fade)
    float depthFactor = 0.5; // default mid-blend when no depth texture
    if (u_hasDepthTex != 0) {
        vec2 screenUV = (v_clipSpace.xy / v_clipSpace.w) * 0.5 + 0.5;
        float sceneDepth = linearizeDepth(texture(u_depthTex, screenUV).r);
        float waterDepth = linearizeDepth(gl_FragCoord.z);
        float depthDiff = sceneDepth - waterDepth;
        depthFactor = clamp(depthDiff / u_maxDepth, 0.0, 1.0);
    }
    vec4 waterColor = mix(u_shallowColor, u_deepColor, depthFactor);

    // 4. Fresnel (Schlick approximation)
    vec3 viewDir = normalize(u_cameraPos - v_worldPos);
    float fresnel = u_fresnelBias + (1.0 - u_fresnelBias) *
        pow(1.0 - max(dot(viewDir, waveNormal), 0.0), u_fresnelPower);
    fresnel = clamp(fresnel, 0.0, 1.0);

    // 5. Final blend
    vec3 finalColor = mix(waterColor.rgb, reflColor, fresnel);
    float finalAlpha = mix(waterColor.a, 1.0, fresnel);

    // 6. Fog
    if (u_fogEnabled != 0) {
        float dist = length(v_worldPos - u_cameraPos);
        float fogFactor = clamp((u_fogFar - dist) / (u_fogFar - u_fogNear), 0.0, 1.0);
        finalColor = mix(u_fogColor, finalColor, fogFactor);
    }

    FragColor = vec4(finalColor, finalAlpha);
}
)glsl";

// --- Vegetation shader (GPU-instanced billboard grass, Y-axis cylindrical billboard) ---
// GLSL 330 core (LEGACY tier).
// Vertex inputs:
//   slot 0: vec3 a_pos    -- local quad position (X in [-0.5,+0.5], Y in [0,1], Z=0)
//   slot 1: vec2 a_uv     -- UV coordinates
//   slot 4: vec3 i_position  (divisor=1) -- world-space instance position
//   slot 5: float i_scale    (divisor=1) -- uniform scale
//   slot 6: float i_rotation (divisor=1) -- Y-axis pre-rotation (radians, unused; billboarding computed per-frame)
// The billboard rotates the quad X-axis toward the camera each frame.
// Alpha-test via discard (no blending) — eliminates sorting.

static const char* const VEGETATION_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;

layout(location = 4) in vec3  i_position;
layout(location = 5) in float i_scale;
layout(location = 6) in float i_rotation;

uniform mat4  u_view;
uniform mat4  u_projection;
uniform vec3  u_cameraPos;
uniform float u_fadeStart;
uniform float u_fadeEnd;

out vec2  v_uv;
out float v_alpha;

void main() {
    // Cylindrical Y-axis billboard: rotate quad to face camera around Y only.
    vec3 toCamera = u_cameraPos - i_position;
    toCamera.y = 0.0;
    float len = length(toCamera);
    vec3 forward = (len > 0.0001) ? (toCamera / len) : vec3(0.0, 0.0, 1.0);
    // right = cross(up, forward) — negate to maintain correct winding.
    vec3 right = cross(vec3(0.0, 1.0, 0.0), forward);

    // Build world position: translate along right (X) and up (Y) from instance origin.
    vec3 worldPos = i_position
        + right        * (a_pos.x * i_scale)
        + vec3(0.0, 1.0, 0.0) * (a_pos.y * i_scale);

    gl_Position = u_projection * u_view * vec4(worldPos, 1.0);
    v_uv = a_uv;

    float dist = length(u_cameraPos - i_position);
    v_alpha = 1.0 - smoothstep(u_fadeStart, u_fadeEnd, dist);
}
)glsl";

static const char* const VEGETATION_FRAG_SOURCE = R"glsl(
#version 330 core

in vec2  v_uv;
in float v_alpha;

uniform sampler2D u_grassTex;
uniform int       u_hasTexture;
uniform vec3      u_lightColor;
uniform vec3      u_ambientColor;

out vec4 fragColor;

void main() {
    vec4 color;
    if (u_hasTexture != 0) {
        color = texture(u_grassTex, v_uv);
    } else {
        // Solid opaque green fallback — matches the 4x4 green fallback texture.
        color = vec4(0.2, 0.6, 0.15, 1.0);
    }

    // Alpha-test: discard fragments below threshold (includes distance fade).
    if (color.a * v_alpha < 0.5) {
        discard;
    }

    // Simple diffuse lighting (no specular — grass is a flat billboard).
    color.rgb *= (u_ambientColor + u_lightColor * 0.7);
    color.a    = 1.0;
    fragColor  = color;
}
)glsl";

// ==================== Library Implementation ====================

struct ShaderPair {
    const char* vertSource;
    const char* fragSource;
    const char* debugName;
};

static const ShaderPair PAIRS[] = {
    { SOLID_VERT_SOURCE,               SOLID_FRAG_SOURCE,               "solid"                 },
    { TEXTURED_VERT_SOURCE,            TEXTURED_FRAG_SOURCE,            "textured"              },
    { SPRITE_VERT_SOURCE,              SPRITE_FRAG_SOURCE,              "sprite"                },
    { MESH_BLINN_PHONG_VERT_SOURCE,    MESH_BLINN_PHONG_FRAG_SOURCE,    "mesh_blinn_phong"      },
    { SHADOW_DEPTH_VERT_SOURCE,        SHADOW_DEPTH_FRAG_SOURCE,        "shadow_depth"          },
    { SKYBOX_VERT_SOURCE,              SKYBOX_FRAG_SOURCE,              "skybox"                },
    { MESH_SKINNED_VERT_SOURCE,        MESH_BLINN_PHONG_FRAG_SOURCE,    "mesh_skinned"          },
    { SHADOW_DEPTH_SKINNED_VERT_SOURCE, SHADOW_DEPTH_FRAG_SOURCE,       "shadow_depth_skinned"  },
    { MESH_BLINN_PHONG_VERT_SOURCE,    MESH_PBR_FRAG_SOURCE,            "mesh_pbr"              },
    { MESH_PBR_SKINNED_VERT_SOURCE,    MESH_PBR_FRAG_SOURCE,            "mesh_pbr_skinned"      },
    { FULLSCREEN_VERT_SOURCE,          POST_THRESHOLD_FRAG_SOURCE,      "post_threshold"        },
    { FULLSCREEN_VERT_SOURCE,          POST_BLUR_FRAG_SOURCE,           "post_blur"             },
    { FULLSCREEN_VERT_SOURCE,          POST_FINAL_FRAG_SOURCE,          "post_final"            },
    { MESH_BLINN_PHONG_INSTANCED_VERT_SOURCE, MESH_BLINN_PHONG_FRAG_SOURCE, "mesh_blinn_phong_instanced" },
    { MESH_PBR_INSTANCED_VERT_SOURCE,  MESH_PBR_FRAG_SOURCE,            "mesh_pbr_instanced"    },
    { SHADOW_DEPTH_INSTANCED_VERT_SOURCE, SHADOW_DEPTH_FRAG_SOURCE,     "shadow_depth_instanced"},
    { FULLSCREEN_VERT_SOURCE,          POST_FXAA_FRAG_SOURCE,           "post_fxaa"             },
    { FULLSCREEN_VERT_SOURCE,          SSAO_PASS_FRAG_SOURCE,           "ssao_pass"             },
    { FULLSCREEN_VERT_SOURCE,          SSAO_BLUR_FRAG_SOURCE,           "ssao_blur"             },
    { TERRAIN_VERT_SOURCE,             TERRAIN_FRAG_SOURCE,             "terrain"               },
    { WATER_VERT_SOURCE,               WATER_FRAG_SOURCE,               "water"                 },
    { VEGETATION_VERT_SOURCE,          VEGETATION_FRAG_SOURCE,          "vegetation"            },
};
static_assert(sizeof(PAIRS) / sizeof(PAIRS[0]) == static_cast<u32>(BuiltinShader::COUNT));

bool initShaderLibrary(ShaderLibrary& library) {
    for (u32 i = 0; i < static_cast<u32>(BuiltinShader::COUNT); ++i) {
        rhi::ShaderDesc desc;
        desc.vertexSource   = PAIRS[i].vertSource;
        desc.fragmentSource = PAIRS[i].fragSource;
        desc.debugName      = PAIRS[i].debugName;

        library.handles[i] = rhi::createShader(desc);
        if (!rhi::isValid(library.handles[i])) {
            FFE_LOG_ERROR("Shader", "Failed to compile built-in shader: %s", PAIRS[i].debugName);
            return false;
        }
    }

    // Register post-process shader handles so the post-process pipeline can use them.
    setPostProcessShaders(
        library.handles[static_cast<u32>(BuiltinShader::POST_THRESHOLD)],
        library.handles[static_cast<u32>(BuiltinShader::POST_BLUR)],
        library.handles[static_cast<u32>(BuiltinShader::POST_FINAL)]);

    // Register FXAA shader handle for anti-aliasing post-process pass.
    setFxaaShader(library.handles[static_cast<u32>(BuiltinShader::POST_FXAA)]);

    // Register SSAO shader handles for the SSAO pipeline.
    setSSAOShaders(
        library.handles[static_cast<u32>(BuiltinShader::SSAO_PASS)],
        library.handles[static_cast<u32>(BuiltinShader::SSAO_BLUR)]);

    // Register water shader handle for the water rendering pipeline.
    setWaterShader(library.handles[static_cast<u32>(BuiltinShader::WATER)]);

    FFE_LOG_INFO("Shader", "Loaded %u built-in shaders", static_cast<u32>(BuiltinShader::COUNT));
    return true;
}

void shutdownShaderLibrary(ShaderLibrary& library) {
    for (u32 i = 0; i < static_cast<u32>(BuiltinShader::COUNT); ++i) {
        if (rhi::isValid(library.handles[i])) {
            rhi::destroyShader(library.handles[i]);
            library.handles[i] = {};
        }
    }
}

} // namespace ffe::renderer
