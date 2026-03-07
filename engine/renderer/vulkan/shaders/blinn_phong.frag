#version 450

layout(set = 0, binding = 1) uniform LightUBO {
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    vec4 cameraPos;
} light;

layout(set = 0, binding = 2) uniform sampler2D diffuseMap;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-light.lightDir.xyz);
    vec3 V = normalize(light.cameraPos.xyz - fragWorldPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 32.0);

    vec3 texColor = texture(diffuseMap, fragTexCoord).rgb;
    vec3 ambient = light.ambientColor.rgb * texColor;
    vec3 diffuse = light.lightColor.rgb * diff * texColor;
    vec3 specular = light.lightColor.rgb * spec * vec3(0.3);

    outColor = vec4(ambient + diffuse + specular, 1.0);
}
