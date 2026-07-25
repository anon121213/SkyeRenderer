#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 model;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out float fragHandedness;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUv = inUv;
    fragNormal = mat3(pc.model) * inNormal;
    fragWorldPos = vec3(pc.model * vec4(inPos, 1.0f));
    fragTangent = mat3(pc.model) * inTangent.xyz;
    fragHandedness = inTangent.w;
}