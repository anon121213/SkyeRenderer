#include "VulkanImage.h"

VulkanImage::VulkanImage(VmaAllocator allocator, VkDevice device, VkFormat format, uint32_t width,
                         uint32_t height, VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t mipLevels)
                           : m_Allocator(allocator), m_Device(device), m_Width(width), m_Height(height),
                             m_MipLevels(mipLevels), m_Format(format), m_Aspect(aspect), m_SingleMipViews(mipLevels, VK_NULL_HANDLE)
{
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = format;
  imageInfo.extent = { width, height, 1 };
  imageInfo.mipLevels = m_MipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = usage;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

  SKY_RHI_VK_CHECK(vmaCreateImage(m_Allocator, &imageInfo, &allocInfo,
                                  &m_Image, &m_Allocation, nullptr),
                                  "Failed to create image");

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_Image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange = { aspect, 0, mipLevels, 0, 1 };

  SKY_RHI_VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_View),
                   "Failed to create image view");
}

VulkanImage::~VulkanImage() noexcept
{
  vkDestroyImageView(m_Device, m_View, nullptr);

  for (auto view : m_SingleMipViews)
    vkDestroyImageView(m_Device, view, nullptr);

  vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
}

VkImageView VulkanImage::createMipView(uint32_t mip) const
{
  VkImageViewCreateInfo v{};
  v.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  v.image = m_Image;
  v.viewType = VK_IMAGE_VIEW_TYPE_2D;
  v.format = m_Format;
  v.subresourceRange = { m_Aspect, mip, 1, 0, 1 };
  VkImageView view = VK_NULL_HANDLE;
  vkCreateImageView(m_Device, &v, nullptr, &view);
  return view;
}

VkImageView VulkanImage::singleMipView(uint32_t mip)
{
  if (mip >= m_MipLevels)
  {
    SKY_RHI_WARN("Mip level out of bounds");
    return VK_NULL_HANDLE;
  }

  if (m_SingleMipViews[mip] == VK_NULL_HANDLE)
  {
    VkImageViewCreateInfo v{};
    v.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    v.image = m_Image;
    v.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v.format = m_Format;
    v.subresourceRange = { m_Aspect, mip, 1, 0, 1 };
    vkCreateImageView(m_Device, &v, nullptr, &m_SingleMipViews[mip]);
  }

  return m_SingleMipViews[mip];
}