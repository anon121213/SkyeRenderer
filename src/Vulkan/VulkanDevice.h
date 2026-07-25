#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

class VulkanInstance;

class VulkanDevice
{
public:
  explicit VulkanDevice(const VulkanInstance& instance);
  ~VulkanDevice();

  VulkanDevice(const VulkanDevice &) = delete;
  VulkanDevice &operator=(const VulkanDevice &) = delete;

  [[nodiscard]] VkDevice handle() const { return m_Device; }
  [[nodiscard]] VkPhysicalDevice physicalDevice() const { return m_PhysicalDevice; }
  [[nodiscard]] VkQueue graphicQueue() const { return m_GraphicsQueue; }
  [[nodiscard]] uint32_t graphicFamilyIndex() const { return m_GraphicsFamily; }
  [[nodiscard]] VmaAllocator allocator() const { return m_Allocator; }
  [[nodiscard]] VkQueue transferQueue() const { return m_TransferQueue; }
  [[nodiscard]] uint32_t transferFamilyIndex() const { return m_TransferFamily; }
  [[nodiscard]] bool hasDedicatedTransfer() const { return m_HasDedicatedTransfer; }

private:
  VkDevice m_Device = VK_NULL_HANDLE;
  VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
  VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
  VkQueue m_TransferQueue = VK_NULL_HANDLE;
  VmaAllocator m_Allocator = VK_NULL_HANDLE;
  uint32_t m_GraphicsFamily = 0;
  uint32_t m_TransferFamily = 0;
  bool m_HasDedicatedTransfer = false;
};

