#pragma once

#include "Common/HandleAllocator.h"
#include "SkyRHI/CommandList.h"
#include "SkyRHI/Device.h"
#include "VulkanBuffer.h"
#include "VulkanCommandPool.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanDevice.h"
#include "VulkanImage.h"
#include "VulkanInstance.h"
#include "VulkanPipeline.h"
#include "VulkanSampler.h"
#include "VulkanShaderModule.h"
#include "VulkanSurface.h"
#include "VulkanSwapchain.h"

class VulkanDescriptorSet;
namespace Sky::RHI
{

struct VulkanSwapchainEntry
{
  VulkanSurface surface;
  VulkanSwapchain swapchain;

  VulkanSwapchainEntry(const VulkanInstance& instance,
    const VulkanDevice& device,
    const SurfaceFactory& factory,
    const uint32_t width,
    const uint32_t height)
      : surface(instance, factory)
      , swapchain(device, surface, width, height) {}
};

struct PendingUpload
{
  std::unique_ptr<VulkanBuffer> staging;
  VkCommandBuffer               cmd    = VK_NULL_HANDLE;
  uint64_t                      target = 0;
};

struct Device::Impl
{
  VulkanInstance      instance;
  VulkanDevice        device;

  HandleAllocator<SwapchainHandle, VulkanSwapchainEntry> swapchainPool;
  SwapchainHandle       defaultSwapchainHandle;
  VulkanSwapchainEntry* defaultEntry = nullptr;

  HandleAllocator<PipelineHandle, VulkanPipeline> pipelinePool;

  VulkanCommandPool   commandPool;

  VkCommandBuffer          commandBuffer     = VK_NULL_HANDLE;
  VkSemaphore              imageAvailable    = VK_NULL_HANDLE;
  std::vector<VkSemaphore> renderFinished;

  VkFence                  inFlight          = VK_NULL_HANDLE;
  uint32_t                 currentImageIndex = 0;

  VkSemaphore              transferTimeline  = VK_NULL_HANDLE;
  uint64_t                 transferValue     = 0;

  HandleAllocator<BufferHandle, VulkanBuffer> bufferPool;

  HandleAllocator<ShaderHandle, VulkanShaderModule> shaderPool;

  HandleAllocator<TextureHandle, VulkanImage> texturePool;

  HandleAllocator<SamplerHandle, VulkanSampler> samplerPool;

  HandleAllocator<DescriptorSetLayoutHandle, VulkanDescriptorSetLayout> descriptorSetLayoutPool;

  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  HandleAllocator<DescriptorSetHandle, VulkanDescriptorSet> descriptorSetPool;

  std::vector<std::unique_ptr<VulkanImage>> frameTransients;

  std::vector<PendingUpload> pendingUploads;

  [[nodiscard]] uint64_t uploadTextureDataAsync(TextureHandle handle, const void* data, size_t size);

  void collectFinishedTransfers()
  {
    if (pendingUploads.empty()) return;

    uint64_t completed = 0;
    vkGetSemaphoreCounterValue(device.handle(), transferTimeline, &completed);

    std::vector<PendingUpload> stillPending;
    for (auto& u : pendingUploads)
    {
      if (u.target <= completed)
        commandPool.free(u.cmd);
      else
        stillPending.push_back(std::move(u));
    }
    pendingUploads = std::move(stillPending);
  }

  [[nodiscard]] CommandList createCommandList(VkCommandBuffer cmd) noexcept
  {
    return {cmd, this};
  }

  void beginFrame();
  void endFrame();
  void waitIdle() const;
  void recordOverlay(const std::function<void(void*)>& record);   // ImGui/debug overlay into backbuffer

  void immediateSubmit(const std::function<void(VkCommandBuffer)>& record);
  void flushTransfers();

  void renderToTexture(VulkanImage* target, uint32_t width, uint32_t height,
                       PipelineHandle pipeline, DescriptorSetHandle descSet,
                       const void* push, uint32_t push_size, uint32_t mipLevel = 0);

  void renderShadowMap(VulkanImage* shadowMap, PipelineHandle pipeline,
                       BufferHandle vb, BufferHandle ib, uint32_t indexCount,
                       uint32_t size, const void* push, uint32_t pushSize);

  explicit Impl(const DeviceCreateInfo& info);
  ~Impl();
};

}
