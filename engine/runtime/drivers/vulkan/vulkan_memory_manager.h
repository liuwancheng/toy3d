#pragma once

#include "drivers/rhi/rhi_result.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <utility>

namespace toy3d
{
    enum class VulkanAllocationUsage
    {
        // CPU access is not requested; VMA still selects the actual memory type.
        GpuOnly,
        CpuUpload,
        CpuReadback
    };

    struct VulkanMemoryManagerDesc
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        std::uint32_t vulkan_api_version = VK_API_VERSION_1_1;
    };

    struct VulkanAllocation
    {
        VmaAllocation handle = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
        void* mapped_data = nullptr;
        std::uint32_t memory_type_index = VK_MAX_MEMORY_TYPES;
    };

    struct VulkanAllocatedBuffer
    {
        VulkanAllocatedBuffer() = default;
        VulkanAllocatedBuffer(const VulkanAllocatedBuffer&) = delete;
        VulkanAllocatedBuffer& operator=(const VulkanAllocatedBuffer&) = delete;

        VulkanAllocatedBuffer(VulkanAllocatedBuffer&& other) noexcept
            : buffer(std::exchange(other.buffer, VK_NULL_HANDLE))
            , allocation(other.allocation)
        {
            other.allocation = {};
        }

        VulkanAllocatedBuffer& operator=(VulkanAllocatedBuffer&&) = delete;

        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocation allocation;
    };

    struct VulkanAllocatedImage
    {
        VulkanAllocatedImage() = default;
        VulkanAllocatedImage(const VulkanAllocatedImage&) = delete;
        VulkanAllocatedImage& operator=(const VulkanAllocatedImage&) = delete;

        VulkanAllocatedImage(VulkanAllocatedImage&& other) noexcept
            : image(std::exchange(other.image, VK_NULL_HANDLE))
            , allocation(other.allocation)
        {
            other.allocation = {};
        }

        VulkanAllocatedImage& operator=(VulkanAllocatedImage&&) = delete;

        VkImage image = VK_NULL_HANDLE;
        VulkanAllocation allocation;
    };

    struct VulkanMemoryManagerStats
    {
        std::uint64_t total_buffer_allocations = 0;
        std::uint64_t total_image_allocations = 0;
        std::uint64_t total_buffer_destructions = 0;
        std::uint64_t total_image_destructions = 0;
        std::uint64_t active_buffer_allocations = 0;
        std::uint64_t active_image_allocations = 0;
        VkDeviceSize active_buffer_bytes = 0;
        VkDeviceSize active_image_bytes = 0;
        VkDeviceSize peak_active_bytes = 0;
    };

    class VulkanMemoryManager final
    {
    public:
        VulkanMemoryManager() = default;
        ~VulkanMemoryManager();

        VulkanMemoryManager(const VulkanMemoryManager&) = delete;
        VulkanMemoryManager& operator=(const VulkanMemoryManager&) = delete;

        RHIStatus initialize(const VulkanMemoryManagerDesc& desc);
        void shutdown();

        RHIResult<VulkanAllocatedBuffer> create_buffer(
            const VkBufferCreateInfo& buffer_info,
            VulkanAllocationUsage allocation_usage,
            const char* debug_name = nullptr);
        void destroy_buffer(VulkanAllocatedBuffer& buffer);
        RHIStatus flush_allocation(
            const VulkanAllocation& allocation,
            VkDeviceSize offset,
            VkDeviceSize size);

        RHIResult<VulkanAllocatedImage> create_image(
            const VkImageCreateInfo& image_info,
            VulkanAllocationUsage allocation_usage,
            const char* debug_name = nullptr);
        void destroy_image(VulkanAllocatedImage& image);

        bool is_initialized() const;
        VulkanMemoryManagerStats statistics() const;

    private:
        VmaAllocator vma_allocator = VK_NULL_HANDLE;
        VulkanMemoryManagerStats manager_stats;
    };
}
