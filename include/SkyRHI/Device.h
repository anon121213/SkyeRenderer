#pragma once

#include "Buffer.h"
#include "DescriptorSetlayout.h"
#include "Handle.h"
#include "Pipeline.h"
#include "Sampler.h"
#include "Shader.h"
#include "Swapchain.h"
#include "Texture.h"
#include "Types.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Sky::RHI
{

class FrameGraph;   // forward decl — execute() takes it by reference; full def in DeviceImpl.cpp
struct ImGuiVulkanInitInfo;   // debug interop (SkyRHI/ImGuiSupport.h) — friended below

struct DeviceCreateInfo
{
  BackendType backend = BackendType::Auto;
  std::string appName = "SkyRenderApp";
  std::string engineName = "SkyEngine";
  std::vector<const char*> requiredInstanceExtensions;
  SurfaceFactoryFn surfaceFactory;
  uint32_t initialWindowWidth = 1280;
  uint32_t initialWindowHeight = 720;
  bool enableValidation = false;
};

class Device
{
public:
  explicit Device(const DeviceCreateInfo&);
  ~Device() noexcept;

  Device(const Device &) = delete;
  Device& operator=(const Device &) = delete;

  void beginFrame();
  void endFrame();
  void execute(FrameGraph& fg);

  // Records a debug overlay into the current frame's command buffer, on top of the
  // scene (backbuffer): PRESENT->COLOR, dynamic rendering (load), callback, ->PRESENT.
  // The callback receives the raw VkCommandBuffer as void* (app casts it for ImGui).
  // Call between execute() and endFrame().
  void recordOverlay(const std::function<void(void*)>& record);
  [[nodiscard]] Format swapchainFormat(SwapchainHandle handle) const;

  void waitIdle() const;

  [[nodiscard]] SwapchainHandle createSwapchain(const SwapchainCreateInfo& info);
  [[nodiscard]] SwapchainHandle defaultSwapchain() const noexcept;
  [[nodiscard]] Extent2D swapchainExtent(SwapchainHandle handle) const;
  void destroySwapchain(SwapchainHandle handle) noexcept;

  [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc);
  void destroyBuffer(BufferHandle handle) noexcept;
  void uploadBufferData(BufferHandle handle, const void* data, uint64_t size);

  [[nodiscard]] void* mapBuffer(BufferHandle handle);
  void unmapBuffer(BufferHandle handle);

  [[nodiscard]] ShaderHandle createShader(const ShaderDesc& desc);
  void destroyShader(ShaderHandle handle) noexcept;

  [[nodiscard]] PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc);
  void destroyPipeline(PipelineHandle handle) noexcept;

  [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc);
  void destroyTexture(TextureHandle handle) noexcept;
  void uploadTextureData(TextureHandle handle, const void* data, size_t size);
  [[nodiscard]] uint64_t uploadTextureDataAsync(TextureHandle handle, const void* data, size_t size);

  [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& desc);
  void destroySampler(SamplerHandle handle) noexcept;

  [[nodiscard]] DescriptorSetLayoutHandle createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc);
  void destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) noexcept;

  [[nodiscard]] DescriptorSetHandle createDescriptorSet(DescriptorSetLayoutHandle layout);
  void destroyDescriptorSet(DescriptorSetHandle handle);
  void updateDescriptorSetTexture(DescriptorSetHandle setHandler, uint32_t binding, TextureHandle textureHandle, SamplerHandle samplerHandle);
  void updateDescriptorSetBuffer(DescriptorSetHandle setHandler, uint32_t binding, BufferHandle bufferHandle, uint64_t range, uint64_t offset = 0);
  void flushTransfers();

  void renderToTexture(TextureHandle target, uint32_t width, uint32_t height,
                       PipelineHandle pipeline, DescriptorSetHandle descSet,
                       const void* push, uint32_t push_size, uint32_t mipLevel = 0);

  struct Impl;

private:
  friend ImGuiVulkanInitInfo imguiVulkanInitInfo(Device&);   // debug interop needs raw handles from m_Impl

  std::unique_ptr<Impl> m_Impl;
};

}
