#version 450

layout(push_constant) uniform Push {
    vec2 texelSize;
    float threshold;
    int isFirstPass;
} pc;

layout(set = 0, binding = 0) uniform sampler2D inTexture;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

vec3 QuadraticThreshold(vec3 color, float threshold, vec3 curve) {
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));

    float rq = clamp(brightness - curve.x, 0.0, curve.y);
    rq = curve.z * rq * rq;

    float factor = max(brightness - threshold, rq) / max(brightness, 1e-4);
    return color * factor;
}

void main() {
    vec2 uv = inUV * 0.5 + 0.5;
    vec2 t  = pc.texelSize;          // source texel size — FULL, not halved

    // 13-tap COD:AW downsample — samples a 4x4 neighborhood
    vec3 a = texture(inTexture, uv + t * vec2(-2.0,  2.0)).rgb;
    vec3 b = texture(inTexture, uv + t * vec2( 0.0,  2.0)).rgb;
    vec3 c = texture(inTexture, uv + t * vec2( 2.0,  2.0)).rgb;

    vec3 d = texture(inTexture, uv + t * vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(inTexture, uv + t * vec2( 0.0,  0.0)).rgb;   // center
    vec3 f = texture(inTexture, uv + t * vec2( 2.0,  0.0)).rgb;

    vec3 g = texture(inTexture, uv + t * vec2(-2.0, -2.0)).rgb;
    vec3 h = texture(inTexture, uv + t * vec2( 0.0, -2.0)).rgb;
    vec3 i = texture(inTexture, uv + t * vec2( 2.0, -2.0)).rgb;

    vec3 j = texture(inTexture, uv + t * vec2(-1.0,  1.0)).rgb;
    vec3 k = texture(inTexture, uv + t * vec2( 1.0,  1.0)).rgb;
    vec3 l = texture(inTexture, uv + t * vec2(-1.0, -1.0)).rgb;
    vec3 m = texture(inTexture, uv + t * vec2( 1.0, -1.0)).rgb;

    vec3 color = e * 0.125;                 // center
    color += (a + c + g + i) * 0.03125;     // far corners (±2)
    color += (b + d + f + h) * 0.0625;      // far edges   (±2)
    color += (j + k + l + m) * 0.125;       // inner 2x2   (±1)

    if (pc.isFirstPass == 1) {
        color = QuadraticThreshold(color, pc.threshold, vec3(pc.threshold - 0.1, 0.2, 0.25));
        color = min(color, vec3(20000.0));   // cap bloom source below fp16/accumulation overflow
    }

    outColor = vec4(color, 1.0);
}