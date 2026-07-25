#include "skypch.h"

#include "VulkanDevice.h"
#include "VulkanInstance.h"

VulkanDevice::VulkanDevice(const VulkanInstance& instance)
{
  uint32_t devicesCount = 0;
  vkEnumeratePhysicalDevices(instance.handle(), &devicesCount, nullptr);

  if (devicesCount == 0)
  {
    SKY_RHI_ERROR("Failed to find Vulkan devices");
    throw std::runtime_error("Failed to find Vulkan devices");
  }

  std::vector<VkPhysicalDevice> devices(devicesCount);
  vkEnumeratePhysicalDevices(instance.handle(), &devicesCount, devices.data());

  for (const auto device : devices)
  {
    uint32_t deviceCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &deviceCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(deviceCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &deviceCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
      if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
      {
        m_PhysicalDevice = device;
        m_GraphicsFamily = i;
        break;
      }
    }

    if (m_PhysicalDevice != VK_NULL_HANDLE)
    {
      break;
    }
  }

  if (m_PhysicalDevice == VK_NULL_HANDLE)
  {
    SKY_RHI_ERROR("Failed to find Vulkan physical device");
    throw std::runtime_error("Failed to find Vulkan physical device");
  }

  uint32_t deviceCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &deviceCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(deviceCount);
  vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &deviceCount, queueFamilies.data());

  for (uint32_t i = 0; i < queueFamilies.size(); ++i)
  {
    if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 &&
        (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 &&
        (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)  == 0)
    {
      m_TransferFamily = i;
      m_HasDedicatedTransfer = true;
    }
  }

  float graphicsQueuePriority = 1.0f;
  float transferQueuePriority = 0.5f;

  std::vector<VkDeviceQueueCreateInfo> queueInfos;

  VkDeviceQueueCreateInfo queueGraphicsCreateInfo = {};
  queueGraphicsCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueGraphicsCreateInfo.queueFamilyIndex = m_GraphicsFamily;
  queueGraphicsCreateInfo.pQueuePriorities = &graphicsQueuePriority;
  queueGraphicsCreateInfo.queueCount = 1;
  queueInfos.push_back(queueGraphicsCreateInfo);

  if (m_HasDedicatedTransfer)
  {
    VkDeviceQueueCreateInfo queueTransferCreateInfo = {};
    queueTransferCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueTransferCreateInfo.queueFamilyIndex = m_TransferFamily;
    queueTransferCreateInfo.pQueuePriorities = &transferQueuePriority;
    queueTransferCreateInfo.queueCount = 1;
    queueInfos.push_back(queueTransferCreateInfo);
  }

  uint32_t deviceExtensionCount = 0;
  vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &deviceExtensionCount, nullptr);
  std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
  vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &deviceExtensionCount, deviceExtensions.data());

  std::vector<const char*> extensionNames;
  extensionNames.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  extensionNames.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

  for (const auto& device_extension : deviceExtensions)
  {
    if (std::strcmp(device_extension.extensionName, "VK_KHR_portability_subset") != 0)
      continue;

    extensionNames.push_back("VK_KHR_portability_subset");
  }

  VkPhysicalDeviceFeatures features{};

  VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeature{};
  timelineFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  timelineFeature.timelineSemaphore = VK_TRUE;

  VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeature{};
  dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
  dynamicRenderingFeature.dynamicRendering = VK_TRUE;
  dynamicRenderingFeature.pNext = &timelineFeature;

  VkDeviceCreateInfo deviceCreateInfo = {};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
  deviceCreateInfo.pQueueCreateInfos = queueInfos.data();
  deviceCreateInfo.pNext = &dynamicRenderingFeature;
  deviceCreateInfo.pEnabledFeatures = &features;
  deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
  deviceCreateInfo.ppEnabledExtensionNames = extensionNames.data();
  deviceCreateInfo.enabledLayerCount = 0;

  VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingQuery{};
  dynamicRenderingQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;

  VkPhysicalDeviceFeatures2 features2{};
  features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features2.pNext = &dynamicRenderingQuery;

  vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &features2);

  if (!dynamicRenderingQuery.dynamicRendering)
  {
    SKY_RHI_ERROR("Dynamic rendering not supported by physical device");
    throw std::runtime_error("Dynamic rendering not supported");
  }

  SKY_RHI_VK_CHECK(vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_Device),
               "Failed to create logical device");

  volkLoadDevice(m_Device);

  VmaVulkanFunctions vulkanFunctions{};
  vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.physicalDevice   = m_PhysicalDevice;
  allocatorInfo.device           = m_Device;
  allocatorInfo.instance         = instance.handle();
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
  allocatorInfo.pVulkanFunctions = &vulkanFunctions;

  SKY_RHI_VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_Allocator),
               "Failed to create VMA allocator");

  vkGetDeviceQueue(m_Device, m_GraphicsFamily, 0, &m_GraphicsQueue);
  if (m_HasDedicatedTransfer)
    vkGetDeviceQueue(m_Device, m_TransferFamily, 0, &m_TransferQueue);
  else
    m_TransferQueue = m_GraphicsQueue;

  SKY_RHI_INFO("Transfer queue: family = {} ({})", m_TransferFamily,
             m_HasDedicatedTransfer ? "dedicated DMA" : "fallback to graphics");
}

VulkanDevice::~VulkanDevice()
{
  vmaDestroyAllocator(m_Allocator);
  vkDestroyDevice(m_Device, nullptr);
}
