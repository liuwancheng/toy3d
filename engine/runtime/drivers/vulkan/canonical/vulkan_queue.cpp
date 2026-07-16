#include "drivers/vulkan/canonical/vulkan_queue.h"

#include <string>

namespace toy3d
{
    namespace
    {
        RHIStatus make_queue_status(VkResult result, const char* operation)
        {
            if (result == VK_SUCCESS)
            {
                return RHIStatus::success();
            }
            return RHIStatus::failure(
                result == VK_ERROR_DEVICE_LOST ? RHIErrorCode::DeviceLost : RHIErrorCode::BackendFailure,
                std::string(operation) + " failed with VkResult " +
                    std::to_string(static_cast<int>(result)) + ".");
        }
    }

    VulkanQueue::VulkanQueue(VkDevice device, VkQueue queue)
        : vk_device(device)
        , vk_queue(queue)
    {
    }

    RHISubmitSerial VulkanQueue::completed_serial() const
    {
        return completed_submit_serial;
    }

    RHIStatus VulkanQueue::wait(RHISubmitSerial serial)
    {
        if (serial > completed_submit_serial)
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "Vulkan queue serial waits require per-submission fence tracking.");
        }
        return RHIStatus::success();
    }

    RHIStatus VulkanQueue::wait_idle()
    {
        if (vk_queue == VK_NULL_HANDLE)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Vulkan queue is not initialized.");
        }
        return make_queue_status(vkQueueWaitIdle(vk_queue), "vkQueueWaitIdle");
    }

    VkQueue VulkanQueue::native_handle() const
    {
        return vk_queue;
    }

    RHIResult<RHISubmitResult> VulkanQueue::submit_impl(const RHISubmitInfo&)
    {
        return RHIResult<RHISubmitResult>::failure(
            RHIErrorCode::Unsupported,
            "Canonical Vulkan queue submission requires submission fence ownership and is not implemented yet.");
    }
}
