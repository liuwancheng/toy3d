#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "drivers/rhi/rhi_viewport_context.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace toy3d
{
    class VulkanDevice;

    struct VulkanSwapchainImage
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView image_view = VK_NULL_HANDLE;
        RHITextureRef texture;
        RHITextureViewRef view;
        VkSemaphore rendering_done = VK_NULL_HANDLE;

        // The frame slot owns this fence. It only records the last graphics use
        // of this image and must never be destroyed through this non-owning link.
        VkFence last_submission_fence = VK_NULL_HANDLE;
    };

    struct VulkanAcquireResult
    {
        std::uint32_t image_index = 0;
        RHIStatus presentation_status;
    };

    RHIStatus map_vulkan_acquire_result(VkResult result);
    RHIStatus map_vulkan_present_result(VkResult result);
    std::uint32_t vulkan_frame_slot_count(std::uint32_t actual_image_count);

    class VulkanSwapchain final
    {
    public:
        explicit VulkanSwapchain(VulkanDevice& device);
        ~VulkanSwapchain();

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        static RHIResult<std::unique_ptr<VulkanSwapchain>> create(
            VulkanDevice& device,
            const RHIViewportContextDesc& desc,
            VkSwapchainKHR old_swapchain);

        RHIResult<VulkanAcquireResult> acquire_image(VkSemaphore image_acquired);
        RHIStatus present(
            VkQueue queue,
            std::uint32_t image_index,
            VkSemaphore rendering_done);

        VkSwapchainKHR native_handle() const;
        VkExtent2D extent() const;
        VkFormat format() const;
        std::uint32_t image_count() const;
        VulkanSwapchainImage& image(std::uint32_t image_index);
        const VulkanSwapchainImage& image(std::uint32_t image_index) const;

    private:
        RHIStatus initialize(
            const RHIViewportContextDesc& desc,
            VkSwapchainKHR old_swapchain);

        VulkanDevice& vulkan_device;
        VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
        VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchain_extent{};
        std::vector<VulkanSwapchainImage> swapchain_images;
    };
}
