#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

class VulkanImage
{
public:
  VulkanImage(VmaAllocator allocator, VkDevice device, VkFormat format,
    uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t mipLevels = 1);
  ~VulkanImage() noexcept;

  VulkanImage(const VulkanImage&) = delete;
  VulkanImage& operator=(const VulkanImage&) = delete;

  [[nodiscard]] VkImage     handle()    const { return m_Image; }
  [[nodiscard]] VkImageView view()      const { return m_View; }
  [[nodiscard]] uint32_t    width()     const { return m_Width; }
  [[nodiscard]] uint32_t    height()    const { return m_Height; }
  [[nodiscard]] uint32_t    mipLevels() const {return m_MipLevels; }

  [[nodiscard]] VkImageView createMipView(uint32_t mip) const;
  [[nodiscard]] VkImageView singleMipView(uint32_t mip);

private:
  VmaAllocator  m_Allocator  = VK_NULL_HANDLE;
  VkDevice      m_Device     = VK_NULL_HANDLE;
  VkImage       m_Image      = VK_NULL_HANDLE;
  VmaAllocation m_Allocation = VK_NULL_HANDLE;
  VkImageView   m_View       = VK_NULL_HANDLE;

  uint32_t m_Width  = 0;
  uint32_t m_Height = 0;

  uint32_t           m_MipLevels = 1;
  VkFormat           m_Format    = VK_FORMAT_UNDEFINED;
  VkImageAspectFlags m_Aspect    = 0;

  std::vector<VkImageView> m_SingleMipViews;
};