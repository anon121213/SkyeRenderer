#include "GltfModel.h"

// tiny_gltf pulls in stb_image for texture decoding. These three defines must
// live in exactly ONE translation unit — this one.
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{

// A glTF accessor points into a bufferView, which points into a buffer.
// Returns the address of element 0 plus the byte stride between elements
// (stride matters because attributes may be interleaved inside one buffer).
const unsigned char* accessorPtr(const tinygltf::Model& model, int accessorIndex,
                                 size_t& outStride, size_t& outCount)
{
  const tinygltf::Accessor&   acc  = model.accessors[accessorIndex];
  const tinygltf::BufferView& view = model.bufferViews[acc.bufferView];
  const tinygltf::Buffer&     buf  = model.buffers[view.buffer];
  outStride = static_cast<size_t>(acc.ByteStride(view));   // bytes between consecutive elements
  outCount  = acc.count;                                    // number of elements
  return buf.data.data() + view.byteOffset + acc.byteOffset;
}

// Copy a tinygltf image (already decompressed to raw pixels by stb) into a
// tightly-packed RGBA8 buffer, expanding RGB -> RGBA when needed.
ImageData toImageData(const tinygltf::Image& img)
{
  if (img.width <= 0 || img.height <= 0)
    throw std::runtime_error("glTF image has no decoded pixels (unsupported encoding?)");

  ImageData out;
  out.width  = static_cast<uint32_t>(img.width);
  out.height = static_cast<uint32_t>(img.height);
  const size_t texels = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
  out.pixels.resize(texels * 4);

  if (img.component == 4)
  {
    std::memcpy(out.pixels.data(), img.image.data(), out.pixels.size());
  }
  else if (img.component == 3)
  {
    for (size_t i = 0; i < texels; ++i)
    {
      out.pixels[i * 4 + 0] = img.image[i * 3 + 0];
      out.pixels[i * 4 + 1] = img.image[i * 3 + 1];
      out.pixels[i * 4 + 2] = img.image[i * 3 + 2];
      out.pixels[i * 4 + 3] = 255;
    }
  }
  else
  {
    throw std::runtime_error("glTF image has unsupported channel count");
  }
  return out;
}

// A node's local transform: either an explicit 4x4 matrix or T*R*S.
glm::mat4 nodeLocalMatrix(const tinygltf::Node& node)
{
  if (node.matrix.size() == 16)
  {
    glm::mat4 m(1.0f);
    for (int i = 0; i < 16; ++i)
      glm::value_ptr(m)[i] = static_cast<float>(node.matrix[i]);   // glTF & glm are both column-major
    return m;
  }

  glm::mat4 m(1.0f);
  if (node.translation.size() == 3)
    m = glm::translate(m, glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
  if (node.rotation.size() == 4)
    m *= glm::mat4_cast(glm::quat(static_cast<float>(node.rotation[3]),   // glm::quat(w, x, y, z)
                                  static_cast<float>(node.rotation[0]),   // glTF stores rotation as [x, y, z, w]
                                  static_cast<float>(node.rotation[1]),
                                  static_cast<float>(node.rotation[2])));
  if (node.scale.size() == 3)
    m = glm::scale(m, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
  return m;
}

} // namespace

GltfModel loadGltf(const std::string& path)
{
  tinygltf::TinyGLTF loader;
  tinygltf::Model    model;
  std::string        err, warn;

  if (!loader.LoadBinaryFromFile(&model, &err, &warn, path))   // .glb container
    throw std::runtime_error("glTF load failed: " + (err.empty() ? path : err));

  if (model.meshes.empty() || model.meshes[0].primitives.empty())
    throw std::runtime_error("glTF: no mesh/primitive found");

  const int                  meshIndex = 0;
  const tinygltf::Primitive& prim      = model.meshes[meshIndex].primitives[0];

  // Bake the node's orientation into the vertices (glTF authors models Z-up and
  // rotate them to Y-up via a node transform; we have no node matrix in the shader).
  glm::mat4 nodeMat(1.0f);
  for (const tinygltf::Node& n : model.nodes)
    if (n.mesh == meshIndex) { nodeMat = nodeLocalMatrix(n); break; }
  const glm::mat3 nodeNormalMat = glm::mat3(nodeMat);   // rotation part — for normals & tangents

  // --- vertex attribute accessors (TANGENT is optional — many models omit it) ---
  auto attr = [&](const char* name) -> int {
    auto it = prim.attributes.find(name);
    return it == prim.attributes.end() ? -1 : it->second;
  };
  const int posIdx  = attr("POSITION");
  const int uvIdx   = attr("TEXCOORD_0");
  const int normIdx = attr("NORMAL");
  const int tanIdx  = attr("TANGENT");   // -1 if absent -> we compute tangents ourselves below
  if (posIdx < 0 || uvIdx < 0 || normIdx < 0)
    throw std::runtime_error("glTF primitive missing POSITION/TEXCOORD_0/NORMAL");

  size_t sPos, sUv, sNorm, count, dummy;
  const unsigned char* pPos  = accessorPtr(model, posIdx,  sPos,  count);
  const unsigned char* pUv   = accessorPtr(model, uvIdx,   sUv,   dummy);
  const unsigned char* pNorm = accessorPtr(model, normIdx, sNorm, dummy);

  GltfModel out;
  out.vertices.resize(count);
  for (size_t i = 0; i < count; ++i)   // raw local-space attributes; node baking happens after tangents
  {
    Vertex& v = out.vertices[i];
    std::memcpy(v.pos,    pPos  + i * sPos,  sizeof(float) * 3);
    std::memcpy(v.uv,     pUv   + i * sUv,   sizeof(float) * 2);
    std::memcpy(v.normal, pNorm + i * sNorm, sizeof(float) * 3);
  }

  // --- indices (normalise any component type to uint32) ---
  if (prim.indices < 0)
    throw std::runtime_error("glTF primitive has no indices");
  {
    const tinygltf::Accessor& acc = model.accessors[prim.indices];
    size_t sIdx, idxCount;
    const unsigned char* pIdx = accessorPtr(model, prim.indices, sIdx, idxCount);
    out.indices.resize(idxCount);
    for (size_t i = 0; i < idxCount; ++i)
    {
      const unsigned char* e = pIdx + i * sIdx;
      switch (acc.componentType)
      {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   out.indices[i] = *reinterpret_cast<const uint32_t*>(e); break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: out.indices[i] = *reinterpret_cast<const uint16_t*>(e); break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  out.indices[i] = *e; break;
        default: throw std::runtime_error("glTF: unsupported index component type");
      }
    }
  }

  // --- tangents: read from file if present, otherwise compute them (Lengyel accumulation) ---
  if (tanIdx >= 0)
  {
    size_t sTan;
    const unsigned char* pTan = accessorPtr(model, tanIdx, sTan, dummy);
    for (size_t i = 0; i < count; ++i)
      std::memcpy(out.vertices[i].tangent, pTan + i * sTan, sizeof(float) * 4);
  }
  else
  {
    // Same math you wrote for the cube, generalised to an indexed mesh:
    // accumulate a per-triangle tangent/bitangent onto each of its 3 vertices,
    // then orthonormalise against the normal and recover the handedness sign.
    std::vector<glm::vec3> tanAcc(count, glm::vec3(0.0f));
    std::vector<glm::vec3> bitAcc(count, glm::vec3(0.0f));
    for (size_t t = 0; t + 2 < out.indices.size(); t += 3)
    {
      const uint32_t i0 = out.indices[t], i1 = out.indices[t + 1], i2 = out.indices[t + 2];
      const glm::vec3 p0(out.vertices[i0].pos[0], out.vertices[i0].pos[1], out.vertices[i0].pos[2]);
      const glm::vec3 p1(out.vertices[i1].pos[0], out.vertices[i1].pos[1], out.vertices[i1].pos[2]);
      const glm::vec3 p2(out.vertices[i2].pos[0], out.vertices[i2].pos[1], out.vertices[i2].pos[2]);
      const glm::vec2 w0(out.vertices[i0].uv[0], out.vertices[i0].uv[1]);
      const glm::vec2 w1(out.vertices[i1].uv[0], out.vertices[i1].uv[1]);
      const glm::vec2 w2(out.vertices[i2].uv[0], out.vertices[i2].uv[1]);
      const glm::vec3 e1 = p1 - p0, e2 = p2 - p0;
      const glm::vec2 d1 = w1 - w0, d2 = w2 - w0;
      const float denom = d1.x * d2.y - d2.x * d1.y;
      const float r = (std::fabs(denom) < 1e-8f) ? 0.0f : 1.0f / denom;
      const glm::vec3 T = (e1 * d2.y - e2 * d1.y) * r;
      const glm::vec3 B = (e2 * d1.x - e1 * d2.x) * r;
      tanAcc[i0] += T; tanAcc[i1] += T; tanAcc[i2] += T;
      bitAcc[i0] += B; bitAcc[i1] += B; bitAcc[i2] += B;
    }
    for (size_t i = 0; i < count; ++i)
    {
      const glm::vec3 n(out.vertices[i].normal[0], out.vertices[i].normal[1], out.vertices[i].normal[2]);
      glm::vec3 t = tanAcc[i] - n * glm::dot(n, tanAcc[i]);        // Gram-Schmidt against the normal
      const float len = glm::length(t);
      t = (len > 1e-8f) ? t / len : glm::vec3(1.0f, 0.0f, 0.0f);   // degenerate-UV fallback
      const float handed = (glm::dot(glm::cross(n, t), bitAcc[i]) < 0.0f) ? -1.0f : 1.0f;
      out.vertices[i].tangent[0] = t.x; out.vertices[i].tangent[1] = t.y;
      out.vertices[i].tangent[2] = t.z; out.vertices[i].tangent[3] = handed;
    }
  }

  // --- bake node orientation into every vertex (glTF authors Z-up, rotates to Y-up via node) ---
  for (Vertex& v : out.vertices)
  {
    glm::vec3 p (v.pos[0],     v.pos[1],     v.pos[2]);
    glm::vec3 n (v.normal[0],  v.normal[1],  v.normal[2]);
    glm::vec3 tg(v.tangent[0], v.tangent[1], v.tangent[2]);
    p  = glm::vec3(nodeMat * glm::vec4(p, 1.0f));   // position: a point (w = 1)
    n  = glm::normalize(nodeNormalMat * n);         // normal:   a direction
    tg = glm::normalize(nodeNormalMat * tg);        // tangent:  a direction
    v.pos[0]=p.x;     v.pos[1]=p.y;     v.pos[2]=p.z;
    v.normal[0]=n.x;  v.normal[1]=n.y;  v.normal[2]=n.z;
    v.tangent[0]=tg.x; v.tangent[1]=tg.y; v.tangent[2]=tg.z;   // tangent.w (handedness) unchanged
  }

  // --- material maps ---
  if (prim.material < 0)
    throw std::runtime_error("glTF primitive has no material");
  const tinygltf::Material& mat = model.materials[prim.material];

  auto imageOf = [&](int textureIndex) -> int {
    return textureIndex >= 0 ? model.textures[textureIndex].source : -1;
  };
  const int albedoImg = imageOf(mat.pbrMetallicRoughness.baseColorTexture.index);
  const int mrImg     = imageOf(mat.pbrMetallicRoughness.metallicRoughnessTexture.index);
  const int normalImg = imageOf(mat.normalTexture.index);
  if (albedoImg < 0 || mrImg < 0 || normalImg < 0)
    throw std::runtime_error("glTF material missing baseColor/metallicRoughness/normal texture");

  out.albedo     = toImageData(model.images[albedoImg]);
  out.metalRough = toImageData(model.images[mrImg]);
  out.normal     = toImageData(model.images[normalImg]);

  return out;
}

HdrImage loadHdr(const std::string& path)
{
  int w, h, comp;
  float* data = stbi_loadf(path.c_str(), &w, &h, &comp, 4);   // форсим RGBA (4 канала)
  if (!data) throw std::runtime_error("Failed to load HDR: " + path);

  HdrImage out;
  out.width  = static_cast<uint32_t>(w);
  out.height = static_cast<uint32_t>(h);
  out.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
  stbi_image_free(data);
  return out;
}