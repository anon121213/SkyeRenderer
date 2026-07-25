#include "../../src/skypch.h"

#include "SkyRHI/Device.h"
#include "SkyRHI/FrameGraph.h"
#include "Window.h"

#include <fstream>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

static std::vector<uint32_t> loadSpirv(const std::string& path)
{
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("Failed to open shader: " + path);

  const size_t sizeBytes = file.tellg();
  std::vector<uint32_t> buffer(sizeBytes / sizeof(uint32_t));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(buffer.data()), sizeBytes);
  return buffer;
}

struct
Vertex {
  float pos[3];
  float uv[2];
  float normal[3];
  float tangent[4];
};

struct PushConstants
{
  glm::mat4 mvp;
  glm::mat4 model;
};

struct SceneData
{
  glm::vec3 lightDir; float _pad0;
  glm::vec3 lightColor; float _pad1;
  glm::vec3 camPos; float _pad2;
};

int main()
{
  Sky::RHI::Log::init();
  SKY_RHI_INFO("SkyEngine starting up...");

  try
  {
    const Window window(1920, 1080, "Vulkan test window");
    const auto extensions = Window::getRequiredInstanceExtensions();

    const Sky::RHI::DeviceCreateInfo info{
      .backend = Sky::RHI::BackendType::Vulkan,
      .requiredInstanceExtensions = extensions,
      .surfaceFactory = [&window](void* instance) -> void* {
        return window.createSurface(static_cast<VkInstance>(instance));
      },
      .initialWindowWidth = window.width(),
      .initialWindowHeight = window.height(),
      .enableValidation = true,
    };

    Sky::RHI::Device device(info);

    auto pack = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint32_t {
      return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    };

    // перед device, или в try после device:
    constexpr uint32_t TEX = 256;
    std::vector<uint32_t> pixels(TEX * TEX);
    for (uint32_t y = 0; y < TEX; ++y)
      for (uint32_t x = 0; x < TEX; ++x)
      {
        const bool check = ((x / 32) + (y / 32)) % 2 == 0;
        pixels[y * TEX + x] = check ? 0xFFFFFFFF : 0xFF404040;   // белый / тёмно-серый (RGBA)
      }

    auto texture = device.createTexture({ Sky::RHI::Format::RGBA8_UNORM, TEX, TEX, Sky::RHI::TextureUsage::Sampled });
    device.uploadTextureData(texture, pixels.data(), pixels.size() * sizeof(uint32_t));

    std::vector<uint32_t> pixelsMetallic(TEX * TEX);
    for (uint32_t y = 0; y < TEX; ++y)
      for (uint32_t x = 0; x < TEX; ++x)
      {
        if (x < 128)
          pixelsMetallic[y * TEX + x] = pack(0, /*rough*/ 25, /*metal*/ 255, 255);
        else
          pixelsMetallic[y * TEX + x] = pack(0, /*rough*/ 204, /*metal*/ 0, 255);
      }

    auto textureMetallic = device.createTexture({ Sky::RHI::Format::RGBA8_UNORM, TEX, TEX, Sky::RHI::TextureUsage::Sampled });
    device.uploadTextureData(textureMetallic, pixelsMetallic.data(), pixelsMetallic.size() * sizeof(uint32_t));

    std::vector<uint32_t> normalPixels(TEX * TEX);
    const float freq = 6.0f * 2.0f * 3.14159265f;
    const float amp  = 0.5f;
    for (uint32_t y = 0; y < TEX; ++y)
      for (uint32_t x = 0; x < TEX; ++x) {
        float u = float(x)/TEX, v = float(y)/TEX;
        // наклон высотного поля h=amp*sin(freq*u)*sin(freq*v)
        float dhdu = amp*freq*cosf(freq*u)*sinf(freq*v);
        float dhdv = amp*freq*sinf(freq*u)*cosf(freq*v);
        glm::vec3 n = glm::normalize(glm::vec3(-dhdu, -dhdv, 1.0f));   // tangent-space нормаль
        glm::vec3 enc = n*0.5f + 0.5f;                                 // [-1,1] → [0,1]
        normalPixels[y*TEX+x] = pack(uint8_t(enc.x*255), uint8_t(enc.y*255), uint8_t(enc.z*255), 255);
      }

    auto textureNormal = device.createTexture({ Sky::RHI::Format::RGBA8_UNORM, TEX, TEX, Sky::RHI::TextureUsage::Sampled });
    device.uploadTextureData(textureNormal, normalPixels.data(), normalPixels.size() * sizeof(uint32_t));

    auto sampler = device.createSampler({});

    Sky::RHI::DescriptorSetLayoutDesc layoutDesc{};
    layoutDesc.bindings = {
      { 0, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // albedo
      { 1, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // metalRough
      { 2, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // normalMap
      { 3, Sky::RHI::DescriptorType::UniformBuffer,        Sky::RHI::ShaderStage::Fragment }, // scene
    };
    auto descLayout = device.createDescriptorSetLayout(layoutDesc);

    const auto sceneUBO = device.createBuffer({
      sizeof(SceneData),
      Sky::RHI::BufferUsage::Uniform,
      Sky::RHI::MemoryType::CpuToGpu});

    void* sceneMapped = device.mapBuffer(sceneUBO);

    auto descSet = device.createDescriptorSet(descLayout);
    device.updateDescriptorSetTexture(descSet, 0, texture, sampler);
    device.updateDescriptorSetTexture(descSet, 1, textureMetallic, sampler);
    device.updateDescriptorSetTexture(descSet, 2, textureNormal, sampler);
    device.updateDescriptorSetBuffer(descSet, 3, sceneUBO, sizeof(SceneData));

    auto vertCode = loadSpirv(std::string(SHADER_DIR) + "/triangle.vert.spv");
    auto fragCode = loadSpirv(std::string(SHADER_DIR) + "/triangle.frag.spv");

    auto vs = device.createShader({ vertCode.data(), vertCode.size() * sizeof(uint32_t) });
    auto fs = device.createShader({ fragCode.data(), fragCode.size() * sizeof(uint32_t) });

    const auto sw = device.defaultSwapchain();

    const std::vector<Sky::RHI::VertexAttribute> attrs = {
      { 0, Sky::RHI::Format::RGB32_SFLOAT,  offsetof(Vertex, pos) },     // location 0 = pos
      { 1, Sky::RHI::Format::RG32_SFLOAT,   offsetof(Vertex, uv)  },     // location 1 = uv
      { 2, Sky::RHI::Format::RGB32_SFLOAT,  offsetof(Vertex, normal) },  // location 2 = normal
      { 3, Sky::RHI::Format::RGBA32_SFLOAT, offsetof(Vertex, tangent) }  // location 3 = tangent
    };

    Sky::RHI::GraphicsPipelineDesc pipeDesc{};
    pipeDesc.vertexShader        = vs;
    pipeDesc.fragmentShader      = fs;
    pipeDesc.vertexStride        = sizeof(Vertex);
    pipeDesc.vertexAttributes    = attrs;
    pipeDesc.colorFormat         = device.swapchainFormat(sw);  // matching swapchain format
    pipeDesc.depthFormat         = Sky::RHI::Format::D32_SFLOAT;
    pipeDesc.pushConstantSize    = sizeof(PushConstants);
    pipeDesc.descriptorSetLayout = descLayout;

    const auto pipeline = device.createGraphicsPipeline(pipeDesc);

    Vertex verts[24] = {
      // front (z=-0.5)
      {{-0.5f,-0.5f,-0.5f},{0,0}, {0, 0, -1}}, {{ 0.5f,-0.5f,-0.5f},{1,0}, {0, 0, -1}}, {{ 0.5f, 0.5f,-0.5f},{1,1}, {0, 0, -1}}, {{-0.5f, 0.5f,-0.5f},{0,1}, {0, 0, -1}},
      // back (z=+0.5)
      {{ 0.5f,-0.5f, 0.5f},{0,0}, { 0, 0, 1}}, {{-0.5f,-0.5f, 0.5f},{1,0}, { 0, 0, 1}}, {{-0.5f, 0.5f, 0.5f},{1,1}, { 0, 0, 1}}, {{ 0.5f, 0.5f, 0.5f},{0,1}, { 0, 0, 1}},
      // left (x=-0.5)
      {{-0.5f,-0.5f, 0.5f},{0,0}, {-1, 0, 0}}, {{-0.5f,-0.5f,-0.5f},{1,0}, {-1, 0, 0}}, {{-0.5f, 0.5f,-0.5f},{1,1}, {-1, 0, 0}}, {{-0.5f, 0.5f, 0.5f},{0,1}, {-1, 0, 0}},
      // right (x=+0.5)
      {{ 0.5f,-0.5f,-0.5f},{0,0}, { 1, 0, 0}}, {{ 0.5f,-0.5f, 0.5f},{1,0}, { 1, 0, 0}}, {{ 0.5f, 0.5f, 0.5f},{1,1}, { 1, 0, 0}}, {{ 0.5f, 0.5f,-0.5f},{0,1}, { 1, 0, 0}},
      // top (y=+0.5)
      {{-0.5f, 0.5f,-0.5f},{0,0}, { 0, 1, 0}}, {{ 0.5f, 0.5f,-0.5f},{1,0}, { 0, 1, 0}}, {{ 0.5f, 0.5f, 0.5f},{1,1}, { 0, 1, 0}}, {{-0.5f, 0.5f, 0.5f},{0,1}, { 0, 1, 0}},
      // bottom (y=-0.5)
      {{-0.5f,-0.5f, 0.5f},{0,0}, {0, -1, 0}}, {{ 0.5f,-0.5f, 0.5f},{1,0}, {0, -1, 0}}, {{ 0.5f,-0.5f,-0.5f},{1,1}, {0, -1, 0}}, {{-0.5f,-0.5f,-0.5f},{0,1}, {0, -1, 0}},
    };

    for (int f = 0; f < 6; ++f) {
      Vertex& a = verts[f*4+0]; Vertex& b = verts[f*4+1]; Vertex& c = verts[f*4+2];
      glm::vec3 p0(a.pos[0],a.pos[1],a.pos[2]), p1(b.pos[0],b.pos[1],b.pos[2]), p2(c.pos[0],c.pos[1],c.pos[2]);
      glm::vec2 uv0(a.uv[0],a.uv[1]), uv1(b.uv[0],b.uv[1]), uv2(c.uv[0],c.uv[1]);
      glm::vec3 e1 = p1-p0, e2 = p2-p0;
      glm::vec2 d1 = uv1-uv0, d2 = uv2-uv0;
      float r = 1.0f / (d1.x*d2.y - d2.x*d1.y);
      glm::vec3 T  = glm::normalize((e1*d2.y - e2*d1.y) * r);
      glm::vec3 Bt = (e2*d1.x - e1*d2.x) * r;              // bitangent для определения знака
      glm::vec3 N(a.normal[0],a.normal[1],a.normal[2]);
      float w = (glm::dot(glm::cross(N, T), Bt) < 0.0f) ? -1.0f : 1.0f;   // handedness
      for (int i = 0; i < 4; ++i) {
        verts[f*4+i].tangent[0]=T.x; verts[f*4+i].tangent[1]=T.y; verts[f*4+i].tangent[2]=T.z; verts[f*4+i].tangent[3]=w;
      }
    }

    const uint16_t indices[36] = {
      0, 1, 2,  2, 3, 0,     // front
      4, 5, 6,  6, 7, 4,     // back
      8, 9,10, 10,11, 8,     // left
     12,13,14, 14,15,12,     // right
     16,17,18, 18,19,16,     // top
     20,21,22, 22,23,20,     // bottom
   };

    const auto ib = device.createBuffer({
      sizeof(indices),
      Sky::RHI::BufferUsage::Index | Sky::RHI::BufferUsage::TransferDst,
      Sky::RHI::MemoryType::GpuOnly });

    device.uploadBufferData(ib, indices, sizeof(indices));

    const auto vb = device.createBuffer({
       sizeof(verts),
       Sky::RHI::BufferUsage::Vertex | Sky::RHI::BufferUsage::TransferDst,
       Sky::RHI::MemoryType::GpuOnly });
    device.uploadBufferData(vb, verts, sizeof(verts));

    struct TriData
    {
      Sky::RHI::FGResource backbuffer;
      Sky::RHI::FGResource depth;
    };

    while (!window.shouldClose())
    {
      Window::pollEvents();

      device.beginFrame();

      const auto extent = device.swapchainExtent(sw);

      const float time   = static_cast<float>(glfwGetTime());
      const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

      glm::mat4 model = glm::rotate(glm::mat4(1.0f), time,
                              glm::normalize(glm::vec3(0.5f, 1.0f, 0.0f)));

      glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.0f),
                             glm::vec3(0.0f, 0.0f, 0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f));

      glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
      proj[1][1] *= -1.0f;

      glm::mat4 mvp = proj * view * model;

      PushConstants pushConstants = {
        .mvp = mvp,
        .model = model,
      };

      glm::vec3 lightPos = glm::vec3(cos(time) * 2.0f, 1.0f, sin(time) * 2.0f);

      SceneData scene{};                              // {} — зануляет паддинг тоже
      scene.lightDir   = glm::normalize(lightPos);    // направление НА свет
      scene.lightColor = glm::vec3(1.0f);
      scene.camPos     = glm::vec3(0.0f, 0.0f, 2.0f);
      memcpy(sceneMapped, &scene, sizeof(SceneData));

      Sky::RHI::FrameGraph fg(device);
      fg.addRasterPass<TriData>("Triangle",
        [&](Sky::RHI::PassBuilder& b, TriData& d) {
          d.backbuffer = b.writeColor(b.importSwapchain(sw));
          d.depth = b.writeDepth(b.createTexture("deph",
            { Sky::RHI::Format::D32_SFLOAT, extent.width, extent.height}));
        },
        [&](const TriData&, Sky::RHI::FGResources&, Sky::RHI::CommandList& cmd) {
          cmd.bindPipeline(pipeline);
          cmd.bindDescriptorSet(descSet);
          cmd.pushConstants(&pushConstants, sizeof(PushConstants));
          cmd.bindVertexBuffer(vb);
          cmd.bindIndexBuffer(ib, Sky::RHI::IndexType::UInt16);
          cmd.setViewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));
          cmd.setScissor(0, 0, extent.width, extent.height);
          cmd.drawIndexed(sizeof(indices) / sizeof(uint16_t));
        });

      fg.compile();
      device.execute(fg);
      device.endFrame();
    }

    device.waitIdle();

    device.unmapBuffer(sceneUBO);
    device.destroyBuffer(sceneUBO);

    device.destroyBuffer(ib);
    device.destroyBuffer(vb);
  }
  catch (const std::exception& e)
  {
    SKY_RHI_CRITICAL("Fatal error: {}", e.what());
    return -1;
  }

  Sky::RHI::Log::shutdown();
  return 0;
}
