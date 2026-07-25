#version 450

layout(set = 0, binding = 0) uniform sampler2D albedo;
layout(set = 0, binding = 1) uniform sampler2D metalRough;
layout(set = 0, binding = 2) uniform sampler2D normalMap;

layout(set = 0, binding = 3) uniform sceneData {
    vec3 lightDir;
    vec3 lightColor;
    vec3 camPos;
} scene;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in float fragHandedness;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// D — GGX / Trowbridge-Reitz
float calculateD(vec3 N, vec3 H, float roughness){
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH*NdotH*(a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom * denom);
    return D;
}

// G — Smith + Schlick-GGX (k для direct lighting)
float calculateG(float NdotV, float NdotL, float roughness){
    float k = (roughness+1.0)*(roughness+1.0) / 8.0;
    float gv = NdotV / (NdotV*(1.0-k) + k);
    float gl = NdotL / (NdotL*(1.0-k) + k);
    float G = gv * gl;
    return G;
}

// F — Fresnel-Schlick
vec3 calculateF(vec3 F0, vec3 H, vec3 V){
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    return F;
}

void main() {
    vec2 mr = texture(metalRough, fragUv).gb;
    float roughness = mr.x;
    float metallic = mr.y;

    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T) * fragHandedness;
    mat3 TBN = mat3(T, B, N);
    vec3 nTan = texture(normalMap, fragUv).rgb * 2.0 - 1.0;
    N = normalize(TBN * nTan);
    vec3 V = normalize(scene.camPos - fragWorldPos);
    vec3 L = normalize(scene.lightDir);
    vec3 H = normalize(V + L);
    vec3 albedoColor = texture(albedo, fragUv).rgb;

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L),0.0);
    vec3 F0 = mix(vec3(0.04), albedoColor, metallic);

    float D = calculateD(N, H, roughness);
    float G = calculateG(NdotV, NdotL, roughness);
    vec3 F = calculateF(F0, H, V);

    vec3 spec = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 Lo = (kD * albedoColor / PI + spec) * scene.lightColor * NdotL;
    vec3 color = Lo + albedoColor * 0.03;

    outColor = vec4(color, 1.0);
}