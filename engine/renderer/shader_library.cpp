#include "renderer/shader_library.h"
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

out vec3 v_texCoord;

void main() {
    v_texCoord = a_position;
    vec4 pos = u_projection * u_viewNoTranslation * vec4(a_position, 1.0);
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
