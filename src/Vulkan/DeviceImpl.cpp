#include "skypch.h"

#include "DeviceImpl.h"

#include "SkyRHI/FrameGraph.h"
#include "VulkanDescriptorSet.h"
#include "VulkanTranslate.h"

namespace
{

std::unique_ptr<Sky::RHI::VulkanSwapchainEntry> makeSwapchainEntry(
  const VulkanInstance& instance, const VulkanDevice& device,
  const Sky::RHI::SurfaceFactoryFn& factory, uint32_t width, uint32_t height)
{
  auto vkFactory = [factory](const VkInstance inst) -> VkSurfaceKHR{
    return static_cast<VkSurfaceKHR>(factory(inst));
  };

  return std::make_unique<Sky::RHI::VulkanSwapchainEntry>(instance, device, vkFactory, width, height);
}

void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                           VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
  VkImageMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.oldLayout = oldLayout;
  b.newLayout = newLayout;
  b.srcAccessMask = srcAccess;
  b.dstAccessMask = dstAccess;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}

namespace Sky::RHI
{

Device::Impl::Impl(const DeviceCreateInfo& info)
  : instance(info.appName, info.engineName, info.requiredInstanceExtensions, info.enableValidation)
  , device(instance)
  , defaultSwapchainHandle(swapchainPool.allocate(
    makeSwapchainEntry(instance, device, info.surfaceFactory,
                               info.initialWindowWidth, info.initialWindowHeight)))
  , defaultEntry(swapchainPool.resolve(defaultSwapchainHandle))
  , commandPool(device)
{
  commandBuffer = commandPool.allocatePrimary();

  VkDescriptorPoolSize poolSize[] = {
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
  };
  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.maxSets = 100;
  poolInfo.poolSizeCount = 3;
  poolInfo.pPoolSizes = poolSize;
  SKY_RHI_VK_CHECK(vkCreateDescriptorPool(device.handle(), &poolInfo, nullptr,
    &descriptorPool), "Failed to create descriptor pool");

  renderFinished.resize(defaultEntry->swapchain.images().size());

  VkSemaphoreCreateInfo semInfo{};
  semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  SKY_RHI_VK_CHECK(vkCreateSemaphore(device.handle(), &semInfo, nullptr, &imageAvailable),
               "Failed to create imageAvailable semaphore");

  for (auto& sem : renderFinished)
    SKY_RHI_VK_CHECK(vkCreateSemaphore(device.handle(), &semInfo, nullptr, &sem),
                 "Failed to create renderFinished semaphore");

  VkSemaphoreTypeCreateInfo typeInfo{};
  typeInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  typeInfo.initialValue  = 0;

  VkSemaphoreCreateInfo timeLineSemInfo{};
  timeLineSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  timeLineSemInfo.pNext = &typeInfo;

  SKY_RHI_VK_CHECK(vkCreateSemaphore(device.handle(), &timeLineSemInfo, nullptr, &transferTimeline),
             "Failed to create timeline semaphore");

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  SKY_RHI_VK_CHECK(vkCreateFence(device.handle(), &fenceInfo, nullptr, &inFlight),
               "Failed to create inFlight fence");

  SKY_RHI_INFO("Device initialized (backend = Vulkan)");
}

Device::Impl::~Impl()
{
  vkDeviceWaitIdle(device.handle());

  vkDestroyDescriptorPool(device.handle(), descriptorPool, nullptr);

  vkDestroyFence(device.handle(), inFlight, nullptr);

  for (const auto sem : renderFinished)
    vkDestroySemaphore(device.handle(), sem, nullptr);

  vkDestroySemaphore(device.handle(), imageAvailable, nullptr);
  vkDestroySemaphore(device.handle(), transferTimeline, nullptr);
}

uint64_t Device::Impl::uploadTextureDataAsync(TextureHandle handle, const void* data, size_t size)
{
  VulkanImage* tex = texturePool.resolve(handle);
  if (!tex)
  {
    SKY_RHI_WARN("uploadTextureData: invalid texture handle");
    return 0;
  }

  auto staging = std::make_unique<VulkanBuffer>(device.allocator(),
     BufferDesc{ size, BufferUsage::TransferSrc, MemoryType::CpuOnly });
  vmaCopyMemoryToAllocation(device.allocator(), data, staging->allocation(), 0, size);

  VkCommandBuffer cmd = commandPool.allocatePrimary();

  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin);

