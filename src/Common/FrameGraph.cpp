#include "SkyRHI/FrameGraph.h"
#include "Vulkan/DeviceImpl.h"
#include "Vulkan/VulkanTranslate.h"
#include "skypch.h"

namespace { std::unique_ptr<void, void(*)(void*)> makeEmptyData() { return { nullptr, [](void*){} }; }}

namespace Sky::RHI
{

TextureHandle FGResources::getTexture(FGResource resource) const noexcept
{
  (void)resource;
  return {};
}

FGResource PassBuilder::importSwapchain(SwapchainHandle swapchain)
{
  return m_Graph->registerImportedSwapchain(swapchain);
}

FGResource PassBuilder::importColorTarget(TextureHandle texture)
{
  return m_Graph->registerImportedTexture(texture);
}

FGResource PassBuilder::createTexture(const char* name, const FGTextureDesc& desc)
{
  return m_Graph->registerTransientTexture(name, desc);
}

FGResource PassBuilder::read(const FGResource resource)
{
  m_Graph->recordRead(m_PassIndex, resource);
  return resource;
}

FGResource PassBuilder::writeColor(const FGResource resource)
{
  m_Graph->recordWrite(m_PassIndex, resource, FGAttachmentType::Color);
  return resource;
}

FGResource PassBuilder::writeDepth(const FGResource resource)
{
  m_Graph->recordWrite(m_PassIndex, resource, FGAttachmentType::Depth);
  return resource;
}

struct FGResourceEntry
{
  std::string name;
  bool imported = false;
  SwapchainHandle importedSwapchain;
  TextureHandle importedTexture;
  bool importedIsTexture = false;
  FGTextureDesc desc;
};

struct FGResourceWrite
{
  FGResource resource;
  FGAttachmentType type;
};

struct FrameGraph::Pass
{
  std::string     name;
  ErasedExecuteFn execute;
  ErasedData      data;

