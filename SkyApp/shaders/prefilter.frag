#version 450
layout(set = 0, binding = 0) uniform sampler2D envMap;
layout(push_constant) uniform Push { float roughness; } pc;
layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;
const float PI = 3.14159265359;

vec2 dirToUv(vec3 d) { return vec2(atan(d.z, d.x)/(2.0*PI)+0.5, acos(clamp(d.y,-1.0,1.0))/PI); }
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint i, uint N) { return vec2(float(i)/float(N), RadicalInverse_VdC(i)); }
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness*roughness;
    float phi = 2.0*PI*Xi.x;
    float cosT = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0)*Xi.y));
    float sinT = sqrt(1.0 - cosT*cosT);
    vec3 H = vec3(cos(phi)*sinT, sin(phi)*sinT, cosT);
    vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent*H.x + bitangent*H.y + N*H.z);
}

void main() {
    vec2 uv = inNdc*0.5 + 0.5;
    float phi = (uv.x-0.5)*2.0*PI;
    float theta = uv.y*PI;
    vec3 N = vec3(sin(theta)*cos(phi), cos(theta), sin(theta)*sin(phi));
    vec3 R = N, V = N;                          // допущение prefilter: R = V = N

    const uint SAMPLES = 128u;
    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;
    for (uint i = 0u; i < SAMPLES; ++i) {
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H = ImportanceSampleGGX(Xi, N, pc.roughness);
        vec3 L = normalize(2.0*dot(V,H)*H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefiltered += texture(envMap, dirToUv(L)).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    outColor = vec4(prefiltered / max(totalWeight, 1e-4), 1.0);
}