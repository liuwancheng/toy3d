#pragma once

#include "drivers/rhi/rhi_queue.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

namespace toy3d
{
    // Owns submission ordering and completion serials for one native queue.
    // Swapchain semaphores remain private to VulkanViewportContext and are not
    // represented by the public RHISubmitInfo contract.
    class VulkanQueue final : public RHIQueue
    {
    public:
        VulkanQueue(VkDevice device, VkQueue queue);
        ~VulkanQueue() override = default;

        RHISubmitSerial completed_serial() const override;
        RHIStatus wait(RHISubmitSerial serial) override;
        RHIStatus wait_idle() override;

        VkQueue native_handle() const;

    protected:
        RHIResult<RHISubmitResult> submit_impl(const RHISubmitInfo& info) override;

    private:
        VkDevice vk_device = VK_NULL_HANDLE;
        VkQueue vk_queue = VK_NULL_HANDLE;
        RHISubmitSerial completed_submit_serial = 0;
    };
}
