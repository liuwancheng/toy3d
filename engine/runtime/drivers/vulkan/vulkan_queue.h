#pragma once

#include "drivers/rhi/rhi_queue.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <deque>
#include <mutex>
#include <vector>

namespace toy3d
{
    class RHIDevice;
    class VulkanCommandList;
    class VulkanTexture;
    class VulkanUploadManager;
    // Owns submission ordering and completion serials for one native queue.
    // Swapchain semaphores remain private to VulkanViewportContext and are not
    // represented by the public RHISubmitInfo contract.
    class VulkanQueue final : public RHIQueue
    {
      public:
        VulkanQueue(const RHIDevice& owner, VkDevice device, VkQueue queue, VulkanUploadManager& upload_manager);
        ~VulkanQueue() override;

        RHIQueueCompletionValue completed_value() const override;
        RHIStatus wait_for_value(RHIQueueCompletionValue value) override;
        RHIStatus wait_idle() override;

        VkQueue native_handle() const;
        RHIResult<RHISubmitResult> submit_viewport(const std::vector<VulkanCommandList*>& command_lists,
                                                   const std::vector<VkCommandBuffer>& command_buffers,
                                                   VulkanTexture& presentation_texture, VkSemaphore wait_semaphore,
                                                   VkPipelineStageFlags wait_stage, VkSemaphore signal_semaphore,
                                                   VkFence completion_fence);

      protected:
        RHIResult<RHISubmitResult> submit_impl(const RHISubmitInfo& info) override;

      private:
        struct PendingSubmission
        {
            RHIQueueCompletionValue completion_value = 0;
            VkFence fence = VK_NULL_HANDLE;
            bool owns_fence = false;
            std::vector<RHICommandListRef> retained_command_lists;
        };

        RHIResult<RHISubmitResult> submit_native(const std::vector<VkCommandBuffer>& command_buffers,
                                                 VkSemaphore wait_semaphore, VkPipelineStageFlags wait_stage,
                                                 VkSemaphore signal_semaphore, VkFence completion_fence,
                                                 bool owns_fence, const std::vector<VulkanCommandList*>& command_lists,
                                                 VulkanTexture* presentation_texture,
                                                 std::vector<RHICommandListRef> retained_command_lists = {});
        void update_completed_value_locked() const;
        void release_pending_submissions_locked();

        const RHIDevice& owner_device;
        VkDevice vk_device = VK_NULL_HANDLE;
        VkQueue vk_queue = VK_NULL_HANDLE;
        VulkanUploadManager& upload_manager;
        mutable std::mutex queue_mutex;
        mutable std::deque<PendingSubmission> pending_submissions;
        mutable RHIQueueCompletionValue last_completed_value = 0;
        RHIQueueCompletionValue next_completion_value = 1;
    };
} // namespace toy3d
