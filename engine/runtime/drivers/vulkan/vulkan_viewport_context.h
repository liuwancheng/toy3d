#pragma once

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
    class VulkanCommandList;
    class VulkanDevice;

    class VulkanViewportContext final : public RHIViewportContext
    {
    public:
        VulkanViewportContext(
            VulkanDevice& device,
            RHISurfaceRef surface,
            RHIViewportContextDesc desc);
        ~VulkanViewportContext() override;

        RHIResult<std::unique_ptr<RHIFrameContext>> begin_frame() override;
        RHIStatus end_frame(
            std::unique_ptr<RHIFrameContext> frame,
            const std::vector<RHICommandListRef>& command_lists) override;
        RHIStatus abort_frame(std::unique_ptr<RHIFrameContext> frame) override;
        RHIStatus request_resize(std::uint32_t width, std::uint32_t height) override;

        // Backend-only entry point used by the frame context. Command buffers
        // are allocated from the active frame slot and submitted by end_frame.
        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>
            create_graphics_command_context();

    private:
        struct FrameSlot;

        RHIStatus recreate_swapchain();
        RHIStatus create_swapchain(VkSwapchainKHR old_swapchain);
        void destroy_swapchain();
        RHIStatus submit_active_frame(const std::vector<VulkanCommandList*>& command_lists);
        RHIStatus present_active_image();
        RHIStatus abort_active_frame();
        void finish_active_frame();

        VulkanDevice& vulkan_device;
        RHISurfaceRef viewport_surface;
        RHIViewportContextDesc viewport_desc;
        VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
        VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchain_extent{};
        std::vector<VkImage> swapchain_images;
        std::vector<VkImageView> swapchain_image_views;
        std::vector<RHITextureRef> present_textures;
        std::vector<RHITextureViewRef> present_views;
        std::vector<FrameSlot> frame_slots;
        std::vector<VkFence> image_fences;
        std::uint32_t current_frame_slot = 0;
        std::uint32_t active_image_index = 0;
        std::uint64_t active_frame_id = 0;
        bool frame_active = false;
        bool presentation_failed = false;
        bool resize_pending = false;
        std::uint32_t pending_width = 0;
        std::uint32_t pending_height = 0;
    };
}
