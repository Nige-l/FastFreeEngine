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

static const char* const MESH_BLINN_PHONG_VERT_SOURCE = R"glsl(
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_model;
uniform mat4 u_viewProjection;
uniform mat3 u_normalMatrix;
uniform mat4 u_lightSpaceMatrix;

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
    gl_Position         = u_viewProjection * worldPos;
}
)glsl";

static const char* const MESH_BLINN_PHONG_FRAG_SOURCE = R"glsl(
#version 330 core

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_texcoord;
in vec4 v_fragPosLightSpace;

uniform sampler2D u_diffuseTexture;
uniform vec4      u_diffuseColor;
uniform vec3      u_lightDir;
uniform vec3      u_lightColor;
uniform vec3      u_ambientColor;
uniform vec3      u_viewPos;

// Shadow map uniforms — inactive when u_shadowsEnabled == 0
uniform sampler2D u_shadowMap;
uniform int       u_shadowsEnabled;
uniform float     u_shadowBias;

out vec4 fragColor;

void main() {
    vec3  norm      = normalize(v_normal);
    vec3  lightDir  = normalize(-u_lightDir);

    // Diffuse
    float diff      = max(dot(norm, lightDir), 0.0);
    vec3  diffuse   = diff * u_lightColor;

    // Specular (Blinn-Phong half-vector)
    vec3  viewDir   = normalize(u_viewPos - v_fragPos);
    vec3  halfDir   = normalize(lightDir + viewDir);
    float spec      = pow(max(dot(norm, halfDir), 0.0), 32.0);
    vec3  specular  = spec * u_lightColor * 0.3;

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

    // Combine: ambient is never shadowed; diffuse + specular scale with shadow factor
    vec4  texSample = texture(u_diffuseTexture, v_texcoord);
    vec3  lighting  = u_ambientColor + shadowFactor * (diffuse + specular);
    fragColor       = vec4(lighting, 1.0) * texSample * u_diffuseColor;
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

// ==================== Library Implementation ====================

struct ShaderPair {
    const char* vertSource;
    const char* fragSource;
    const char* debugName;
};

static const ShaderPair PAIRS[] = {
    { SOLID_VERT_SOURCE,               SOLID_FRAG_SOURCE,               "solid"            },
    { TEXTURED_VERT_SOURCE,            TEXTURED_FRAG_SOURCE,            "textured"         },
    { SPRITE_VERT_SOURCE,              SPRITE_FRAG_SOURCE,              "sprite"           },
    { MESH_BLINN_PHONG_VERT_SOURCE,    MESH_BLINN_PHONG_FRAG_SOURCE,    "mesh_blinn_phong" },
    { SHADOW_DEPTH_VERT_SOURCE,        SHADOW_DEPTH_FRAG_SOURCE,        "shadow_depth"     },
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
