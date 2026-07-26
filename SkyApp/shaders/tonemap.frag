#version 450

layout(set = 0, binding = 0) uniform sampler2D hdrTex;
layout(set = 0, binding = 1) uniform sampler2D bloom;

layout(push_constant) uniform Push {
    float exposure;
    float bloomIntensity;
} pc;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

vec3 ACESFilm(vec3 x) {
    return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0);
}

void main() {
    vec2 uv = inNdc * 0.5 + 0.5;
    vec3 hdr = texture(hdrTex, uv).rgb;
    vec3 blm = texture(bloom,  uv).rgb;
    vec3 c = (hdr + blm * pc.bloomIntensity) * pc.exposure;
    outColor = vec4(ACESFilm(c), 1.0);
}