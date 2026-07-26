#version 450

layout(push_constant) uniform Push {
    mat4 invViewProj;
    vec4 camPos;
} pc;

layout(set = 0, binding = 0) uniform sampler2D envMap;
layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

void main() {
    vec4 world = pc.invViewProj * vec4(inNdc, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w - pc.camPos.xyz);
    float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;   // азимут
    float v = acos(clamp(dir.y, -1.0, 1.0)) / PI;      // высота (y=1 вверх → v=0 верх картинки)
    outColor = vec4(texture(envMap, vec2(u, v)).rgb, 1.0);
}