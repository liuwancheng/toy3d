#include "drivers/vulkan/vulkan_upload_manager.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace toy3d
{
    VulkanUploadPage::VulkanUploadPage(VulkanMemoryManager& memory_manager, VulkanAllocatedBuffer allocated_buffer,
                                       VkDeviceSize capacity)
        : memory_manager_instance(&memory_manager), allocated_buffer(std::move(allocated_buffer)),
          page_capacity(capacity)
    {
    }

    VulkanUploadPage::~VulkanUploadPage()
    {
        if (memory_manager_instance != nullptr)
        {
            memory_manager_instance->destroy_buffer(allocated_buffer);
        }
    }

    VkBuffer VulkanUploadPage::buffer() const
    {
        return allocated_buffer.buffer;
    }

    void* VulkanUploadPage::mapped_data() const
    {
        return allocated_buffer.allocation.mapped_data;
    }

    VkDeviceSize VulkanUploadPage::capacity() const
    {
        return page_capacity;
    }

    VkDeviceSize VulkanUploadPage::used_size() const
    {
        return next_offset;
    }

    bool VulkanUploadPage::try_allocate(VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& offset)
    {
        const VkDeviceSize safe_alignment = std::max<VkDeviceSize>(alignment, 1);
        const VkDeviceSize remainder = next_offset % safe_alignment;
        const VkDeviceSize padding = remainder == 0 ? 0 : safe_alignment - remainder;
        if (padding > page_capacity - next_offset || size > page_capacity - next_offset - padding)
        {
            return false;
        }
        if (padding > 0)
        {
            // Uniform-page alignment gaps are initialized so native allocation
            // padding never exposes bytes left by an earlier recording.
            auto* padding_begin = static_cast<std::byte*>(mapped_data()) + next_offset;
            std::memset(padding_begin, 0, static_cast<std::size_t>(padding));
        }
        offset = next_offset + padding;
        next_offset = offset + size;
        return true;
    }

    RHIStatus VulkanUploadPage::flush(VkDeviceSize offset, VkDeviceSize size)
    {
        return memory_manager_instance->flush_allocation(allocated_buffer.allocation, offset, size);
    }

    void VulkanUploadPage::mark_submitted(RHIQueueCompletionValue completion_value)
    {
        page_retire_value = std::max(page_retire_value, completion_value);
    }

    RHIQueueCompletionValue VulkanUploadPage::retire_value() const
    {
        return page_retire_value;
    }

    void VulkanUploadPage::reset()
    {
        next_offset = 0;
        page_retire_value = 0;
    }

    VkBuffer VulkanUploadAllocation::buffer() const
    {
        return page ? page->buffer() : VK_NULL_HANDLE;
    }

    VulkanUploadManager::VulkanUploadManager(VulkanMemoryManager& memory_manager) : memory_manager(memory_manager)
    {
    }

    RHIResult<VulkanUploadAllocation> VulkanUploadManager::upload(const void* source_data, std::size_t source_size,
                                                                  VkDeviceSize alignment)
    {
        if (source_data == nullptr || source_size == 0)
        {
            return RHIResult<VulkanUploadAllocation>::failure(RHIErrorCode::InvalidArgument,
                                                              "Vulkan upload requires non-empty source data.");
        }
        const VkDeviceSize size = static_cast<VkDeviceSize>(source_size);
        if (static_cast<std::size_t>(size) != source_size)
        {
            return RHIResult<VulkanUploadAllocation>::failure(RHIErrorCode::InvalidArgument,
                                                              "Vulkan upload size exceeds VkDeviceSize.");
        }

        const bool dedicated = size > default_page_size / 2;
        if (dedicated || !current_page)
        {
            auto page = create_page(dedicated ? size : default_page_size);
            if (!page)
            {
                return RHIResult<VulkanUploadAllocation>::failure(page.status().code(), page.status().message());
            }
            if (dedicated)
            {
                VkDeviceSize offset = 0;
                page.value()->try_allocate(size, alignment, offset);
                VulkanUploadAllocation allocation{page.value(), offset, size, page.value()->mapped_data()};
                std::memcpy(allocation.mapped_data, source_data, source_size);
                const RHIStatus flush_status = allocation.page->flush(offset, size);
                if (!flush_status)
                {
                    return RHIResult<VulkanUploadAllocation>::failure(flush_status.code(), flush_status.message());
                }
                ++manager_stats.total_upload_count;
                manager_stats.total_upload_bytes += size;
                ++manager_stats.dedicated_upload_count;
                manager_stats.dedicated_upload_bytes += size;
                return RHIResult<VulkanUploadAllocation>::success(std::move(allocation));
            }
            current_page = std::move(page.value());
            manager_stats.peak_managed_page_count =
                std::max(manager_stats.peak_managed_page_count, (current_page ? std::size_t{1} : std::size_t{0}) +
                                                                    pending_pages.size() + available_pages.size());
        }

        VkDeviceSize offset = 0;
        if (!current_page->try_allocate(size, alignment, offset))
        {
            ++manager_stats.total_page_rollovers;
            pending_pages.push_back(std::move(current_page));
            if (!available_pages.empty())
            {
                current_page = std::move(available_pages.back());
                available_pages.pop_back();
            }
            else
            {
                auto page = create_page(default_page_size);
                if (!page)
                {
                    return RHIResult<VulkanUploadAllocation>::failure(page.status().code(), page.status().message());
                }
                current_page = std::move(page.value());
                manager_stats.peak_managed_page_count =
                    std::max(manager_stats.peak_managed_page_count, (current_page ? std::size_t{1} : std::size_t{0}) +
                                                                        pending_pages.size() + available_pages.size());
            }
            if (!current_page->try_allocate(size, alignment, offset))
            {
                return RHIResult<VulkanUploadAllocation>::failure(
                    RHIErrorCode::OutOfMemory, "A fresh Vulkan upload page could not satisfy the allocation.");
            }
        }

        // std::byte performs untyped mapped-memory offset arithmetic without
        // implying character data or a typed GPU element representation.
        auto* destination = static_cast<std::byte*>(current_page->mapped_data()) + offset;
        std::memcpy(destination, source_data, source_size);
        const RHIStatus flush_status = current_page->flush(offset, size);
        if (!flush_status)
        {
            return RHIResult<VulkanUploadAllocation>::failure(flush_status.code(), flush_status.message());
        }
        ++manager_stats.total_upload_count;
        manager_stats.total_upload_bytes += size;
        return RHIResult<VulkanUploadAllocation>::success(
            VulkanUploadAllocation{current_page, offset, size, destination});
    }

    void VulkanUploadManager::mark_submitted(const std::vector<std::shared_ptr<VulkanUploadPage>>& pages,
                                             RHIQueueCompletionValue completion_value)
    {
        if (completion_value == 0)
        {
            return;
        }
        for (const std::shared_ptr<VulkanUploadPage>& page : pages)
        {
            if (page)
            {
                page->mark_submitted(completion_value);
            }
        }
    }

    void VulkanUploadManager::release_completed(RHIQueueCompletionValue completed_value)
    {
        auto iterator = pending_pages.begin();
        while (iterator != pending_pages.end())
        {
            const std::shared_ptr<VulkanUploadPage>& page = *iterator;
            const RHIQueueCompletionValue retire_value = page->retire_value();
            const bool gpu_complete = retire_value == 0 || retire_value <= completed_value;
            if (gpu_complete && page.use_count() == 1)
            {
                page->reset();
                available_pages.push_back(std::move(*iterator));
                ++manager_stats.total_page_recycles;
                iterator = pending_pages.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    VulkanUploadManagerStats VulkanUploadManager::statistics() const
    {
        VulkanUploadManagerStats snapshot = manager_stats;
        snapshot.current_page_count = current_page ? 1 : 0;
        snapshot.pending_page_count = pending_pages.size();
        snapshot.available_page_count = available_pages.size();
        snapshot.current_page_used_bytes = current_page ? current_page->used_size() : 0;
        snapshot.current_page_capacity_bytes = current_page ? current_page->capacity() : 0;
        return snapshot;
    }

    void VulkanUploadManager::shutdown()
    {
        current_page.reset();
        pending_pages.clear();
        available_pages.clear();
    }

    RHIResult<std::shared_ptr<VulkanUploadPage>> VulkanUploadManager::create_page(VkDeviceSize capacity)
    {
        VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_info.size = capacity;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        auto buffer = memory_manager.create_buffer(buffer_info, VulkanAllocationUsage::CpuUpload, "Vulkan upload page");
        if (!buffer)
        {
            return RHIResult<std::shared_ptr<VulkanUploadPage>>::failure(buffer.status().code(),
                                                                         buffer.status().message());
        }
        if (buffer.value().allocation.mapped_data == nullptr)
        {
            memory_manager.destroy_buffer(buffer.value());
            return RHIResult<std::shared_ptr<VulkanUploadPage>>::failure(
                RHIErrorCode::BackendFailure, "VMA created an upload page without a persistent mapping.");
        }
        ++manager_stats.total_page_creations;
        return RHIResult<std::shared_ptr<VulkanUploadPage>>::success(
            std::make_shared<VulkanUploadPage>(memory_manager, std::move(buffer.value()), capacity));
    }
} // namespace toy3d
