#include "drivers/vulkan/canonical/vulkan_memory_allocator.h"

#include <string>

namespace toy3d
{
    VulkanMemoryAllocator::VulkanMemoryAllocator(
        VkPhysicalDevice physical_device,
        VkDevice device)
        : vk_physical_device(physical_device)
        , vk_device(device)
    {
    }

    RHIResult<VulkanAllocation> VulkanMemoryAllocator::allocate(
        const VkMemoryRequirements& requirements,
        VkMemoryPropertyFlags required_properties,
        VkMemoryPropertyFlags preferred_properties) const
    {
        if (vk_physical_device == VK_NULL_HANDLE || vk_device == VK_NULL_HANDLE || requirements.size == 0)
        {
            return RHIResult<VulkanAllocation>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan allocation requires initialized device handles and non-zero requirements.");
        }

        const auto memory_type = select_memory_type(
            requirements.memoryTypeBits,
            required_properties,
            preferred_properties);
        if (!memory_type)
        {
            return RHIResult<VulkanAllocation>::failure(
                memory_type.status().code(), memory_type.status().message());
        }

        VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate_info.allocationSize = requirements.size;
        allocate_info.memoryTypeIndex = memory_type.value();
        VkDeviceMemory memory = VK_NULL_HANDLE;
        const VkResult result = vkAllocateMemory(vk_device, &allocate_info, nullptr, &memory);
        if (result != VK_SUCCESS)
        {
            return RHIResult<VulkanAllocation>::failure(
                result == VK_ERROR_DEVICE_LOST ? RHIErrorCode::DeviceLost : RHIErrorCode::OutOfMemory,
                "vkAllocateMemory failed with VkResult " +
                    std::to_string(static_cast<int>(result)) + ".");
        }

        VulkanAllocation allocation;
        allocation.memory = memory;
        allocation.size = requirements.size;
        allocation.memory_type_index = memory_type.value();
        return RHIResult<VulkanAllocation>::success(allocation);
    }

    RHIStatus VulkanMemoryAllocator::release(VulkanAllocation& allocation) const
    {
        if (allocation.memory == VK_NULL_HANDLE)
        {
            return RHIStatus::success();
        }
        if (vk_device == VK_NULL_HANDLE)
        {
            return RHIStatus::failure(
                RHIErrorCode::NotReady,
                "Cannot release Vulkan memory after the device has been destroyed.");
        }
        vkFreeMemory(vk_device, allocation.memory, nullptr);
        allocation = {};
        return RHIStatus::success();
    }

    RHIResult<std::uint32_t> VulkanMemoryAllocator::select_memory_type(
        std::uint32_t memory_type_bits,
        VkMemoryPropertyFlags required_properties,
        VkMemoryPropertyFlags preferred_properties) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(vk_physical_device, &properties);

        std::uint32_t required_match = VK_MAX_MEMORY_TYPES;
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            if ((memory_type_bits & (1U << index)) == 0)
            {
                continue;
            }
            const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
            if ((flags & required_properties) != required_properties)
            {
                continue;
            }
            if ((flags & preferred_properties) == preferred_properties)
            {
                return RHIResult<std::uint32_t>::success(index);
            }
            if (required_match == VK_MAX_MEMORY_TYPES)
            {
                required_match = index;
            }
        }
        if (required_match != VK_MAX_MEMORY_TYPES)
        {
            return RHIResult<std::uint32_t>::success(required_match);
        }
        return RHIResult<std::uint32_t>::failure(
            RHIErrorCode::Unsupported,
            "No Vulkan memory type satisfies the requested properties.");
    }
}
