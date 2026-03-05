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
