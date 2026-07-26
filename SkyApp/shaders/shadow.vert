#version 450

layout(push_constant) uniform Push { mat4 lightMVP; } pc;
layout(location = 0) in vec3 inPos;

void main() {
    gl_Position = pc.lightMVP * vec4(inPos, 1.0);
}