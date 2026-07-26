#include "../../src/skypch.h"

#include "SkyRHI/Device.h"
#include "SkyRHI/FrameGraph.h"
#include "SkyRHI/ImGuiSupport.h"
#include "Window.h"
#include "GltfModel.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

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

struct PushConstants
{
  glm::mat4 mvp;
  glm::mat4 model;
};

struct SkyPush {
  glm::mat4 invViewProj;
  glm::vec4 camPos;
};

struct SceneData {
  glm::vec3 sunDir;     float _pad0;
  glm::vec3 sunColor;   float _pad1;
  glm::vec3 pointPos;   float _pad2;
  glm::vec3 pointColor; float _pad3;
  glm::vec3 camPos;     float _pad4;
};

static void InitImGui(const Window& window, Sky::RHI::Device& device, VkDescriptorPool* imguiPool, ImGui_ImplVulkan_InitInfo* imguiInit)
{
  // --- ImGui init (Vulkan backend via dynamic rendering; GLFW input backend) ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForVulkan(window.handle(), true);

  const auto imguiInfo = Sky::RHI::imguiVulkanInitInfo(device);
  static VkFormat imguiColorFormat = imguiInfo.colorFormat;   // must outlive Init (pointer below)

  // ImGui 1.91.5 needs a descriptor pool provided (auto-pool came in a later version)
  VkDescriptorPoolSize imguiPoolSizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 } };
  VkDescriptorPoolCreateInfo imguiPoolInfo{};
  imguiPoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  imguiPoolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  imguiPoolInfo.maxSets       = 16;
  imguiPoolInfo.poolSizeCount = 1;
  imguiPoolInfo.pPoolSizes    = imguiPoolSizes;
  vkCreateDescriptorPool(imguiInfo.device, &imguiPoolInfo, nullptr, imguiPool);

  imguiInit->Instance         = imguiInfo.instance;
  imguiInit->PhysicalDevice   = imguiInfo.physicalDevice;
  imguiInit->Device           = imguiInfo.device;
  imguiInit->QueueFamily      = imguiInfo.queueFamily;
  imguiInit->Queue            = imguiInfo.queue;
  imguiInit->MinImageCount    = imguiInfo.minImageCount;
  imguiInit->ImageCount       = imguiInfo.imageCount;
  imguiInit->MSAASamples      = VK_SAMPLE_COUNT_1_BIT;
  imguiInit->DescriptorPool   = *imguiPool;
  imguiInit->UseDynamicRendering = true;
  imguiInit->PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  imguiInit->PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
  imguiInit->PipelineRenderingCreateInfo.pColorAttachmentFormats = &imguiColorFormat;
  ImGui_ImplVulkan_Init(imguiInit);
}

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

    // --- load a real PBR model (mesh + material maps) from glTF ---
    GltfModel model = loadGltf(std::string(ASSET_DIR) + "/DamagedHelmet.glb");
    SKY_RHI_INFO("Loaded glTF: {} verts, {} indices, albedo {}x{}",
                 model.vertices.size(), model.indices.size(), model.albedo.width, model.albedo.height);

    HdrImage env = loadHdr(std::string(ASSET_DIR) + "/venice_sunset_4k.hdr");
    SKY_RHI_INFO("Loaded HDR: {}x{}", env.width, env.height);

    auto makeTexture = [&](const ImageData& img) {
      auto tex = device.createTexture({ Sky::RHI::Format::RGBA8_UNORM, img.width, img.height, Sky::RHI::TextureUsage::Sampled });
      device.uploadTextureData(tex, img.pixels.data(), img.pixels.size());
      return tex;
    };

    auto texture         = makeTexture(model.albedo);      // baseColor
    auto textureMetallic = makeTexture(model.metalRough);  // G=roughness, B=metallic
    auto textureNormal   = makeTexture(model.normal);      // tangent-space normals
    auto envTex = device.createTexture({
      .format = Sky::RHI::Format::RGBA32_SFLOAT,
      .width = env.width,
      .height = env.height,
      .usage = Sky::RHI::TextureUsage::Sampled,
    });
    device.uploadTextureData(envTex, env.pixels.data(), env.pixels.size() * sizeof(float));

    auto sampler = device.createSampler({});

    // descriptor: env-текстура
    Sky::RHI::DescriptorSetLayoutDesc skyLayoutDesc{};
    skyLayoutDesc.bindings = { { 0, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment } };
    auto skyLayout  = device.createDescriptorSetLayout(skyLayoutDesc);
    auto skyDescSet = device.createDescriptorSet(skyLayout);
    device.updateDescriptorSetTexture(skyDescSet, 0, envTex, sampler);   // переиспользуем sampler

    // shaders
    auto skyVertCode = loadSpirv(std::string(SHADER_DIR) + "/skybox.vert.spv");
    auto skyFragCode = loadSpirv(std::string(SHADER_DIR) + "/skybox.frag.spv");
    auto skyVs = device.createShader({ skyVertCode.data(), skyVertCode.size() * sizeof(uint32_t) });
    auto skyFs = device.createShader({ skyFragCode.data(), skyFragCode.size() * sizeof(uint32_t) });

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

    Sky::RHI::GraphicsPipelineDesc skyDesc{};
    skyDesc.vertexShader        = skyVs;
    skyDesc.fragmentShader      = skyFs;
    skyDesc.vertexStride        = 0;
    skyDesc.colorFormat         = device.swapchainFormat(sw);
    skyDesc.depthFormat         = Sky::RHI::Format::D32_SFLOAT;   // match the pass's depth attachment
    skyDesc.pushConstantSize    = sizeof(SkyPush);
    skyDesc.descriptorSetLayout = skyLayout;
    skyDesc.depthTest           = false;
    skyDesc.depthWrite          = false;
    auto skyPipeline = device.createGraphicsPipeline(skyDesc);

    // --- ImGui init (Vulkan backend via dynamic rendering; GLFW input backend) ---
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    ImGui_ImplVulkan_InitInfo imguiInfo{};
    InitImGui(window, device, &imguiPool, &imguiInfo);

    // debug-tweakable scene params (demonstrates ImGui + useful for Phase 7 tuning)
    float sunIntensity   = 2.0f;
    float pointIntensity = 8.0f;

    const auto vb = device.createBuffer({
       model.vertices.size() * sizeof(Vertex),
       Sky::RHI::BufferUsage::Vertex | Sky::RHI::BufferUsage::TransferDst,
       Sky::RHI::MemoryType::GpuOnly });
    device.uploadBufferData(vb, model.vertices.data(), model.vertices.size() * sizeof(Vertex));

    const auto ib = device.createBuffer({
      model.indices.size() * sizeof(uint32_t),
      Sky::RHI::BufferUsage::Index | Sky::RHI::BufferUsage::TransferDst,
      Sky::RHI::MemoryType::GpuOnly });
    device.uploadBufferData(ib, model.indices.data(), model.indices.size() * sizeof(uint32_t));

    const uint32_t indexCount = static_cast<uint32_t>(model.indices.size());

    struct TriData
    {
      Sky::RHI::FGResource backbuffer;
      Sky::RHI::FGResource depth;
    };

    while (!window.shouldClose())
    {
      Window::pollEvents();

      device.beginFrame();

      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      ImGui::Begin("SkyRenderer");
      ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
      ImGui::SliderFloat("Sun intensity",   &sunIntensity,   0.0f, 10.0f);
      ImGui::SliderFloat("Point intensity", &pointIntensity, 0.0f, 20.0f);
      ImGui::End();

      const auto extent = device.swapchainExtent(sw);

      const float time   = static_cast<float>(glfwGetTime());
      const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

      glm::mat4 model = glm::rotate(glm::mat4(1.0f), time,
                              glm::vec3(0.0f, 1.0f, 0.0f));   // turntable spin around Y

      glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                             glm::vec3(0.0f, 0.0f, 0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f));

      glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
      proj[1][1] *= -1.0f;

      glm::mat4 invViewProj = glm::inverse(proj * view);

      glm::mat4 mvp = proj * view * model;

      PushConstants pushConstants = {
        .mvp = mvp,
        .model = model,
      };

      SkyPush skyPush{
        invViewProj,
        glm::vec4(0.0f, 0.0f, 3.0f, 0.0f)
      };  // camPos = eye (0,0,3)

      SceneData scene{};
      scene.sunDir     = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
      scene.sunColor   = glm::vec3(sunIntensity);
      scene.pointPos   = glm::vec3(cos(time)*3.0f, 1.5f, sin(time)*3.0f);
      scene.pointColor = glm::vec3(pointIntensity);
      scene.camPos     = glm::vec3(0.0f, 0.0f, 3.0f);
      memcpy(sceneMapped, &scene, sizeof(SceneData));

      Sky::RHI::FrameGraph fg(device);
      fg.addRasterPass<TriData>("Triangle",
        [&](Sky::RHI::PassBuilder& b, TriData& d) {
          d.backbuffer = b.writeColor(b.importSwapchain(sw));
          d.depth = b.writeDepth(b.createTexture("deph",
            { Sky::RHI::Format::D32_SFLOAT, extent.width, extent.height}));
        },
        [&](const TriData&, Sky::RHI::FGResources&, Sky::RHI::CommandList& cmd) {
          // dynamic viewport/scissor must be set before ANY draw in the pass
          cmd.setViewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));
          cmd.setScissor(0, 0, extent.width, extent.height);

          // skybox (background, no depth)
          cmd.bindPipeline(skyPipeline);
          cmd.bindDescriptorSet(skyDescSet);
          cmd.pushConstants(&skyPush, sizeof(SkyPush));
          cmd.draw(3);

          // model on top
          cmd.bindPipeline(pipeline);
          cmd.bindDescriptorSet(descSet);
          cmd.pushConstants(&pushConstants, sizeof(PushConstants));
          cmd.bindVertexBuffer(vb);
          cmd.bindIndexBuffer(ib, Sky::RHI::IndexType::UInt32);
          cmd.drawIndexed(indexCount);
        });

      fg.compile();
      device.execute(fg);

      ImGui::Render();
      device.recordOverlay([](void* cmd) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmd));
      });

      device.endFrame();
    }

    device.waitIdle();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(imguiInfo.Device, imguiPool, nullptr);

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