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

// ============================================================================
// Push-constant / UBO layouts (must match GLSL)
// ============================================================================

struct PushConstants          // model: MVP + model matrix
{
  glm::mat4 mvp;
  glm::mat4 model;
};

struct SkyPush                // skybox: ray reconstruction + camera
{
  glm::mat4 invViewProj;
  glm::vec4 camPos;
};

struct BloomPush              // bloom downsample: source texel size + threshold
{
  glm::vec2 texelSize;
  float     threshold;
  int       isFirstPass;
};

struct TonemapPush            // tonemap: exposure + bloom mix strength
{
  float exposure;
  float bloomIntensity;
};

struct UpsamplePush
{
  glm::vec2 filterRadius;
};

struct SceneData             // per-frame scene UBO (std140: vec3 padded to 16)
{
  glm::vec3 sunDir;     float _pad0;
  glm::vec3 sunColor;   float _pad1;
  glm::vec3 pointPos;   float _pad2;
  glm::vec3 pointColor; float _pad3;
  glm::vec3 camPos;     float _pad4;
  glm::mat4 lightVP;
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
    // ========================================================================
    // Window, Device, Swapchain
    // ========================================================================
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

    const auto sw       = device.defaultSwapchain();
    const auto scExtent = device.swapchainExtent(sw);

