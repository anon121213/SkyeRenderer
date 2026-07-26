#version 450

layout(set = 0, binding = 0) uniform sampler2D albedo;
layout(set = 0, binding = 1) uniform sampler2D metalRough;
layout(set = 0, binding = 2) uniform sampler2D normalMap;
layout(set = 0, binding = 4) uniform sampler2D irradianceMap;
layout(set = 0, binding = 5) uniform sampler2D prefilteredMap;
layout(set = 0, binding = 6) uniform sampler2D brdfLut;
layout(set = 0, binding = 7) uniform sampler2D shadowMap;

layout(set = 0, binding = 3) uniform sceneData {
    vec3 sunDir;
    vec3 sunColor;
    vec3 pointPos;
    vec3 pointColor;
    vec3 camPos;
    mat4 lightVP;
} scene;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in float fragHandedness;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float calcShadow(vec4 lightSpacePos) {
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;   // perspective divide
    vec2 uv = proj.xy * 0.5 + 0.5;                      // xy [-1,1] → [0,1]
    float current = proj.z;                            // глубина пикселя в свете (Vulkan z∈[0,1])
    if (current > 1.0 || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    return 1.0;                                    // вне frustum света = освещён

    float bias = 0.0015;                               // против self-shadow acne
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y) {
        float d = texture(shadowMap, uv + vec2(x, y) * texel).r;
        lit += (current - bias > d) ? 0.0 : 1.0;       // глубже записанного → в тени
    }
    return lit / 9.0;                                  // среднее по 3×3 → мягкий край
}

vec2 dirToUv(vec3 d) {
    return vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

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

vec3 shade(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo, float roughness, float metallic) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float D = calculateD(N, H, roughness);
    float G = calculateG(NdotV, NdotL, roughness);
    vec3 F = calculateF(F0, H, V);
    vec3 spec = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    return (kD * albedo / PI + spec) * radiance * NdotL;
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
    vec3 albedoColor = texture(albedo, fragUv).rgb;

    float shadow = calcShadow(scene.lightVP * vec4(fragWorldPos, 1.0));
    vec3 Lo = shade(normalize(scene.sunDir), scene.sunColor, N, V, albedoColor, roughness, metallic) * shadow;

    vec3 toLight = scene.pointPos - fragWorldPos;
    float dist = length(toLight);
    vec3 Lp = toLight / dist;
    float atten = 1.0 / (dist * dist);
    Lo += shade(Lp, scene.pointColor * atten, N, V, albedoColor, roughness, metallic);

    // --- IBL ambient ---
    float NdotV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedoColor, metallic);
    vec3 F  = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metallic);

    // diffuse IBL
    vec3 diffuseIBL = texture(irradianceMap, dirToUv(N)).rgb * albedoColor;

    // specular IBL
    vec3 R = reflect(-V, N);
    const float MAX_LOD = 5.0;                                   // PREFILTER_MIPS - 1
    vec3 prefiltered = textureLod(prefilteredMap, dirToUv(R), roughness * MAX_LOD).rgb;
    vec2 brdf = texture(brdfLut, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefiltered * (F * brdf.x + brdf.y);

    vec3 ambient = kD * diffuseIBL + specularIBL;
    vec3 color = Lo + ambient;                                   // direct + IBL
    outColor = vec4(color, 1.0);
    outColor = vec4(color, 1.0);
}