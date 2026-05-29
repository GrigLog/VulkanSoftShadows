#version 460

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform SceneUbo {
    mat4 viewProjection;
    vec4 sunPosition;
    mat4 model[16];
    vec4 color[16];
} sceneUbo;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outWorldPosition;

void main() {
    uint objectIndex = gl_InstanceIndex;
    vec4 worldPosition = sceneUbo.model[objectIndex] * vec4(inPosition, 1.0);
    gl_Position = sceneUbo.viewProjection * worldPosition;
    outColor = sceneUbo.color[objectIndex];
    outWorldPosition = worldPosition.xyz;
}
