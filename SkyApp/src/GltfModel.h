#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Vertex layout shared with the shader (matches attribute locations 0..3 in
// triangle.vert). Interleaved: our choice — glTF stores attributes separately
// and the loader packs them into this struct.
struct Vertex
{
  float pos[3];
  float uv[2];
  float normal[3];
  float tangent[4];   // xyz = tangent direction, w = handedness (+1/-1). glTF stores TANGENT exactly like this.
};

// One decoded texture image, tightly packed as RGBA8 (width*height*4 bytes).
struct ImageData
{
  std::vector<uint8_t> pixels;
  uint32_t width  = 0;
  uint32_t height = 0;
};

// A single-mesh glTF model with its three PBR material maps decoded and ready to upload.
struct GltfModel
{
  std::vector<Vertex>   vertices;
  std::vector<uint32_t> indices;
  ImageData albedo;       // baseColorTexture       — sRGB colour
  ImageData metalRough;   // metallicRoughnessTexture — linear, G=roughness B=metallic (matches our shader)
  ImageData normal;       // normalTexture          — linear tangent-space normals
};

// Loads the first mesh + its material maps from a .glb/.gltf file.
// Throws std::runtime_error on failure.
GltfModel loadGltf(const std::string& path);
