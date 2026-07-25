#pragma once

// ── Debug-only interop for Dear ImGui's Vulkan backend ──────────────────────
// Exposes raw Vulkan handles that ImGui_ImplVulkan_Init needs. Segregated from
// the clean handle-based Device.h so the core API stays free of VkFoo types —
// include this ONLY in app-side debug/UI code.
// ────────────────────────────────────────────────────────────────────────────

#include <volk.h>

#include <cstdint>

namespace Sky::RHI
{

class Device;

struct ImGuiVulkanInitInfo
{
  VkInstance       instance       = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice         device         = VK_NULL_HANDLE;
  uint32_t         queueFamily    = 0;
  VkQueue          queue          = VK_NULL_HANDLE;
  VkFormat         colorFormat    = VK_FORMAT_UNDEFINED;   // swapchain color format (for dynamic rendering)
  uint32_t         imageCount     = 0;
  uint32_t         minImageCount  = 0;
};

// Pulls the raw Vulkan handles out of the device for ImGui's Vulkan backend init.
[[nodiscard]] ImGuiVulkanInitInfo imguiVulkanInitInfo(Device& device);

}
