#pragma once

#include "drivers/rhi/rhi_viewport_context.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace toy3d
{
    class VulkanCommandList;
    class VulkanDevice;
    class VulkanGenerationPublicationTracker;

    struct VulkanViewportObservation
    {
        std::uint64_t generation_publication_id = 0;
        std::uint64_t rejected_generation_construction_count = 0;
        std::size_t active_generation_count = 0;
        std::size_t retired_generation_count = 0;
        std::size_t frame_slot_count = 0;
        std::size_t image_state_count = 0;
        std::size_t pending_present_fence_count = 0;
        std::uint64_t fallback_queue_drain_count = 0;
        std::uint64_t discarded_semaphore_count = 0;
        bool swapchain_maintenance1_enabled = false;
    };

    class VulkanViewportContext final : public RHIViewportContext
    {
    public:
        VulkanViewportContext(
            VulkanDevice& device,
            RHISurfaceRef surface,
            RHIViewportContextDesc desc);
        ~VulkanViewportContext() override;

        RHIResult<std::unique_ptr<RHIFrameContext>> begin_frame() override;
        RHIResult<RHIFrameEndResult> end_frame(
            std::unique_ptr<RHIFrameContext> frame,
            const std::vector<RHICommandListRef>& command_lists) override;
        RHIStatus abort_frame(std::unique_ptr<RHIFrameContext> frame) override;
        RHIStatus request_resize(std::uint32_t width, std::uint32_t height) override;
        VulkanViewportObservation observation_snapshot() const;

        // Backend-only entry point used by the frame context. Command buffers
        // are allocated from the active frame slot and submitted by end_frame.
        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>
            create_graphics_command_context();

    private:
        struct FrameSlot;
        struct SwapchainImagePresentationState;
        struct SwapchainGeneration;

        RHIStatus recreate_swapchain();
        RHIStatus create_swapchain(
            VkSwapchainKHR old_swapchain,
            std::unique_ptr<SwapchainGeneration>& generation);
        void destroy_generation(SwapchainGeneration& generation);
        RHIStatus retire_generation(std::unique_ptr<SwapchainGeneration> generation);
        RHIStatus collect_retired_generations();
        RHIStatus submit_active_frame(const std::vector<VulkanCommandList*>& command_lists);
        RHIStatus present_active_image();
        RHIStatus abort_active_frame();
        RHIStatus latch_presentation_failure(const RHIStatus& status);
        RHIStatus latch_incomplete_active_frame_failure(
            const RHIStatus& status,
            const char* operation);
        void finish_active_frame();

        VulkanDevice& vulkan_device;
        RHISurfaceRef viewport_surface;
        RHIViewportContextDesc viewport_desc;
        std::unique_ptr<SwapchainGeneration> active_generation;
        std::vector<std::unique_ptr<SwapchainGeneration>> retired_generations;
        std::unique_ptr<VulkanGenerationPublicationTracker> publication_tracker;
        std::uint32_t active_image_index = 0;
        std::uint64_t active_frame_id = 0;
        std::uint64_t fallback_queue_drain_count = 0;
        std::uint64_t discarded_semaphore_count = 0;
        bool frame_active = false;
        RHIStatus presentation_failure;
        bool resize_pending = false;
        std::uint32_t pending_width = 0;
        std::uint32_t pending_height = 0;
    };
}
