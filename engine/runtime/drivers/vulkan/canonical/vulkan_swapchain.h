#pragma once

#include "drivers/rhi/rhi_swapchain.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <vector>

namespace toy3d
{
    class VulkanDevice;
    class VulkanQueue;

    // Owns VkSwapchainKHR and the RHI wrappers for its externally-owned images.
    // Frame-slot semaphores, command pools, and pass scheduling belong to
    // VulkanViewportContext rather than this resource wrapper.
    class VulkanSwapchain final : public RHISwapchain
    {
    public:
        VulkanSwapchain(
            VulkanDevice& device,
            RHISurfaceRef surface,
            RHISwapchainDesc desc);
        ~VulkanSwapchain() override;

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        RHIStatus initialize();
        RHIStatus shutdown();

        RHITextureRef back_buffer(std::uint32_t image_index) const override;
        VkSwapchainKHR native_handle() const;

    protected:
        RHIResult<RHIAcquiredImage> acquire_next_image_impl() override;
        RHIStatus present_impl(RHIQueue& queue, const RHIPresentInfo& info) override;
        RHIStatus resize_impl(std::uint32_t width, std::uint32_t height) override;

    private:
        VulkanDevice& vulkan_device;
        RHISurfaceRef rhi_surface;
        VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
        std::vector<RHITextureRef> back_buffers;
    };
}
