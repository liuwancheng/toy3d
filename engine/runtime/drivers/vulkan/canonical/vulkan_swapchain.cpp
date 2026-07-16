#include "drivers/vulkan/canonical/vulkan_swapchain.h"

#include "drivers/vulkan/canonical/vulkan_device.h"
#include "drivers/vulkan/canonical/vulkan_queue.h"

namespace toy3d
{
    VulkanSwapchain::VulkanSwapchain(
        VulkanDevice& device,
        RHISurfaceRef surface,
        RHISwapchainDesc desc)
        : RHISwapchain(std::move(desc))
        , vulkan_device(device)
        , rhi_surface(std::move(surface))
    {
    }

    VulkanSwapchain::~VulkanSwapchain()
    {
        shutdown();
    }

    RHIStatus VulkanSwapchain::initialize()
    {
        if (vulkan_device.device() == VK_NULL_HANDLE || !rhi_surface)
        {
            return RHIStatus::failure(
                RHIErrorCode::NotReady,
                "Vulkan swapchain requires an initialized device and surface.");
        }
        return RHIStatus::failure(
            RHIErrorCode::Unsupported,
            "VulkanSwapchain creation will be migrated from VulkanViewportContext in the next implementation stage.");
    }

    RHIStatus VulkanSwapchain::shutdown()
    {
        back_buffers.clear();
        if (vk_swapchain != VK_NULL_HANDLE && vulkan_device.device() != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(vulkan_device.device(), vk_swapchain, nullptr);
            vk_swapchain = VK_NULL_HANDLE;
        }
        return RHIStatus::success();
    }

    RHITextureRef VulkanSwapchain::back_buffer(std::uint32_t image_index) const
    {
        if (image_index >= back_buffers.size())
        {
            return nullptr;
        }
        return back_buffers[image_index];
    }

    VkSwapchainKHR VulkanSwapchain::native_handle() const
    {
        return vk_swapchain;
    }

    RHIResult<RHIAcquiredImage> VulkanSwapchain::acquire_next_image_impl()
    {
        return RHIResult<RHIAcquiredImage>::failure(
            RHIErrorCode::Unsupported,
            "VulkanSwapchain acquire is not implemented yet.");
    }

    RHIStatus VulkanSwapchain::present_impl(RHIQueue& queue, const RHIPresentInfo&)
    {
        if (dynamic_cast<VulkanQueue*>(&queue) == nullptr)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan swapchain presentation requires a VulkanQueue.");
        }
        return RHIStatus::failure(
            RHIErrorCode::Unsupported,
            "VulkanSwapchain present is not implemented yet.");
    }

    RHIStatus VulkanSwapchain::resize_impl(std::uint32_t, std::uint32_t)
    {
        return RHIStatus::failure(
            RHIErrorCode::Unsupported,
            "VulkanSwapchain resize is not implemented yet.");
    }
}
