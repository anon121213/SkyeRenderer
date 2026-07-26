#version 450

layout(push_constant) uniform Push {
    vec2 filterRadius;
} pc;

layout(set = 0, binding = 0) uniform sampler2D inTexture;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = inUV * 0.5 + 0.5;
    vec2 r  = pc.filterRadius;

    vec3 a = texture(inTexture, uv + vec2(-r.x,  r.y)).rgb;
    vec3 b = texture(inTexture, uv + vec2( 0.0,  r.y)).rgb;
    vec3 c = texture(inTexture, uv + vec2( r.x,  r.y)).rgb;
    vec3 d = texture(inTexture, uv + vec2(-r.x,  0.0)).rgb;
    vec3 e = texture(inTexture, uv).rgb;                        // center
    vec3 f = texture(inTexture, uv + vec2( r.x,  0.0)).rgb;
    vec3 g = texture(inTexture, uv + vec2(-r.x, -r.y)).rgb;
    vec3 h = texture(inTexture, uv + vec2( 0.0, -r.y)).rgb;
    vec3 i = texture(inTexture, uv + vec2( r.x, -r.y)).rgb;

    vec3 tent = (e*4.0 + (b+d+f+h)*2.0 + (a+c+g+i)*1.0) / 16.0;   // 1-2-1 / 2-4-2 / 1-2-1
    outColor = vec4(tent, 1.0);
}