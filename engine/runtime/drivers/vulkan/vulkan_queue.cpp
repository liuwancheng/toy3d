#include "drivers/vulkan/vulkan_queue.h"

#include "drivers/vulkan/vulkan_command_context.h"
#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_upload_manager.h"

#include <string>
#include <utility>

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
                std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result)) + ".");
        }
    } // namespace

    VulkanQueue::VulkanQueue(const RHIDevice& owner, VkDevice device, VkQueue queue, VulkanUploadManager& manager)
        : RHIQueue(owner), owner_device(owner), vk_device(device), vk_queue(queue), upload_manager(manager)
    {
    }

    VulkanQueue::~VulkanQueue()
    {
        if (vk_queue != VK_NULL_HANDLE)
        {
            vkQueueWaitIdle(vk_queue);
        }
        std::lock_guard<std::mutex> lock(queue_mutex);
        release_pending_submissions_locked();
    }

    RHIQueueCompletionValue VulkanQueue::completed_value() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        update_completed_value_locked();
        return last_completed_value;
    }

    RHIStatus VulkanQueue::wait_for_value(RHIQueueCompletionValue value)
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        update_completed_value_locked();
        if (value == 0 || value >= next_completion_value)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Vulkan queue wait requires a submitted completion value.");
        }
        if (value <= last_completed_value)
        {
            return RHIStatus::success();
        }
        for (const PendingSubmission& submission : pending_submissions)
        {
            if (submission.completion_value == value)
            {
                const RHIStatus status = make_queue_status(
                    vkWaitForFences(vk_device, 1, &submission.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
                if (status)
                {
                    update_completed_value_locked();
                }
                return status;
            }
        }
        return RHIStatus::failure(RHIErrorCode::BackendFailure, "Vulkan queue lost a pending submit fence.");
    }

    RHIStatus VulkanQueue::wait_idle()
    {
        if (vk_queue == VK_NULL_HANDLE)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Vulkan queue is not initialized.");
        }
        std::lock_guard<std::mutex> lock(queue_mutex);
        const RHIStatus status = make_queue_status(vkQueueWaitIdle(vk_queue), "vkQueueWaitIdle");
        if (status)
        {
            last_completed_value = next_completion_value - 1;
            release_pending_submissions_locked();
            upload_manager.release_completed(last_completed_value);
        }
        return status;
    }

    VkQueue VulkanQueue::native_handle() const
    {
        return vk_queue;
    }

    RHIResult<RHISubmitResult> VulkanQueue::submit_impl(const RHISubmitInfo& info)
    {
        if (vk_device == VK_NULL_HANDLE || vk_queue == VK_NULL_HANDLE)
        {
            return RHIResult<RHISubmitResult>::failure(RHIErrorCode::NotReady, "Vulkan queue is not initialized.");
        }
        std::vector<VkCommandBuffer> command_buffers;
        std::vector<std::shared_ptr<VulkanCommandList>> vulkan_command_lists;
        std::vector<VulkanCommandList*> state_command_lists;
        command_buffers.reserve(info.command_lists.size());
        state_command_lists.reserve(info.command_lists.size());
        for (const RHICommandListRef& command_list : info.command_lists)
        {
            const auto vulkan_command_list = std::dynamic_pointer_cast<VulkanCommandList>(command_list);
            if (!vulkan_command_list)
            {
                return RHIResult<RHISubmitResult>::failure(RHIErrorCode::InvalidArgument,
                                                           "Vulkan queue requires Vulkan command lists.");
            }
            if (!vulkan_command_list->is_device_level())
            {
                return RHIResult<RHISubmitResult>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan generic queue submission accepts only device-level command "
                    "lists; viewport lists must be submitted by their viewport context.");
            }
            for (const auto& previous : vulkan_command_lists)
            {
                if (vulkan_command_list->has_state_overlap(*previous))
                {
                    return RHIResult<RHISubmitResult>::failure(
                        RHIErrorCode::Unsupported,
                        "Vulkan cannot yet submit multiple command lists with overlapping state transitions.");
                }
            }
            vulkan_command_lists.push_back(vulkan_command_list);
            state_command_lists.push_back(vulkan_command_list.get());
            command_buffers.push_back(vulkan_command_list->command_buffer());
        }

        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        const VkResult result = vkCreateFence(vk_device, &fence_info, nullptr, &fence);
        if (result != VK_SUCCESS)
        {
            const RHIStatus status = make_queue_status(result, "vkCreateFence");
            return RHIResult<RHISubmitResult>::failure(status.code(), status.message());
        }
        auto submit_result = submit_native(command_buffers, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, fence, true,
                                           state_command_lists, nullptr, info.command_lists);
        return submit_result;
    }

    RHIResult<RHISubmitResult> VulkanQueue::submit_viewport(const std::vector<VulkanCommandList*>& command_lists,
                                                            const std::vector<VkCommandBuffer>& command_buffers,
                                                            VulkanTexture& presentation_texture,
                                                            VkSemaphore wait_semaphore, VkPipelineStageFlags wait_stage,
                                                            VkSemaphore signal_semaphore, VkFence completion_fence)
    {
        if (wait_semaphore == VK_NULL_HANDLE || signal_semaphore == VK_NULL_HANDLE ||
            completion_fence == VK_NULL_HANDLE || wait_stage == 0)
        {
            return RHIResult<RHISubmitResult>::failure(
                RHIErrorCode::InvalidArgument, "Vulkan viewport submission requires valid synchronization objects.");
        }
        return submit_native(command_buffers, wait_semaphore, wait_stage, signal_semaphore, completion_fence, false,
                             command_lists, &presentation_texture);
    }

    RHIResult<RHISubmitResult> VulkanQueue::submit_native(const std::vector<VkCommandBuffer>& command_buffers,
                                                          VkSemaphore wait_semaphore, VkPipelineStageFlags wait_stage,
                                                          VkSemaphore signal_semaphore, VkFence completion_fence,
                                                          bool owns_fence,
                                                          const std::vector<VulkanCommandList*>& command_lists,
                                                          VulkanTexture* presentation_texture,
                                                          std::vector<RHICommandListRef> retained_command_lists)
    {
        if (vk_device == VK_NULL_HANDLE || vk_queue == VK_NULL_HANDLE || command_buffers.empty() ||
            completion_fence == VK_NULL_HANDLE)
        {
            if (owns_fence && completion_fence != VK_NULL_HANDLE && vk_device != VK_NULL_HANDLE)
            {
                vkDestroyFence(vk_device, completion_fence, nullptr);
            }
            return RHIResult<RHISubmitResult>::failure(
                RHIErrorCode::NotReady, "Vulkan queue submission requires initialized handles and command buffers.");
        }

        VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        if (wait_semaphore != VK_NULL_HANDLE)
        {
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &wait_semaphore;
            submit_info.pWaitDstStageMask = &wait_stage;
        }
        submit_info.commandBufferCount = static_cast<std::uint32_t>(command_buffers.size());
        submit_info.pCommandBuffers = command_buffers.data();
        if (signal_semaphore != VK_NULL_HANDLE)
        {
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &signal_semaphore;
        }

        std::lock_guard<std::mutex> lock(queue_mutex);
        if (presentation_texture != nullptr && !presentation_texture->is_owned_by(owner_device))
        {
            if (owns_fence)
            {
                vkDestroyFence(vk_device, completion_fence, nullptr);
            }
            return RHIResult<RHISubmitResult>::failure(RHIErrorCode::InvalidArgument,
                                                       "Vulkan queue presentation texture belongs to another device.");
        }
        for (const VulkanCommandList* command_list : command_lists)
        {
            if (command_list == nullptr || !command_list->is_owned_by(owner_device))
            {
                if (owns_fence)
                {
                    vkDestroyFence(vk_device, completion_fence, nullptr);
                }
                return RHIResult<RHISubmitResult>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan queue submission contains a command list from another device.");
            }
            const RHIStatus state_status = command_list->validate_committed_resource_states();
            if (!state_status)
            {
                if (owns_fence)
                {
                    vkDestroyFence(vk_device, completion_fence, nullptr);
                }
                return RHIResult<RHISubmitResult>::failure(state_status.code(), state_status.message());
            }
        }
        const VkResult result = vkQueueSubmit(vk_queue, 1, &submit_info, completion_fence);
        if (result != VK_SUCCESS)
        {
            if (owns_fence)
            {
                vkDestroyFence(vk_device, completion_fence, nullptr);
            }
            const RHIStatus status = make_queue_status(result, "vkQueueSubmit");
            return RHIResult<RHISubmitResult>::failure(status.code(), status.message());
        }
        const RHIQueueCompletionValue completion_value = next_completion_value++;
        for (VulkanCommandList* command_list : command_lists)
        {
            command_list->commit_resource_states();
            for (const RHIResourceRef& resource : command_list->retained_resources())
            {
                if (const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(resource))
                {
                    buffer->mark_used(completion_value);
                }
                else if (const auto texture = std::dynamic_pointer_cast<VulkanTexture>(resource))
                {
                    texture->mark_used(completion_value);
                }
                else if (const auto readback = std::dynamic_pointer_cast<VulkanReadback>(resource))
                {
                    readback->mark_used(completion_value);
                }
            }
            upload_manager.mark_submitted(command_list->retained_upload_pages(), completion_value);
        }
        if (presentation_texture != nullptr)
        {
            presentation_texture->set_state(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, RHIAccess::Present);
            presentation_texture->mark_used(completion_value);
        }
        pending_submissions.push_back(
            {completion_value, completion_fence, owns_fence, std::move(retained_command_lists)});
        return RHIResult<RHISubmitResult>::success({completion_value});
    }

    void VulkanQueue::update_completed_value_locked() const
    {
        const RHIQueueCompletionValue previous_completed_value = last_completed_value;
        while (!pending_submissions.empty())
        {
            const PendingSubmission& submission = pending_submissions.front();
            const VkResult result = vkGetFenceStatus(vk_device, submission.fence);
            if (result == VK_NOT_READY)
            {
                break;
            }
            if (result != VK_SUCCESS)
            {
                break;
            }
            last_completed_value = submission.completion_value;
            if (submission.owns_fence)
            {
                vkDestroyFence(vk_device, submission.fence, nullptr);
            }
            pending_submissions.pop_front();
        }
        if (last_completed_value != previous_completed_value)
        {
            upload_manager.release_completed(last_completed_value);
        }
    }

    void VulkanQueue::release_pending_submissions_locked()
    {
        for (const PendingSubmission& submission : pending_submissions)
        {
            if (submission.owns_fence && submission.fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(vk_device, submission.fence, nullptr);
            }
        }
        pending_submissions.clear();
    }
} // namespace toy3d
