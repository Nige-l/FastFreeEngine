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

// ==================== Library Implementation ====================

struct ShaderPair {
    const char* vertSource;
    const char* fragSource;
    const char* debugName;
};

static const ShaderPair PAIRS[] = {
    { SOLID_VERT_SOURCE,    SOLID_FRAG_SOURCE,    "solid"    },
    { TEXTURED_VERT_SOURCE, TEXTURED_FRAG_SOURCE, "textured" },
    { SPRITE_VERT_SOURCE,   SPRITE_FRAG_SOURCE,   "sprite"   },
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