  std::vector<FGResource> reads;
  std::vector<FGResourceWrite> writes;
};

struct FrameGraph::Impl
{
  Device*                      device = nullptr;
  std::vector<Pass>            passes;
  std::vector<FGResourceEntry> resources;
  std::vector<uint32_t>        executionOrder;
};

FrameGraph::FrameGraph(Device& device)
  : m_Impl(std::make_unique<Impl>())
{
  m_Impl->device = &device;
}

FrameGraph::~FrameGraph() = default;


void FrameGraph::compile()
{
  constexpr uint32_t kNoProducer = UINT32_MAX;

  const size_t passCount = m_Impl->passes.size();
  const size_t resCount  = m_Impl->resources.size();

  std::vector<uint32_t> producer(resCount, kNoProducer);
  std::vector<uint32_t> resourceRef(resCount, 0);
  std::vector<uint32_t> passRef(passCount, 0);
  std::vector<bool>     culled(passCount, false);
  std::vector<uint32_t> inDegree(passCount, 0);
  std::vector<std::vector<uint32_t>> adj(passCount);

  for (uint32_t i = 0; i < passCount; i++)
    for (const auto& writer : m_Impl->passes[i].writes)
      producer[writer.resource.index] = i;

  for (const auto& pass : m_Impl->passes)
    for (const auto& r : pass.reads)
      resourceRef[r.index]++;

  for (uint32_t r = 0; r < resCount; ++r)
    if (m_Impl->resources[r].imported)
      resourceRef[r]++;

  for (uint32_t p = 0; p < passCount; ++p)
    passRef[p] = static_cast<uint32_t>(m_Impl->passes[p].writes.size());

  std::vector<uint32_t> stack;
  for (uint32_t r = 0; r < resCount; ++r)
    if (resourceRef[r] == 0)
      stack.push_back(r);

  while (!stack.empty())
  {
    auto r = stack.back();
    stack.pop_back();
    auto prod = producer[r];

    if (prod == kNoProducer)
      continue;

    if (--passRef[prod] == 0)
    {
      culled[prod] = true;
      for (const auto& rr : m_Impl->passes[prod].reads)
        if (--resourceRef[rr.index] == 0)
          stack.push_back(rr.index);
    }
  }

  for (uint32_t b = 0; b < passCount; ++b)
  {
    if (culled[b]) continue;

    for (const auto& r : m_Impl->passes[b].reads)
    {
      const uint32_t a = producer[r.index];
      if (a == kNoProducer || culled[a]) continue;

      adj[a].push_back(b);
      inDegree[b]++;
    }
  }

  m_Impl->executionOrder.clear();

  std::vector<uint32_t> ready;
  for (uint32_t p = 0; p < passCount; ++ p)
    if (!culled[p] && inDegree[p] == 0)
      ready.push_back(p);

  while (!ready.empty())
  {
    const uint32_t p = ready.back();
    ready.pop_back();
    m_Impl->executionOrder.push_back(p);

    for (const uint32_t q : adj[p])
      if (--inDegree[q] == 0)
        ready.push_back(q);
  }
}

void FrameGraph::execute(CommandList& cmd)
{
  auto vkCmd = static_cast<VkCommandBuffer>(cmd.m_NativeCmd);
  auto* impl = static_cast<Device::Impl*>(cmd.m_DeviceImpl);

  auto transition = [&](VkImage img, VkImageLayout& current, VkImageLayout target,
                     VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                     VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                     VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
  {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = current;
    b.newLayout = target;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = { aspect, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(vkCmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    current = target;   // ← обновляем tracked layout
  };

  std::vector layouts(m_Impl->resources.size(), VK_IMAGE_LAYOUT_UNDEFINED);
  FGResources resources;

  for (const uint32_t passIndex : m_Impl->executionOrder)
  {
    auto& pass = m_Impl->passes[passIndex];

    VkRenderingAttachmentInfoKHR depthAttachment{};
    bool hasDepth = false;

    for (auto& write : pass.writes)
    {
      if (write.type == FGAttachmentType::Depth)
      {
        FGResourceEntry& entry = m_Impl->resources[write.resource.index];

        auto img = std::make_unique<VulkanImage>(
          impl->device.allocator(), impl->device.handle(),
          toVkFormat(entry.desc.format), entry.desc.width, entry.desc.height,
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

        transition(img->handle(), layouts[write.resource.index],
          VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
          0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
          VK_IMAGE_ASPECT_DEPTH_BIT);

        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        depthAttachment.imageView = img->view();
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        hasDepth = true;
        impl->frameTransients.push_back(std::move(img));
      }
    }

    std::vector<VkRenderingAttachmentInfoKHR> colorAttachments;
    VkExtent2D extent{};

    for (auto& write : pass.writes)
    {
      if (write.type != FGAttachmentType::Color)
        continue;

      FGResourceEntry& entry = m_Impl->resources[write.resource.index];
      VkImage image; VkImageView view;

      if (entry.importedIsTexture)
      {
        VulkanImage* tex = impl->texturePool.resolve(entry.importedTexture);
        image = tex->handle();
        view = tex->view();
        extent = { tex->width(), tex->height() };
      }
      else
      {
        VulkanSwapchainEntry* sw = impl->swapchainPool.resolve(entry.importedSwapchain);
        image = sw->swapchain.images()[impl->currentImageIndex];
        view = sw->swapchain.imageViews()[impl->currentImageIndex];
        extent = sw->swapchain.extent();
      }

      transition(image, layouts[write.resource.index],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

      VkRenderingAttachmentInfoKHR att{};
      att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
      att.imageView = view;
      att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      att.clearValue.color = {{ 0.0f, 0.0f, 0.05f, 1.0f }};

      colorAttachments.push_back(att);
    }

    VkRenderingInfoKHR ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    ri.renderArea.offset = { 0, 0 };
    ri.renderArea.extent = extent;
    ri.layerCount = 1;
    ri.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    ri.pColorAttachments = colorAttachments.data();
    ri.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;

    vkCmdBeginRenderingKHR(vkCmd, &ri);

    CommandList passCmd = impl->createCommandList(vkCmd);
    pass.execute(resources, passCmd);

    vkCmdEndRenderingKHR(vkCmd);
  }

  for (uint32_t r = 0; r < m_Impl->resources.size(); ++r)
  {
    auto& e = m_Impl->resources[r];
    if (!e.imported)
      continue;

    if (e.importedIsTexture)
    {
      VulkanImage* tex = impl->texturePool.resolve(e.importedTexture);
      transition(tex->handle(), layouts[r], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    else
    {
      VulkanSwapchainEntry* sw = impl->swapchainPool.resolve(m_Impl->resources[r].importedSwapchain);
      const VkImage image = sw->swapchain.images()[impl->currentImageIndex];

      transition(image, layouts[r], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
  }
}

uint32_t FrameGraph::beginPass(const char* name)
{
  const uint32_t index = static_cast<uint32_t>(m_Impl->passes.size());
  m_Impl->passes.emplace_back(Pass
  {
    .name = name,
    .data = makeEmptyData()
  });
  return index;
}

void FrameGraph::finalizePass(uint32_t passIndex, ErasedExecuteFn execute, ErasedData data)
{
  m_Impl->passes[passIndex].execute = std::move(execute);
  m_Impl->passes[passIndex].data = std::move(data);
}

FGResource FrameGraph::registerImportedSwapchain(SwapchainHandle swapchainHandle)
{
  FGResourceEntry entry{};
  entry.imported = true;
  entry.importedSwapchain = swapchainHandle;
  const uint32_t index = static_cast<uint32_t>(m_Impl->resources.size());
  m_Impl->resources.push_back(entry);
  return FGResource{index};
}

FGResource FrameGraph::registerTransientTexture(const char* name, const FGTextureDesc& desc)
{
  FGResourceEntry entry{};
  entry.imported = false;
  entry.desc = desc;
  const uint32_t index = static_cast<uint32_t>(m_Impl->resources.size());
  m_Impl->resources.push_back(entry);
  return FGResource{index};
}

FGResource FrameGraph::registerImportedTexture(TextureHandle texture)
{
  const uint32_t idx = static_cast<uint32_t>(m_Impl->resources.size());
  FGResourceEntry entry;
  entry.imported = true;
  entry.importedIsTexture = true;
  entry.importedTexture = texture;
  m_Impl->resources.push_back(entry);
  return FGResource{ idx };
}

void FrameGraph::recordRead(uint32_t passIndex, FGResource resource)
{
  m_Impl->passes[passIndex].reads.push_back({resource});
}

void FrameGraph::recordWrite(uint32_t passIndex, FGResource resource, FGAttachmentType type)
{
  m_Impl->passes[passIndex].writes.push_back({resource, type});
}

}