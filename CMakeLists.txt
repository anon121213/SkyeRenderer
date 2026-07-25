cmake_minimum_required(VERSION 3.21)
project(SkyRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)

include(FetchContent)

# Once a dependency is cloned, don't re-run its git "update" step on reconfigure.
# Avoids a shallow-clone HEAD-hash failure some CMake versions hit on rebuild.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

FetchContent_Declare(
        vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG        v3.3.0
        GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(vma)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.15.0
        GIT_SHALLOW    TRUE
)
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG        vulkan-sdk-1.4.350.0
        GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(volk)

add_library(SkyRenderer STATIC
        src/Core/Log.cpp
        src/Vulkan/VulkanInstance.cpp
        src/Vulkan/VulkanDevice.cpp
        src/Vulkan/VulkanSurface.cpp
        src/Vulkan/VMAImplementation.cpp
        src/Vulkan/VulkanSwapchain.cpp
        src/Vulkan/VulkanShaderModule.cpp
        src/Vulkan/VulkanPipeline.cpp
        src/Vulkan/VulkanCommandPool.cpp
        src/Vulkan/DeviceImpl.cpp
        include/SkyRHI/Handle.h
        include/SkyRHI/Device.h
        src/Common/HandleAllocator.h
        include/SkyRHI/CommandList.h
        src/Vulkan/CommandList.cpp
        src/Vulkan/DeviceImpl.h
        include/SkyRHI/Swapchain.h
        include/SkyRHI/FrameGraph.h
        src/Common/FrameGraph.cpp
        include/SkyRHI/Buffer.h
        src/Vulkan/VulkanBuffer.h
        src/Vulkan/VulkanBuffer.cpp
        include/SkyRHI/Shader.h
        include/SkyRHI/Pipeline.h
        src/Vulkan/VulkanImage.h
        src/Vulkan/VulkanImage.cpp
        src/Vulkan/VulkanTranslate.h
        include/SkyRHI/Texture.h
        include/SkyRHI/Sampler.h
        src/Vulkan/VulkanSampler.h
        src/Vulkan/VulkanSampler.cpp
        include/SkyRHI/DescriptorSetlayout.h
        src/Vulkan/VulkanDescriptorSetLayout.cpp
        src/Vulkan/VulkanDescriptorSetLayout.h
        src/Vulkan/VulkanDescriptorSet.cpp
        src/Vulkan/VulkanDescriptorSet.h
)

target_include_directories(SkyRenderer
        PUBLIC include
        PRIVATE src
)

target_link_libraries(SkyRenderer PUBLIC
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        spdlog::spdlog
        volk
)

target_compile_definitions(SkyRenderer PRIVATE
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=1
)

target_precompile_headers(SkyRenderer PRIVATE src/skypch.h)

if (PROJECT_IS_TOP_LEVEL)
    add_subdirectory(SkyApp)
endif()