    // ========================================================================
    // Assets: glTF model + HDR environment
    // ========================================================================
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
      .width  = env.width,
      .height = env.height,
      .usage  = Sky::RHI::TextureUsage::Sampled,
    });
    device.uploadTextureData(envTex, env.pixels.data(), env.pixels.size() * sizeof(float));

    // ========================================================================
    // Shader modules
    // ========================================================================
    auto makeShader = [&](const char* name) {
      auto code = loadSpirv(std::string(SHADER_DIR) + name);
      return device.createShader({ code.data(), code.size() * sizeof(uint32_t) });
    };

    auto vs     = makeShader("/triangle.vert.spv");     // model
    auto fs     = makeShader("/triangle.frag.spv");
    auto skyVs  = makeShader("/skybox.vert.spv");       // fullscreen triangle (reused by all post passes)
    auto skyFs  = makeShader("/skybox.frag.spv");
    auto brdfFs = makeShader("/brdf.frag.spv");         // IBL: BRDF LUT bake
    auto irrFs  = makeShader("/irradiance.frag.spv");   // IBL: irradiance bake
    auto preFs  = makeShader("/prefilter.frag.spv");    // IBL: prefiltered env bake
    auto shVs   = makeShader("/shadow.vert.spv");       // shadow depth pass
    auto shFs   = makeShader("/shadow.frag.spv");
    auto tmFs   = makeShader("/tonemap.frag.spv");      // post: tonemap + bloom composite
    auto dsFs   = makeShader("/downsample.frag.spv");   // post: bloom downsample/prefilter
    auto usFs   = makeShader("/upsample.frag.spv");     // post: bloom uspsample

    // ========================================================================
    // Samplers
    // ========================================================================
    auto sampler      = device.createSampler({});                                          // default: linear, repeat
    auto bloomSampler = device.createSampler({ .addressMode = Sky::RHI::AddressMode::ClampToEdge });

    // ========================================================================
    // Render targets & GPU textures
    // ========================================================================
    // HDR scene target (scene renders here, then tonemap -> swapchain)
    auto hdrTarget = device.createTexture({
      Sky::RHI::Format::RGBA16_SFLOAT, scExtent.width, scExtent.height,
      Sky::RHI::TextureUsage::ColorAttachment | Sky::RHI::TextureUsage::Sampled });

    // Bloom mip chain: base = half-res, halved down to ~8px, capped at 7 levels
    uint32_t bloomMipCount = 1;
    {
      uint32_t bw = scExtent.width / 2, bh = scExtent.height / 2;
      while (glm::min(bw, bh) > 8 && bloomMipCount < 7) { bw /= 2; bh /= 2; bloomMipCount++; }
    }
    auto bloomTarget = device.createTexture({
      Sky::RHI::Format::RGBA16_SFLOAT, scExtent.width / 2, scExtent.height / 2,
      Sky::RHI::TextureUsage::ColorAttachment | Sky::RHI::TextureUsage::Sampled, bloomMipCount });

    // IBL bakes (filled once at startup, below)
    auto brdfLut = device.createTexture({
      Sky::RHI::Format::RG16_SFLOAT, 512, 512,
      Sky::RHI::TextureUsage::ColorAttachment | Sky::RHI::TextureUsage::Sampled });

    auto irradianceMap = device.createTexture({
      Sky::RHI::Format::RGBA16_SFLOAT, 64, 32,
      Sky::RHI::TextureUsage::ColorAttachment | Sky::RHI::TextureUsage::Sampled });

    const uint32_t PREFILTER_MIPS = 6;
    auto prefilteredMap = device.createTexture({
      Sky::RHI::Format::RGBA16_SFLOAT, 256, 128,
      Sky::RHI::TextureUsage::ColorAttachment | Sky::RHI::TextureUsage::Sampled,
      PREFILTER_MIPS });

    const uint32_t SHADOW_SIZE = 2048;
    auto shadowMap = device.createTexture({
      Sky::RHI::Format::D32_SFLOAT, SHADOW_SIZE, SHADOW_SIZE,
      Sky::RHI::TextureUsage::DepthStencilAttachment | Sky::RHI::TextureUsage::Sampled });

    // ========================================================================
    // Descriptor layouts & sets
    // ========================================================================
    // --- environment (skybox + IBL bakes) ---
    Sky::RHI::DescriptorSetLayoutDesc skyLayoutDesc{};
    skyLayoutDesc.bindings = { { 0, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment } };
    auto skyLayout  = device.createDescriptorSetLayout(skyLayoutDesc);
    auto skyDescSet = device.createDescriptorSet(skyLayout);
    device.updateDescriptorSetTexture(skyDescSet, 0, envTex, sampler);

    // --- model (PBR): 3 material maps + scene UBO + 3 IBL maps + shadow ---
    Sky::RHI::DescriptorSetLayoutDesc layoutDesc{};
    layoutDesc.bindings = {
      { 0, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // albedo
      { 1, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // metalRough
      { 2, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // normalMap
      { 3, Sky::RHI::DescriptorType::UniformBuffer,        Sky::RHI::ShaderStage::Fragment }, // scene
      { 4, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // irradiance
      { 5, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // prefiltered
      { 6, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // brdfLut
      { 7, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment }, // shadowMap
    };
    auto descLayout = device.createDescriptorSetLayout(layoutDesc);

    const auto sceneUBO = device.createBuffer({
      sizeof(SceneData),
      Sky::RHI::BufferUsage::Uniform,
      Sky::RHI::MemoryType::CpuToGpu });
    void* sceneMapped = device.mapBuffer(sceneUBO);

    auto descSet = device.createDescriptorSet(descLayout);
    device.updateDescriptorSetTexture(descSet, 0, texture,         sampler);
    device.updateDescriptorSetTexture(descSet, 1, textureMetallic, sampler);
    device.updateDescriptorSetTexture(descSet, 2, textureNormal,   sampler);
    device.updateDescriptorSetBuffer (descSet, 3, sceneUBO, sizeof(SceneData));
    device.updateDescriptorSetTexture(descSet, 4, irradianceMap,   sampler);
    device.updateDescriptorSetTexture(descSet, 5, prefilteredMap,  sampler);
    device.updateDescriptorSetTexture(descSet, 6, brdfLut,         sampler);
    device.updateDescriptorSetTexture(descSet, 7, shadowMap,       sampler);

    // --- tonemap: hdr (binding 0) + bloom mip0 (binding 1) ---
    Sky::RHI::DescriptorSetLayoutDesc tmLayoutDesc{};
    tmLayoutDesc.bindings = {
      { 0, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment },
      { 1, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment },
    };
    auto tmLayout  = device.createDescriptorSetLayout(tmLayoutDesc);
    auto tmDescSet = device.createDescriptorSet(tmLayout);
    device.updateDescriptorSetTexture   (tmDescSet, 0, hdrTarget,   sampler);
    device.updateDescriptorSetTextureMip(tmDescSet, 1, bloomTarget, bloomSampler, 0);

    // --- bloom: source texture the downsample pass samples ---
    Sky::RHI::DescriptorSetLayoutDesc bloomLayoutDesc{};
    bloomLayoutDesc.bindings = { { 0, Sky::RHI::DescriptorType::CombinedImageSampler, Sky::RHI::ShaderStage::Fragment } };
    auto bloomLayout  = device.createDescriptorSetLayout(bloomLayoutDesc);
    auto bloomDescSet = device.createDescriptorSet(bloomLayout);
    device.updateDescriptorSetTexture(bloomDescSet, 0, hdrTarget, bloomSampler);   // first pass source = HDR

    // one descriptor per bloom mip — samples that mip (source for down/up passes)
    std::vector<Sky::RHI::DescriptorSetHandle> bloomMipSets(bloomMipCount);
    for (uint32_t k = 0; k < bloomMipCount; ++k) {
      bloomMipSets[k] = device.createDescriptorSet(bloomLayout);
      device.updateDescriptorSetTextureMip(bloomMipSets[k], 0, bloomTarget, bloomSampler, k);
    }

    // ========================================================================
    // Vertex layouts
    // ========================================================================
    const std::vector<Sky::RHI::VertexAttribute> attrs = {
      { 0, Sky::RHI::Format::RGB32_SFLOAT,  offsetof(Vertex, pos) },     // location 0 = pos
      { 1, Sky::RHI::Format::RG32_SFLOAT,   offsetof(Vertex, uv)  },     // location 1 = uv
      { 2, Sky::RHI::Format::RGB32_SFLOAT,  offsetof(Vertex, normal) },  // location 2 = normal
      { 3, Sky::RHI::Format::RGBA32_SFLOAT, offsetof(Vertex, tangent) }  // location 3 = tangent
    };

    const std::vector<Sky::RHI::VertexAttribute> shadowAttrs = {
      { 0, Sky::RHI::Format::RGB32_SFLOAT, offsetof(Vertex, pos) },   // position only
    };

    // ========================================================================
    // Pipelines
    // ========================================================================
    // model (PBR) — renders into the HDR target
    Sky::RHI::GraphicsPipelineDesc pipeDesc{};
    pipeDesc.vertexShader        = vs;
    pipeDesc.fragmentShader      = fs;
    pipeDesc.vertexStride        = sizeof(Vertex);
    pipeDesc.vertexAttributes    = attrs;
    pipeDesc.colorFormat         = Sky::RHI::Format::RGBA16_SFLOAT;
    pipeDesc.depthFormat         = Sky::RHI::Format::D32_SFLOAT;
    pipeDesc.pushConstantSize    = sizeof(PushConstants);
    pipeDesc.descriptorSetLayout = descLayout;
    const auto pipeline = device.createGraphicsPipeline(pipeDesc);

    // skybox — background, no depth
    Sky::RHI::GraphicsPipelineDesc skyDesc{};
    skyDesc.vertexShader        = skyVs;
    skyDesc.fragmentShader      = skyFs;
    skyDesc.vertexStride        = 0;
    skyDesc.colorFormat         = Sky::RHI::Format::RGBA16_SFLOAT;
    skyDesc.depthFormat         = Sky::RHI::Format::D32_SFLOAT;
    skyDesc.pushConstantSize    = sizeof(SkyPush);
    skyDesc.descriptorSetLayout = skyLayout;
    skyDesc.depthTest           = false;
    skyDesc.depthWrite          = false;
    auto skyPipeline = device.createGraphicsPipeline(skyDesc);

    // IBL: BRDF LUT bake
    Sky::RHI::GraphicsPipelineDesc brdfDesc{};
    brdfDesc.vertexShader   = skyVs;
    brdfDesc.fragmentShader = brdfFs;
    brdfDesc.vertexStride   = 0;
    brdfDesc.colorFormat    = Sky::RHI::Format::RG16_SFLOAT;
    brdfDesc.depthFormat    = Sky::RHI::Format::Undefined;
    auto brdfPipeline = device.createGraphicsPipeline(brdfDesc);

    // IBL: irradiance bake
    Sky::RHI::GraphicsPipelineDesc irrDesc{};
    irrDesc.vertexShader        = skyVs;
    irrDesc.fragmentShader      = irrFs;
    irrDesc.vertexStride        = 0;
    irrDesc.colorFormat         = Sky::RHI::Format::RGBA16_SFLOAT;
    irrDesc.depthFormat         = Sky::RHI::Format::Undefined;
    irrDesc.descriptorSetLayout = skyLayout;
    auto irrPipeline = device.createGraphicsPipeline(irrDesc);

    // IBL: prefiltered env bake (per-roughness mip)
    Sky::RHI::GraphicsPipelineDesc preDesc{};
    preDesc.vertexShader        = skyVs;
    preDesc.fragmentShader      = preFs;
    preDesc.vertexStride        = 0;
    preDesc.colorFormat         = Sky::RHI::Format::RGBA16_SFLOAT;
    preDesc.depthFormat         = Sky::RHI::Format::Undefined;
    preDesc.descriptorSetLayout = skyLayout;                     // env on set0 binding0
    preDesc.pushConstantSize    = sizeof(float);                 // roughness
    auto prefilterPipeline = device.createGraphicsPipeline(preDesc);

    // shadow — depth-only (no color attachment)
    Sky::RHI::GraphicsPipelineDesc shadowDesc{};
    shadowDesc.vertexShader     = shVs;
    shadowDesc.fragmentShader   = shFs;
    shadowDesc.vertexStride     = sizeof(Vertex);
    shadowDesc.vertexAttributes = shadowAttrs;
    shadowDesc.colorFormat      = Sky::RHI::Format::Undefined;
    shadowDesc.depthFormat      = Sky::RHI::Format::D32_SFLOAT;
    shadowDesc.depthTest        = true;
    shadowDesc.depthWrite       = true;
    shadowDesc.pushConstantSize = sizeof(glm::mat4);
    auto shadowPipeline = device.createGraphicsPipeline(shadowDesc);

    // bloom downsample/prefilter
    Sky::RHI::GraphicsPipelineDesc dsDesc{};
    dsDesc.vertexShader        = skyVs;
    dsDesc.fragmentShader      = dsFs;
    dsDesc.vertexStride        = 0;
    dsDesc.colorFormat         = Sky::RHI::Format::RGBA16_SFLOAT;
    dsDesc.depthFormat         = Sky::RHI::Format::Undefined;
    dsDesc.descriptorSetLayout = bloomLayout;
    dsDesc.pushConstantSize    = sizeof(BloomPush);
    auto downsamplePipeline = device.createGraphicsPipeline(dsDesc);

    // tonemap — HDR + bloom -> swapchain
    Sky::RHI::GraphicsPipelineDesc tmDesc{};
    tmDesc.vertexShader        = skyVs;
    tmDesc.fragmentShader      = tmFs;
    tmDesc.vertexStride        = 0;
    tmDesc.colorFormat         = device.swapchainFormat(sw);
    tmDesc.depthFormat         = Sky::RHI::Format::Undefined;
    tmDesc.descriptorSetLayout = tmLayout;
    tmDesc.pushConstantSize    = sizeof(TonemapPush);
    auto tonemapPipeline = device.createGraphicsPipeline(tmDesc);

    Sky::RHI::GraphicsPipelineDesc usDesc{};
    usDesc.vertexShader        = skyVs;
    usDesc.fragmentShader      = usFs;
    usDesc.vertexStride        = 0;
    usDesc.colorFormat         = Sky::RHI::Format::RGBA16_SFLOAT;
    usDesc.depthFormat         = Sky::RHI::Format::Undefined;
    usDesc.descriptorSetLayout = bloomLayout;
    usDesc.pushConstantSize    = sizeof(UpsamplePush);
    usDesc.additiveBlend       = true;
    auto upsamplePipeline = device.createGraphicsPipeline(usDesc);

    // ========================================================================
    // IBL bakes (one-time, env is static)
    // ========================================================================
    device.renderToTexture(brdfLut, 512, 512, brdfPipeline, {}, nullptr, 0);
    device.renderToTexture(irradianceMap, 64, 32, irrPipeline, skyDescSet, nullptr, 0);
    for (uint32_t mip = 0; mip < PREFILTER_MIPS; ++mip) {
      uint32_t w = 256u >> mip;
      uint32_t h = 128u >> mip;
      float roughness = float(mip) / float(PREFILTER_MIPS - 1);
      device.renderToTexture(prefilteredMap, w, h, prefilterPipeline, skyDescSet,
                             &roughness, sizeof(float), mip);
    }

    // ========================================================================
    // GPU geometry buffers
    // ========================================================================
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

    // ========================================================================
    // ImGui
    // ========================================================================
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    ImGui_ImplVulkan_InitInfo imguiInfo{};
    InitImGui(window, device, &imguiPool, &imguiInfo);

    // ========================================================================
    // Tweakables (ImGui-controlled)
    // ========================================================================
    float sunIntensity   = 2.0f;
    float pointIntensity = 8.0f;
    float exposure       = 1.0f;
    float bloomIntensity = 0.05f;

    struct TriData
    {
      Sky::RHI::FGResource backbuffer;
      Sky::RHI::FGResource depth;
    };

    // ========================================================================
    // Render loop
    // ========================================================================
    while (!window.shouldClose())
    {
      Window::pollEvents();

      device.beginFrame();

      // --- ImGui UI ---
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      ImGui::Begin("SkyRenderer");
      ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
      ImGui::SliderFloat("Sun intensity",   &sunIntensity,   0.0f, 10.0f);
      ImGui::SliderFloat("Point intensity", &pointIntensity, 0.0f, 20.0f);
      ImGui::SliderFloat("Exposure",        &exposure,       0.0f, 5.0f);
      ImGui::SliderFloat("Bloom intensity", &bloomIntensity, 0.0f, 1.0f);
      ImGui::End();

      const auto extent = device.swapchainExtent(sw);

      // --- camera / transforms ---
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
      glm::mat4 mvp         = proj * view * model;

      PushConstants pushConstants = { .mvp = mvp, .model = model };
      SkyPush skyPush{ invViewProj, glm::vec4(0.0f, 0.0f, 3.0f, 0.0f) };   // camPos = eye (0,0,3)

      // --- shadow pass (light's view of the model) ---
      glm::vec3 sunDir    = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));    // matches sunDir in UBO
      glm::vec3 lightPos  = sunDir * 5.0f;                                  // "light camera" along the direction
      glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
      glm::mat4 lightProj = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, 0.1f, 10.0f);  // ortho box around the model
      glm::mat4 lightVP   = lightProj * lightView;
      glm::mat4 lightMVP  = lightVP * model;
      device.renderShadowMap(shadowMap, shadowPipeline, vb, ib, indexCount,
                             SHADOW_SIZE, &lightMVP, sizeof(glm::mat4));

      // --- scene UBO ---
      SceneData scene{};
      scene.sunDir     = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
      scene.sunColor   = glm::vec3(sunIntensity);
      scene.pointPos   = glm::vec3(cos(time)*3.0f, 1.5f, sin(time)*3.0f);
      scene.pointColor = glm::vec3(pointIntensity);
      scene.camPos     = glm::vec3(0.0f, 0.0f, 3.0f);
      scene.lightVP    = lightVP;
      memcpy(sceneMapped, &scene, sizeof(SceneData));

      // --- main scene pass (skybox + model) into the HDR target ---
      Sky::RHI::FrameGraph fg(device);
      fg.addRasterPass<TriData>("Triangle",
        [&](Sky::RHI::PassBuilder& b, TriData& d) {
          d.backbuffer = b.writeColor(b.importColorTarget(hdrTarget));
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

      // --- bloom: prefilter HDR -> bloom mip0 (stage A: single pass) ---
      BloomPush bp{};
      bp.texelSize   = glm::vec2(1.0f / scExtent.width, 1.0f / scExtent.height);
      bp.threshold   = 1.0f;
      bp.isFirstPass = 1;
      device.bloomPass(bloomTarget, scExtent.width / 2, scExtent.height / 2,
                       downsamplePipeline, bloomDescSet, &bp, sizeof(BloomPush), 0, false);

      for (uint32_t i = 0; i < bloomMipCount - 1; i++)
      {
        uint32_t dstW = std::max(1u, (scExtent.width  / 2) >> (i + 1));
        uint32_t dstH = std::max(1u, (scExtent.height / 2) >> (i + 1));

        bp.texelSize   = glm::vec2(1.0 / ((scExtent.width/2) >> i), 1.0 / ((scExtent.height/2) >> i));
        bp.threshold   = 1.0f;
        bp.isFirstPass = 0;
        device.bloomPass(bloomTarget, dstW, dstH, downsamplePipeline, bloomMipSets[i],
                         &bp, sizeof(BloomPush), i + 1, false);
      }

      UpsamplePush up{};
      up.filterRadius = glm::vec2(0.005f);

      for (uint32_t i = bloomMipCount - 1; i > 0; i--)
      {
        uint32_t dstW = std::max(1u, (scExtent.width  / 2) >> (i - 1));
        uint32_t dstH = std::max(1u, (scExtent.height / 2) >> (i - 1));
        device.bloomPass(bloomTarget, dstW, dstH, upsamplePipeline, bloomMipSets[i],
                         &up, sizeof(UpsamplePush), i - 1, true);
      }

      // --- tonemap: HDR + bloom -> swapchain ---
      TonemapPush tmp{ exposure, bloomIntensity };
      device.tonemap(tonemapPipeline, tmDescSet, &tmp, sizeof(TonemapPush));

      // --- ImGui overlay ---
      ImGui::Render();
      device.recordOverlay([](void* cmd) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmd));
      });

      device.endFrame();
    }

    device.waitIdle();

    // ========================================================================
    // Cleanup
    // ========================================================================
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
