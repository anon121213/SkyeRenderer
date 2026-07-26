#version 450
layout(set = 0, binding = 0) uniform sampler2D envMap;
layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;
const float PI = 3.14159265359;

vec2 dirToUv(vec3 d) {
    return vec2(atan(d.z, d.x) / (2.0*PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
}

void main() {
    vec2 uv = inNdc * 0.5 + 0.5;
    float phi   = (uv.x - 0.5) * 2.0 * PI;      // texel → направление N (инверсия skybox-маппинга)
    float theta = uv.y * PI;
    vec3 N = vec3(sin(theta)*cos(phi), cos(theta), sin(theta)*sin(phi));

    vec3 up    = abs(N.y) < 0.999 ? vec3(0,1,0) : vec3(1,0,0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    vec3 irradiance = vec3(0.0);
    float samples = 0.0;
    const float delta = 0.05;   // coarser — irradiance is very low-freq; keeps the bake within GPU time budget
    for (float p = 0.0; p < 2.0*PI; p += delta) {
        for (float t = 0.0; t < 0.5*PI; t += delta) {
            vec3 tan = vec3(sin(t)*cos(p), sin(t)*sin(p), cos(t));      // полусфера в tangent space
            vec3 world = tan.x*right + tan.y*up + tan.z*N;
            irradiance += texture(envMap, dirToUv(world)).rgb * cos(t) * sin(t);
            samples += 1.0;
        }
    }
    outColor = vec4(PI * irradiance / samples, 1.0);
}