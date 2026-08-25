#include "drivers/vulkan/vulkan_presentation_lifecycle.h"

#include <algorithm>
#include <string>

namespace toy3d
{
    VulkanPresentTransition evaluate_vulkan_present_result(VkResult result)
    {
        VulkanPresentTransition transition;
        transition.phase = VulkanImagePresentationPhase::AwaitingWSI;
        if (result == VK_SUCCESS)
        {
            transition.status = RHIStatus::success();
            return transition;
        }
        if (result == VK_SUBOPTIMAL_KHR)
        {
            transition.status = RHIStatus::failure(
                RHIErrorCode::Suboptimal,
                "vkQueuePresentKHR completed with a suboptimal swapchain; recreation is pending.");
            transition.recreate_required = true;
            return transition;
        }

        transition.phase = VulkanImagePresentationPhase::DiscardAfterGraphics;
        transition.recreate_required = result == VK_ERROR_OUT_OF_DATE_KHR;
        transition.terminal = !transition.recreate_required;
        RHIErrorCode code = RHIErrorCode::BackendFailure;
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            code = RHIErrorCode::OutOfDate;
        }
        else if (result == VK_ERROR_DEVICE_LOST)
        {
            code = RHIErrorCode::DeviceLost;
        }
        else if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
        {
            code = RHIErrorCode::OutOfMemory;
        }
        transition.status = RHIStatus::failure(
            code,
            "vkQueuePresentKHR failed with VkResult " +
                std::to_string(static_cast<int>(result)) + ".");
        return transition;
    }

    std::uint32_t vulkan_frame_slot_count(std::uint32_t actual_image_count)
    {
        constexpr std::uint32_t max_frames_in_flight = 2;
        return std::min(max_frames_in_flight, actual_image_count);
    }

    bool vulkan_swapchain_maintenance1_gate(
        bool instance_dependencies_enabled,
        bool swapchain_extension_enabled,
        bool maintenance1_extension_available,
        bool maintenance1_feature_supported)
    {
        return instance_dependencies_enabled &&
            swapchain_extension_enabled &&
            maintenance1_extension_available &&
            maintenance1_feature_supported;
    }

    VulkanGenerationRetirementMode choose_vulkan_generation_retirement(
        bool maintenance1_enabled,
        bool requires_queue_drain,
        std::size_t pending_present_fence_count)
    {
        if (requires_queue_drain || !maintenance1_enabled)
        {
            return VulkanGenerationRetirementMode::WaitSharedQueue;
        }
        return pending_present_fence_count == 0
            ? VulkanGenerationRetirementMode::DestroyImmediately
            : VulkanGenerationRetirementMode::WaitPresentFences;
    }

    void VulkanGenerationPublicationTracker::publish(std::uint32_t image_count)
    {
        ++current.publication_id;
        current.active_generation_count = 1;
        current.frame_slot_count = vulkan_frame_slot_count(image_count);
        current.image_state_count = image_count;
    }

    void VulkanGenerationPublicationTracker::reject_construction()
    {
        ++current.rejected_construction_count;
    }

    void VulkanGenerationPublicationTracker::shutdown()
    {
        current.active_generation_count = 0;
        current.frame_slot_count = 0;
        current.image_state_count = 0;
    }

    VulkanGenerationPublicationObservation
        VulkanGenerationPublicationTracker::observation() const
    {
        return current;
    }

    VulkanGenerationLifecycle::VulkanGenerationLifecycle(
        std::uint32_t image_count,
        bool maintenance1_enabled)
        : image_states(image_count)
        , maintenance1(maintenance1_enabled)
    {
    }

    RHIStatus VulkanGenerationLifecycle::record_acquire(std::uint32_t image_index)
    {
        if (image_index >= image_states.size())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan acquire returned an image index outside the active generation.");
        }
        VulkanImageLifecycleState& state = image_states[image_index];
        if (state.phase == VulkanImagePresentationPhase::DiscardAfterGraphics)
        {
            return RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Vulkan reacquired an image whose presentation semaphore was marked for discard.");
        }
        state.phase = VulkanImagePresentationPhase::Reusable;
        state.present_fence_pending = false;
        return RHIStatus::success();
    }

    RHIStatus VulkanGenerationLifecycle::record_submit_failure(
        std::uint32_t image_index,
        const RHIStatus& failure)
    {
        queue_drain_required = true;
        if (image_index < image_states.size())
        {
            image_states[image_index].phase =
                VulkanImagePresentationPhase::DiscardAfterGraphics;
            ++discarded_semaphores;
        }
        if (failure.code() == RHIErrorCode::DeviceLost)
        {
            return failure;
        }
        return RHIStatus::failure(
            RHIErrorCode::BackendFailure,
            "Vulkan submission failed after image acquire: " + failure.message());
    }

    void VulkanGenerationLifecycle::record_submit_success(std::uint32_t image_index)
    {
        if (image_index < image_states.size())
        {
            image_states[image_index].phase = VulkanImagePresentationPhase::AwaitingWSI;
        }
    }

    VulkanPresentTransition VulkanGenerationLifecycle::record_present_result(
        std::uint32_t image_index,
        VkResult result,
        bool has_present_fence)
    {
        VulkanPresentTransition transition = evaluate_vulkan_present_result(result);
        if (image_index >= image_states.size())
        {
            transition.status = RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Vulkan present used an image index outside the active generation.");
            transition.phase = VulkanImagePresentationPhase::DiscardAfterGraphics;
            transition.terminal = true;
            queue_drain_required = true;
            return transition;
        }
        VulkanImageLifecycleState& state = image_states[image_index];
        state.phase = transition.phase;
        state.present_fence_pending = maintenance1 &&
            has_present_fence &&
            transition.phase == VulkanImagePresentationPhase::AwaitingWSI;
        if (transition.phase == VulkanImagePresentationPhase::DiscardAfterGraphics)
        {
            queue_drain_required = true;
            ++discarded_semaphores;
        }
        if (maintenance1 && !has_present_fence &&
            transition.phase == VulkanImagePresentationPhase::AwaitingWSI)
        {
            queue_drain_required = true;
        }
        return transition;
    }

    void VulkanGenerationLifecycle::require_queue_drain()
    {
        queue_drain_required = true;
    }

    const VulkanImageLifecycleState& VulkanGenerationLifecycle::image_state(
        std::uint32_t image_index) const
    {
        return image_states.at(image_index);
    }

    std::size_t VulkanGenerationLifecycle::image_count() const
    {
        return image_states.size();
    }

    std::size_t VulkanGenerationLifecycle::pending_present_fence_count() const
    {
        return static_cast<std::size_t>(std::count_if(
            image_states.begin(),
            image_states.end(),
            [](const VulkanImageLifecycleState& state)
            {
                return state.present_fence_pending;
            }));
    }

    std::uint64_t VulkanGenerationLifecycle::discarded_semaphore_count() const
    {
        return discarded_semaphores;
    }

    VulkanGenerationRetirementMode VulkanGenerationLifecycle::retirement_mode() const
    {
        return choose_vulkan_generation_retirement(
            maintenance1,
            queue_drain_required,
            pending_present_fence_count());
    }

    VkResult VulkanPresentationNativeApiDefault::acquire_next_image(
        VkDevice device,
        VkSwapchainKHR swapchain,
        std::uint64_t timeout,
        VkSemaphore semaphore,
        VkFence fence,
        std::uint32_t* image_index)
    {
        return vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, image_index);
    }

    VkResult VulkanPresentationNativeApiDefault::queue_submit(
        VkQueue queue,
        std::uint32_t submit_count,
        const VkSubmitInfo* submits,
        VkFence fence)
    {
        return vkQueueSubmit(queue, submit_count, submits, fence);
    }

    VkResult VulkanPresentationNativeApiDefault::queue_present(
        VkQueue queue,
        const VkPresentInfoKHR* present_info)
    {
        return vkQueuePresentKHR(queue, present_info);
    }

    VkResult VulkanPresentationNativeApiDefault::queue_wait_idle(VkQueue queue)
    {
        return vkQueueWaitIdle(queue);
    }

    VkResult VulkanPresentationNativeApiDefault::create_swapchain(
        VkDevice device,
        const VkSwapchainCreateInfoKHR* create_info,
        VkSwapchainKHR* swapchain)
    {
        return vkCreateSwapchainKHR(device, create_info, nullptr, swapchain);
    }

    VkResult VulkanPresentationNativeApiDefault::create_image_view(
        VkDevice device,
        const VkImageViewCreateInfo* create_info,
        VkImageView* image_view)
    {
        return vkCreateImageView(device, create_info, nullptr, image_view);
    }

    VkResult VulkanPresentationNativeApiDefault::create_semaphore(
        VkDevice device,
        const VkSemaphoreCreateInfo* create_info,
        VkSemaphore* semaphore)
    {
        return vkCreateSemaphore(device, create_info, nullptr, semaphore);
    }

    VkResult VulkanPresentationNativeApiDefault::create_fence(
        VkDevice device,
        const VkFenceCreateInfo* create_info,
        VkFence* fence)
    {
        return vkCreateFence(device, create_info, nullptr, fence);
    }

    VkResult VulkanPresentationNativeApiDefault::create_command_pool(
        VkDevice device,
        const VkCommandPoolCreateInfo* create_info,
        VkCommandPool* command_pool)
    {
        return vkCreateCommandPool(device, create_info, nullptr, command_pool);
    }
}
