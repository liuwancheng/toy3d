#include "drivers/vulkan/vulkan_deferred_deletion.h"

#include <algorithm>
#include <utility>

namespace toy3d
{
    RHIStatus VulkanDeferredDeletionQueue::enqueue(RHIQueueCompletionValue retire_value, DeletionCallback callback)
    {
        if (retire_value == 0 || !callback)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Deferred Vulkan deletion requires a valid retire value and callback.");
        }
        entries.push_back({retire_value, std::move(callback)});
        return RHIStatus::success();
    }

    void VulkanDeferredDeletionQueue::release_completed(VkDevice device, RHIQueueCompletionValue completed_value)
    {
        const auto first_pending =
            std::stable_partition(entries.begin(), entries.end(), [completed_value](const Entry& entry)
                                  { return entry.retire_value > completed_value; });
        for (auto iterator = first_pending; iterator != entries.end(); ++iterator)
        {
            iterator->callback(device);
        }
        entries.erase(first_pending, entries.end());
    }

    void VulkanDeferredDeletionQueue::release_all(VkDevice device)
    {
        for (Entry& entry : entries)
        {
            entry.callback(device);
        }
        entries.clear();
    }

    std::size_t VulkanDeferredDeletionQueue::pending_count() const
    {
        return entries.size();
    }
} // namespace toy3d
