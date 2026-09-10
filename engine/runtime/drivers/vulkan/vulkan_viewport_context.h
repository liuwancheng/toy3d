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
    class VulkanDeferredDeletionQueue;
    class VulkanQueue;
    class VulkanDescriptorPoolManager;
    class VulkanRenderPassResources;
    class VulkanSwapchain;
    class VulkanUploadPage;
    class VulkanUploadManager;

    struct VulkanFrameSlot
    {
        VkSemaphore image_acquired = VK_NULL_HANDLE;
        VkFence submission_fence = VK_NULL_HANDLE;
        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkCommandBuffer present_transition_command_buffer = VK_NULL_HANDLE;
        RHIQueueCompletionValue completion_value = 0;
        std::vector<RHICommandListRef> submitted_command_lists;
        std::vector<RHIResourceRef> submitted_resources;
        std::vector<std::shared_ptr<VulkanUploadPage>> submitted_upload_pages;
        std::vector<RHITextureViewRef> submitted_texture_views;
        std::vector<RHIGraphicsPipelineRef> submitted_graphics_pipelines;
        std::vector<RHIBindingSetRef> submitted_binding_sets;
        std::vector<std::shared_ptr<VulkanRenderPassResources>> submitted_render_pass_resources;
    };

    struct VulkanViewportObservation
    {
        std::uint64_t swapchain_publication_id = 0;
        std::uint64_t rejected_swapchain_construction_count = 0;
        std::size_t frame_slot_count = 0;
        std::size_t swapchain_image_count = 0;
        std::uint64_t recreate_queue_idle_count = 0;
    };

    class VulkanViewportContext final : public RHIViewportContext
    {
      public:
        VulkanViewportContext(const RHIDevice& owner, VkPhysicalDevice physical_device, VkDevice device,
                              VkSurfaceKHR surface, std::uint32_t graphics_queue_family, VulkanQueue& graphics_queue,
                              VulkanUploadManager& upload_manager, VulkanDescriptorPoolManager& descriptor_pool_manager,
                              VulkanDeferredDeletionQueue& deletion_queue,
                              RHISurfaceRef rhi_surface, RHIViewportContextDesc desc);
        ~VulkanViewportContext() override;

        RHIResult<RHIFrameEndResult> end_frame(std::unique_ptr<RHIFrameContext> frame,
                                               const std::vector<RHICommandListRef>& command_lists) override;
        RHIStatus abort_frame(std::unique_ptr<RHIFrameContext> frame) override;
        RHIStatus request_resize(const Extent& extent) override;
        VulkanViewportObservation observation_snapshot() const;

        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> create_graphics_command_context();

      protected:
        RHIResult<std::unique_ptr<RHIFrameContext>> begin_frame_impl() override;

      private:
        RHIStatus recreate_swapchain();
        RHIStatus create_frame_slots(std::uint32_t count, std::vector<VulkanFrameSlot>& output_slots);
        void destroy_frame_slots(std::vector<VulkanFrameSlot>& slots);
        RHIStatus submit_active_frame(const std::vector<VulkanCommandList*>& command_lists);
        RHIStatus present_active_image();
        RHIStatus abort_active_frame();
        RHIStatus latch_presentation_failure(const RHIStatus& status);
        RHIStatus latch_incomplete_active_frame_failure(const RHIStatus& status, const char* operation);
        void finish_active_frame();

        const RHIDevice& owner_device;
        VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
        VkDevice vk_device = VK_NULL_HANDLE;
        VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
        std::uint32_t graphics_queue_family = VK_QUEUE_FAMILY_IGNORED;
        VulkanQueue& graphics_queue;
        VulkanUploadManager& upload_manager;
        VulkanDescriptorPoolManager& descriptor_pool_manager;
        VulkanDeferredDeletionQueue& deletion_queue;
        RHISurfaceRef viewport_surface;
        RHIViewportContextDesc viewport_desc;
        std::unique_ptr<VulkanSwapchain> swapchain;
        std::vector<VulkanFrameSlot> frame_slots;
        std::uint32_t current_frame_slot = 0;
        std::uint32_t active_image_index = 0;
        std::uint64_t active_frame_id = 0;
        std::uint64_t swapchain_publication_id = 0;
        std::uint64_t rejected_swapchain_construction_count = 0;
        std::uint64_t recreate_queue_idle_count = 0;
        bool frame_active = false;
        RHIStatus presentation_failure;
        bool resize_pending = false;
        Extent pending_extent;
    };
} // namespace toy3d