  transitionImageLayout(cmd, tex->handle(),
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    0, VK_ACCESS_TRANSFER_WRITE_BIT,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
  region.imageExtent = { tex->width(), tex->height(), 1 };
  vkCmdCopyBufferToImage(cmd, staging->handle(), tex->handle(),
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  transitionImageLayout(cmd, tex->handle(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

  vkEndCommandBuffer(cmd);

  const uint64_t target = ++transferValue;

  VkTimelineSemaphoreSubmitInfo timelineInfo{};
  timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  timelineInfo.signalSemaphoreValueCount = 1;
  timelineInfo.pSignalSemaphoreValues = &target;

  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.pNext = &timelineInfo;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &transferTimeline;

  SKY_RHI_VK_CHECK(vkQueueSubmit(device.transferQueue(), 1, &submit, VK_NULL_HANDLE),
                   "Async upload submit failed");

  pendingUploads.push_back({ std::move(staging), cmd, target });
  return target;
}

void Device::Impl::beginFrame()
{
  collectFinishedTransfers();

  vkWaitForFences(device.handle(), 1, &inFlight, VK_TRUE, UINT64_MAX);
  vkResetFences(device.handle(), 1, &inFlight);

  frameTransients.clear();

  vkAcquireNextImageKHR(device.handle(), defaultEntry->swapchain.handle(), UINT64_MAX,
                        imageAvailable, VK_NULL_HANDLE, &currentImageIndex);

  vkResetCommandBuffer(commandBuffer, 0);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  SKY_RHI_VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin command buffer");
}

void Device::Impl::endFrame()
{
  SKY_RHI_VK_CHECK(vkEndCommandBuffer(commandBuffer), "Failed to end command buffer");

  VkSemaphore waitSemaphores[] = { imageAvailable };
  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  VkSemaphore signalSemaphores[] = { renderFinished[currentImageIndex] };

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  SKY_RHI_VK_CHECK(vkQueueSubmit(device.graphicQueue(), 1, &submitInfo, inFlight),
               "Failed to submit draw command buffer");

  VkSwapchainKHR swapchain = defaultEntry->swapchain.handle();

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapchain;
  presentInfo.pImageIndices = &currentImageIndex;

  vkQueuePresentKHR(device.graphicQueue(), &presentInfo);
}

void Device::Impl::immediateSubmit(const std::function<void(VkCommandBuffer)>& record)
{
  VkCommandBuffer cmd = commandPool.allocatePrimary();

  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(cmd, &begin);

  record(cmd);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;

  SKY_RHI_VK_CHECK(vkQueueSubmit(device.graphicQueue(), 1, &submit, VK_NULL_HANDLE),
    "Immediate submit failed");

  vkQueueWaitIdle(device.graphicQueue());

  commandPool.free(cmd);
}

void Device::Impl::flushTransfers()
{
  if (transferValue == 0) return;

  VkSemaphoreWaitInfo wait{};
  wait.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  wait.semaphoreCount = 1;
  wait.pSemaphores    = &transferTimeline;
  wait.pValues        = &transferValue;
  vkWaitSemaphores(device.handle(), &wait, UINT64_MAX);

  for (auto& u : pendingUploads) commandPool.free(u.cmd);
  pendingUploads.clear();
}

void Device::Impl::waitIdle() const
{
  vkDeviceWaitIdle(device.handle());
}

Device::Device(const DeviceCreateInfo& info) : m_Impl(std::make_unique<Impl>(info)) {}

Device::~Device() noexcept = default;

void Device::beginFrame()
{
  m_Impl->beginFrame();
}

void Device::endFrame()
{
  m_Impl->endFrame();
}

void Device::execute(FrameGraph& fg)
{
  CommandList cmd = m_Impl->createCommandList(m_Impl->commandBuffer);
  fg.execute(cmd);
}

Format Device::swapchainFormat(SwapchainHandle handle) const
{
  VulkanSwapchainEntry* entry = m_Impl->swapchainPool.resolve(handle);
  return entry ? toSkyFormat(entry->swapchain.imageFormat()) : Format::Undefined;
}

void Device::waitIdle() const { m_Impl->waitIdle(); }

SwapchainHandle Device::createSwapchain(const SwapchainCreateInfo& info)
{
  return m_Impl->swapchainPool.allocate(
    makeSwapchainEntry(m_Impl->instance, m_Impl->device,
                          info.surfaceFactory, info.width, info.height));
}

void Device::destroySwapchain(SwapchainHandle handle) noexcept
{
  m_Impl->swapchainPool.deallocate(handle);
}

SwapchainHandle Device::defaultSwapchain() const noexcept
{
  return m_Impl->defaultSwapchainHandle;
}

Extent2D Device::swapchainExtent(SwapchainHandle handle) const
{
  VulkanSwapchainEntry* entry = m_Impl->swapchainPool.resolve(handle);
  if (!entry) return {};
  const auto [width, height] = entry->swapchain.extent();
  return { width, height };
}

BufferHandle Device::createBuffer(const BufferDesc& desc)
{
  return m_Impl->bufferPool.allocate(std::make_unique<VulkanBuffer>(m_Impl->device.allocator(), desc));
}

void Device::destroyBuffer(BufferHandle handle) noexcept
{
  m_Impl->bufferPool.deallocate(handle);
}

void* Device::mapBuffer(BufferHandle handle)
{
  VulkanBuffer* buf = m_Impl->bufferPool.resolve(handle);
  return buf ? buf->map() : nullptr;
}

void Device::unmapBuffer(BufferHandle handle)
{
  if (VulkanBuffer* buf = m_Impl->bufferPool.resolve(handle))
    buf->unmap();
}

void Device::uploadBufferData(BufferHandle handle, const void* data, uint64_t size)
{
  VulkanBuffer* dst = m_Impl->bufferPool.resolve(handle);
  if (!dst)
  {
    SKY_RHI_WARN("uploadBufferData: invalid buffer handle");
    return;
  }

  VulkanBuffer staging(m_Impl->device.allocator(),
    {size, BufferUsage::TransferSrc, MemoryType::CpuOnly});

  SKY_RHI_VK_CHECK(vmaCopyMemoryToAllocation(m_Impl->device.allocator(), data,
    staging.allocation(), 0, size),
    "Failed to copy data to staging buffer");

  m_Impl->immediateSubmit([&](VkCommandBuffer cmd)
  {
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, staging.handle(), dst->handle(), 1, &region);
  });
}

ShaderHandle Device::createShader(const ShaderDesc& desc)
{
  return m_Impl->shaderPool.allocate(
    std::make_unique<VulkanShaderModule>(m_Impl->device, desc.code, desc.codeSize));
}

void Device::destroyShader(ShaderHandle handle) noexcept
{
  m_Impl->shaderPool.deallocate(handle);
}

PipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
  VulkanShaderModule* vs = m_Impl->shaderPool.resolve(desc.vertexShader);
  VulkanShaderModule* fs = m_Impl->shaderPool.resolve(desc.fragmentShader);

  if (!vs || !fs)
  {
    SKY_RHI_ERROR("createGraphicsPipeline: invalid shader handle");
    return {};
  }

  VulkanDescriptorSetLayout* dsl = m_Impl->descriptorSetLayoutPool.resolve(desc.descriptorSetLayout);
  VkDescriptorSetLayout dslHandle = dsl ? dsl->handle() : VK_NULL_HANDLE;

  return m_Impl->pipelinePool.allocate(
    std::make_unique<VulkanPipeline>(m_Impl->device, desc, vs->handle(), fs->handle(), dslHandle));
}

void Device::destroyPipeline(PipelineHandle handle) noexcept
{
  m_Impl->pipelinePool.deallocate(handle);
}

TextureHandle Device::createTexture(const TextureDesc& desc)
{
  const VkImageUsageFlags usage = toVkImageUsage(desc.usage) | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  return m_Impl->texturePool.allocate(std::make_unique<VulkanImage>(
    m_Impl->device.allocator(), m_Impl->device.handle(),
    toVkFormat(desc.format), desc.width, desc.height,
    usage, VK_IMAGE_ASPECT_COLOR_BIT));
}

void Device::destroyTexture(TextureHandle handle) noexcept
{
  m_Impl->texturePool.deallocate(handle);
}

void Device::uploadTextureData(TextureHandle handle, const void* data, size_t size)
{
  VulkanImage* tex = m_Impl->texturePool.resolve(handle);
  if (!tex)
  {
    SKY_RHI_WARN("uploadTextureData: invalid texture handle");
    return;
  }

  VulkanBuffer staging(m_Impl->device.allocator(),
{ size, BufferUsage::TransferSrc, MemoryType::CpuOnly });
  vmaCopyMemoryToAllocation(m_Impl->device.allocator(), data, staging.allocation(), 0, size);

  m_Impl->immediateSubmit([&](VkCommandBuffer cmd)
  {
    transitionImageLayout(cmd, tex->handle(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      0, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { tex->width(), tex->height(), 1 };
    vkCmdCopyBufferToImage(cmd, staging.handle(), tex->handle(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transitionImageLayout(cmd, tex->handle(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  });
}
uint64_t Device::uploadTextureDataAsync(TextureHandle handle, const void* data, size_t size)
{
  return m_Impl->uploadTextureDataAsync(handle, data, size);
}

SamplerHandle Device::createSampler(const SamplerDesc& desc)
{
  return m_Impl->samplerPool.allocate(
    std::make_unique<VulkanSampler>(m_Impl->device.handle(), desc));
}

void Device::destroySampler(SamplerHandle handle) noexcept
{
  m_Impl->samplerPool.deallocate(handle);
}

DescriptorSetLayoutHandle Device::createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc)
{
  return m_Impl->descriptorSetLayoutPool.allocate(
    std::make_unique<VulkanDescriptorSetLayout>(m_Impl->device.handle(), desc));
}

void Device::destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) noexcept
{
  m_Impl->descriptorSetLayoutPool.deallocate(handle);
}

DescriptorSetHandle Device::createDescriptorSet(DescriptorSetLayoutHandle layout)
{
  VulkanDescriptorSetLayout* dsl = m_Impl->descriptorSetLayoutPool.resolve(layout);
  if (!dsl)
  {
    SKY_RHI_ERROR("createDescriptorSet: invalid layout");
    return {};
  }

  return m_Impl->descriptorSetPool.allocate(std::make_unique<VulkanDescriptorSet>(
    m_Impl->device.handle(), m_Impl->descriptorPool, dsl->handle()));
}

void Device::destroyDescriptorSet(DescriptorSetHandle handle)
{
  m_Impl->descriptorSetPool.deallocate(handle);
}

void Device::updateDescriptorSetTexture(DescriptorSetHandle setHandler, uint32_t binding,
                                        TextureHandle textureHandle, SamplerHandle samplerHandle)
{
  VulkanDescriptorSet* set = m_Impl->descriptorSetPool.resolve(setHandler);
  VulkanImage*         tex     = m_Impl->texturePool.resolve(textureHandle);
  VulkanSampler*       sampler = m_Impl->samplerPool.resolve(samplerHandle);

  if (!set || !tex || !sampler)
  {
    SKY_RHI_ERROR("updateDescriptorSetTexture: invalid handle");
    return;
  }

  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView   = tex->view();
  imageInfo.sampler     = sampler->handle();

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set->handle();
  write.dstBinding = binding;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(m_Impl->device.handle(), 1, &write, 0, nullptr);
}

void Device::updateDescriptorSetBuffer(DescriptorSetHandle setHandler, uint32_t binding,
                                       BufferHandle bufferHandle, uint64_t range, uint64_t offset)
{
  VulkanDescriptorSet* set    = m_Impl->descriptorSetPool.resolve(setHandler);
  VulkanBuffer*        buffer = m_Impl->bufferPool.resolve(bufferHandle);

  if (!set || !buffer)
  {
    SKY_RHI_ERROR("updateDescriptorSetBuffer: invalid handle");
    return;
  }

  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = buffer->handle();
  bufferInfo.offset = offset;
  bufferInfo.range = range;

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set->handle();
  write.dstBinding = binding;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  write.pBufferInfo = &bufferInfo;

  vkUpdateDescriptorSets(m_Impl->device.handle(), 1, &write, 0, nullptr);
}
void Device::flushTransfers()
{
  m_Impl->flushTransfers();
}

} // namespace Sky::RHI