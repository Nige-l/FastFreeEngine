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
