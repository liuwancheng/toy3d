#pragma once

#include "drivers/rhi/rhi_public_definitions.h"
#include "drivers/rhi/rhi_result.h"
#include "drivers/vulkan/vulkan_memory_manager.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace toy3d
{
    class VulkanUploadPage final
    {
    public:
        VulkanUploadPage(
            VulkanMemoryManager& memory_manager,
            VulkanAllocatedBuffer allocated_buffer,
            VkDeviceSize capacity);
        ~VulkanUploadPage();

        VulkanUploadPage(const VulkanUploadPage&) = delete;
        VulkanUploadPage& operator=(const VulkanUploadPage&) = delete;

        VkBuffer buffer() const;
        void* mapped_data() const;
        VkDeviceSize capacity() const;
        VkDeviceSize used_size() const;
        bool try_allocate(VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& offset);
        RHIStatus flush(VkDeviceSize offset, VkDeviceSize size);
        void mark_submitted(RHIQueueCompletionValue completion_value);
        RHIQueueCompletionValue retire_value() const;
        void reset();

    private:
        VulkanMemoryManager* memory_manager_instance = nullptr;
        VulkanAllocatedBuffer allocated_buffer;
        VkDeviceSize page_capacity = 0;
        VkDeviceSize next_offset = 0;
        RHIQueueCompletionValue page_retire_value = 0;
    };

    struct VulkanUploadAllocation
    {
        std::shared_ptr<VulkanUploadPage> page;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
        void* mapped_data = nullptr;

        VkBuffer buffer() const;
    };

    struct VulkanUploadManagerStats
    {
        std::uint64_t total_upload_count = 0;
        VkDeviceSize total_upload_bytes = 0;
        std::uint64_t total_page_creations = 0;
        std::uint64_t total_page_rollovers = 0;
        std::uint64_t total_page_recycles = 0;
        std::uint64_t dedicated_upload_count = 0;
        VkDeviceSize dedicated_upload_bytes = 0;
        std::size_t current_page_count = 0;
        std::size_t pending_page_count = 0;
        std::size_t available_page_count = 0;
        std::size_t peak_managed_page_count = 0;
        VkDeviceSize current_page_used_bytes = 0;
        VkDeviceSize current_page_capacity_bytes = 0;
    };

    class VulkanUploadManager final
    {
    public:
        explicit VulkanUploadManager(VulkanMemoryManager& memory_manager);
        ~VulkanUploadManager() = default;

        VulkanUploadManager(const VulkanUploadManager&) = delete;
        VulkanUploadManager& operator=(const VulkanUploadManager&) = delete;

        RHIResult<VulkanUploadAllocation> upload(
            const void* source_data,
            std::size_t source_size,
            VkDeviceSize alignment);
        void mark_submitted(
            const std::vector<std::shared_ptr<VulkanUploadPage>>& pages,
            RHIQueueCompletionValue completion_value);
        void release_completed(RHIQueueCompletionValue completed_value);
        VulkanUploadManagerStats statistics() const;
        void shutdown();

    private:
        RHIResult<std::shared_ptr<VulkanUploadPage>> create_page(VkDeviceSize capacity);

        static constexpr VkDeviceSize default_page_size = 16ULL * 1024ULL * 1024ULL;
        VulkanMemoryManager& memory_manager;
        std::shared_ptr<VulkanUploadPage> current_page;
        std::vector<std::shared_ptr<VulkanUploadPage>> pending_pages;
        std::vector<std::shared_ptr<VulkanUploadPage>> available_pages;
        VulkanUploadManagerStats manager_stats;
    };
}
