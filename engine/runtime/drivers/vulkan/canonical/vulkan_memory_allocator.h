#pragma once

#include "drivers/rhi/rhi_result.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>

namespace toy3d
{
    struct VulkanAllocation
    {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
        std::uint32_t memory_type_index = VK_MAX_MEMORY_TYPES;
    };

    // First-stage dedicated allocation policy. Suballocation can replace the
    // implementation later without changing VulkanBuffer/VulkanTexture.
    class VulkanMemoryAllocator final
    {
    public:
        VulkanMemoryAllocator(VkPhysicalDevice physical_device, VkDevice device);

        RHIResult<VulkanAllocation> allocate(
            const VkMemoryRequirements& requirements,
            VkMemoryPropertyFlags required_properties,
            VkMemoryPropertyFlags preferred_properties = 0) const;
        RHIStatus release(VulkanAllocation& allocation) const;

    private:
        RHIResult<std::uint32_t> select_memory_type(
            std::uint32_t memory_type_bits,
            VkMemoryPropertyFlags required_properties,
            VkMemoryPropertyFlags preferred_properties) const;

        VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
        VkDevice vk_device = VK_NULL_HANDLE;
    };
}
