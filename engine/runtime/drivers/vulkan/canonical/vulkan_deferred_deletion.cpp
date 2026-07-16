#include "drivers/vulkan/canonical/vulkan_deferred_deletion.h"

#include <algorithm>
#include <utility>

namespace toy3d
{
    RHIStatus VulkanDeferredDeletionQueue::enqueue(
        RHISubmitSerial serial,
        DeletionCallback callback)
    {
        if (serial == 0 || !callback)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Deferred Vulkan deletion requires a valid submit serial and callback.");
        }
        entries.push_back({serial, std::move(callback)});
        return RHIStatus::success();
    }

    void VulkanDeferredDeletionQueue::release_completed(
        VkDevice device,
        RHISubmitSerial completed_serial)
    {
        const auto first_pending = std::stable_partition(
            entries.begin(), entries.end(),
            [completed_serial](const Entry& entry)
            {
                return entry.serial > completed_serial;
            });
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
}
