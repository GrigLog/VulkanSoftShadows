#version 460

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inWorldPosition;
layout(location = 0) out vec4 outFragmentColor;

void main() {
    outFragmentColor = inColor;
}
