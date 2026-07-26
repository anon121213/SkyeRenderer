#pragma once

#include <vulkan/vulkan.h>
#include "SkyRHI/Types.h"

inline VkFormat toVkFormat(Sky::RHI::Format f)
{
  using F = Sky::RHI::Format;
  switch (f)
  {
  case F::RGB32_SFLOAT:  return VK_FORMAT_R32G32B32_SFLOAT;
  case F::RGBA32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
  case F::RG32_SFLOAT:   return VK_FORMAT_R32G32_SFLOAT;
  case F::RGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
  case F::RG16_SFLOAT:   return VK_FORMAT_R16G16_SFLOAT;
  case F::R16_SFLOAT:    return VK_FORMAT_R16_SFLOAT;
  case F::BGRA8_SRGB:    return VK_FORMAT_B8G8R8A8_SRGB;
  case F::RGBA8_SRGB:    return VK_FORMAT_R8G8B8A8_SRGB;
  case F::RGBA8_UNORM:   return VK_FORMAT_R8G8B8A8_UNORM;
  case F::D32_SFLOAT:    return VK_FORMAT_D32_SFLOAT;
  default:               return VK_FORMAT_UNDEFINED;
  }
}

inline VkImageUsageFlags toVkImageUsage(Sky::RHI::TextureUsage u)
{
  using U = Sky::RHI::TextureUsage;
  VkImageUsageFlags flags = 0;
  if (has(u, U::TransferSrc))            flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (has(u, U::TransferDst))            flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (has(u, U::Sampled))                flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  if (has(u, U::Storage))                flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  if (has(u, U::ColorAttachment))        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (has(u, U::DepthStencilAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  if (has(u, U::InputAttachment))        flags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
  return flags;
}

inline Sky::RHI::Format toSkyFormat(VkFormat f)
{
  switch (f)
  {
  case VK_FORMAT_B8G8R8A8_SRGB:  return Sky::RHI::Format::BGRA8_SRGB;
  case VK_FORMAT_R8G8B8A8_SRGB:  return Sky::RHI::Format::RGBA8_SRGB;
  case VK_FORMAT_B8G8R8A8_UNORM: return Sky::RHI::Format::BGRA8_UNORM;
  case VK_FORMAT_R8G8B8A8_UNORM: return Sky::RHI::Format::RGBA8_UNORM;
  default:                       return Sky::RHI::Format::Undefined;
  }
}

inline VkFilter toVkFilter(Sky::RHI::Filter f)
{
  return f == Sky::RHI::Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

inline VkSamplerMipmapMode toVkMipmapMode(Sky::RHI::SamplerMipmapMode m)
{
  return m == Sky::RHI::SamplerMipmapMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                                   : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

inline VkSamplerAddressMode toVkAddressMode(Sky::RHI::AddressMode a)
{
  using A = Sky::RHI::AddressMode;
  switch (a)
  {
  case A::Repeat:            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case A::MirroredRepeat:    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  case A::ClampToEdge:       return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  case A::ClampToBorder:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  case A::MirrorClampToEdge: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
  default:                   return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  }
}

inline VkDescriptorType toVkDescriptorType(Sky::RHI::DescriptorType t)
{
  using D = Sky::RHI::DescriptorType;
  switch (t)
  {
  case D::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  case D::UniformBuffer:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case D::StorageBuffer:        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  default:                      return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  }
}

inline VkShaderStageFlags toVkShaderStage(Sky::RHI::ShaderStage s)
{
  using S = Sky::RHI::ShaderStage;
  VkShaderStageFlags flags = 0;
  if (has(s, S::Vertex))   flags |= VK_SHADER_STAGE_VERTEX_BIT;
  if (has(s, S::Fragment)) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
  if (has(s, S::Compute))  flags |= VK_SHADER_STAGE_COMPUTE_BIT;
  return flags;
}