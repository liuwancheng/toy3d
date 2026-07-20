#include "drivers/vulkan/vulkan_memory_manager.h"

#include <algorithm>
#include <string>

namespace toy3d
{
    namespace
    {
        RHIErrorCode allocation_error_code(VkResult result)
        {
            switch (result)
            {
            case VK_ERROR_OUT_OF_HOST_MEMORY:
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            case VK_ERROR_TOO_MANY_OBJECTS:
                return RHIErrorCode::OutOfMemory;
            case VK_ERROR_DEVICE_LOST:
                return RHIErrorCode::DeviceLost;
            default:
                return RHIErrorCode::BackendFailure;
            }
        }

        VmaAllocationCreateInfo make_allocation_create_info(VulkanMemoryClass memory_class)
        {
            VmaAllocationCreateInfo create_info{};
            switch (memory_class)
            {
            case VulkanMemoryClass::DeviceLocal:
                create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                create_info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            case VulkanMemoryClass::Upload:
                create_info.flags =
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
                create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                break;
            case VulkanMemoryClass::Readback:
                create_info.flags =
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
                create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                break;
            }
            return create_info;
        }
    }

    VulkanMemoryManager::~VulkanMemoryManager()
    {
        shutdown();
    }

    RHIStatus VulkanMemoryManager::initialize(const VulkanMemoryManagerDesc& desc)
    {
        if (vma_allocator != VK_NULL_HANDLE)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan memory manager is already initialized.");
        }
        if (desc.instance == VK_NULL_HANDLE ||
            desc.physical_device == VK_NULL_HANDLE ||
            desc.device == VK_NULL_HANDLE)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan memory manager requires initialized instance, physical device, and device handles.");
        }

        VmaAllocatorCreateInfo create_info{};
        create_info.instance = desc.instance;
        create_info.physicalDevice = desc.physical_device;
        create_info.device = desc.device;
        create_info.vulkanApiVersion = desc.vulkan_api_version;

        const VkResult result = vmaCreateAllocator(&create_info, &vma_allocator);
        if (result != VK_SUCCESS)
        {
            vma_allocator = VK_NULL_HANDLE;
            return RHIStatus::failure(
                allocation_error_code(result),
                "vmaCreateAllocator failed with VkResult " +
                    std::to_string(static_cast<int>(result)) + ".");
        }
        manager_stats = {};
        return RHIStatus::success();
    }

    void VulkanMemoryManager::shutdown()
    {
        if (vma_allocator != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(vma_allocator);
            vma_allocator = VK_NULL_HANDLE;
        }
    }

    RHIResult<VulkanAllocatedBuffer> VulkanMemoryManager::create_buffer(
        const VkBufferCreateInfo& buffer_info,
        VulkanMemoryClass memory_class,
        const char* debug_name)
    {
        if (vma_allocator == VK_NULL_HANDLE)
        {
            return RHIResult<VulkanAllocatedBuffer>::failure(
                RHIErrorCode::NotReady,
                "Vulkan memory manager is not initialized.");
        }
        if (buffer_info.sType != VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO || buffer_info.size == 0)
        {
            return RHIResult<VulkanAllocatedBuffer>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer allocation requires a valid create info and non-zero size.");
        }

        const VmaAllocationCreateInfo allocation_create_info =
            make_allocation_create_info(memory_class);
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo allocation_info{};
        const VkResult result = vmaCreateBuffer(
            vma_allocator,
            &buffer_info,
            &allocation_create_info,
            &buffer,
            &allocation,
            &allocation_info);
        if (result != VK_SUCCESS)
        {
            return RHIResult<VulkanAllocatedBuffer>::failure(
                allocation_error_code(result),
                "vmaCreateBuffer failed with VkResult " +
                    std::to_string(static_cast<int>(result)) + ".");
        }

        if (debug_name != nullptr && debug_name[0] != '\0')
        {
            vmaSetAllocationName(vma_allocator, allocation, debug_name);
        }

        VulkanAllocatedBuffer allocated_buffer;
        allocated_buffer.buffer = buffer;
        allocated_buffer.allocation.handle = allocation;
        allocated_buffer.allocation.offset = allocation_info.offset;
        allocated_buffer.allocation.size = allocation_info.size;
        allocated_buffer.allocation.mapped_data = allocation_info.pMappedData;
        allocated_buffer.allocation.memory_type_index = allocation_info.memoryType;
        ++manager_stats.total_buffer_allocations;
        ++manager_stats.active_buffer_allocations;
        manager_stats.active_buffer_bytes += allocation_info.size;
        manager_stats.peak_active_bytes = std::max(
            manager_stats.peak_active_bytes,
            manager_stats.active_buffer_bytes + manager_stats.active_image_bytes);
        return RHIResult<VulkanAllocatedBuffer>::success(std::move(allocated_buffer));
    }

    void VulkanMemoryManager::destroy_buffer(VulkanAllocatedBuffer& buffer)
    {
        if (buffer.buffer == VK_NULL_HANDLE && buffer.allocation.handle == VK_NULL_HANDLE)
        {
            return;
        }
        const VkDeviceSize allocation_size = buffer.allocation.size;
        if (vma_allocator != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(vma_allocator, buffer.buffer, buffer.allocation.handle);
            ++manager_stats.total_buffer_destructions;
            if (manager_stats.active_buffer_allocations > 0)
            {
                --manager_stats.active_buffer_allocations;
            }
            manager_stats.active_buffer_bytes = allocation_size <= manager_stats.active_buffer_bytes
                ? manager_stats.active_buffer_bytes - allocation_size
                : 0;
        }
        buffer.buffer = VK_NULL_HANDLE;
        buffer.allocation = {};
    }

    RHIStatus VulkanMemoryManager::flush_allocation(
        const VulkanAllocation& allocation,
        VkDeviceSize offset,
        VkDeviceSize size)
    {
        if (vma_allocator == VK_NULL_HANDLE || allocation.handle == VK_NULL_HANDLE)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Vulkan allocation cannot be flushed.");
        }
        const VkResult result = vmaFlushAllocation(vma_allocator, allocation.handle, offset, size);
        if (result != VK_SUCCESS)
        {
            return RHIStatus::failure(
                allocation_error_code(result),
                "vmaFlushAllocation failed with VkResult " +
                    std::to_string(static_cast<int>(result)) + ".");
        }
        return RHIStatus::success();
    }

    RHIResult<VulkanAllocatedImage> VulkanMemoryManager::create_image(
        const VkImageCreateInfo& image_info,
        VulkanMemoryClass memory_class,
        const char* debug_name)
    {
        if (vma_allocator == VK_NULL_HANDLE)
        {
            return RHIResult<VulkanAllocatedImage>::failure(
                RHIErrorCode::NotReady,
                "Vulkan memory manager is not initialized.");
        }
        if (image_info.sType != VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO ||
            image_info.extent.width == 0 ||
            image_info.extent.height == 0 ||
            image_info.extent.depth == 0)
        {
            return RHIResult<VulkanAllocatedImage>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan image allocation requires a valid create info and non-zero extent.");
        }

        const VmaAllocationCreateInfo allocation_create_info =
            make_allocation_create_info(memory_class);
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo allocation_info{};
        const VkResult result = vmaCreateImage(
            vma_allocator,
            &image_info,
            &allocation_create_info,
            &image,
            &allocation,
            &allocation_info);
        if (result != VK_SUCCESS)
        {
            return RHIResult<VulkanAllocatedImage>::failure(
                allocation_error_code(result),
                "vmaCreateImage failed with VkResult " +
                    std::to_string(static_cast<int>(result)) + ".");
        }

        if (debug_name != nullptr && debug_name[0] != '\0')
        {
            vmaSetAllocationName(vma_allocator, allocation, debug_name);
        }

        VulkanAllocatedImage allocated_image;
        allocated_image.image = image;
        allocated_image.allocation.handle = allocation;
        allocated_image.allocation.offset = allocation_info.offset;
        allocated_image.allocation.size = allocation_info.size;
        allocated_image.allocation.mapped_data = allocation_info.pMappedData;
        allocated_image.allocation.memory_type_index = allocation_info.memoryType;
        ++manager_stats.total_image_allocations;
        ++manager_stats.active_image_allocations;
        manager_stats.active_image_bytes += allocation_info.size;
        manager_stats.peak_active_bytes = std::max(
            manager_stats.peak_active_bytes,
            manager_stats.active_buffer_bytes + manager_stats.active_image_bytes);
        return RHIResult<VulkanAllocatedImage>::success(std::move(allocated_image));
    }

    void VulkanMemoryManager::destroy_image(VulkanAllocatedImage& image)
    {
        if (image.image == VK_NULL_HANDLE && image.allocation.handle == VK_NULL_HANDLE)
        {
            return;
        }
        const VkDeviceSize allocation_size = image.allocation.size;
        if (vma_allocator != VK_NULL_HANDLE)
        {
            vmaDestroyImage(vma_allocator, image.image, image.allocation.handle);
            ++manager_stats.total_image_destructions;
            if (manager_stats.active_image_allocations > 0)
            {
                --manager_stats.active_image_allocations;
            }
            manager_stats.active_image_bytes = allocation_size <= manager_stats.active_image_bytes
                ? manager_stats.active_image_bytes - allocation_size
                : 0;
        }
        image.image = VK_NULL_HANDLE;
        image.allocation = {};
    }

    bool VulkanMemoryManager::is_initialized() const
    {
        return vma_allocator != VK_NULL_HANDLE;
    }

    VulkanMemoryManagerStats VulkanMemoryManager::statistics() const
    {
        return manager_stats;
    }
}
