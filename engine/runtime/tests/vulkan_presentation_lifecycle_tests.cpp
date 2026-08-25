#include "drivers/vulkan/vulkan_presentation_lifecycle.h"
#include "drivers/rhi/rhi_viewport_context.h"
#include <cstdlib>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    class MockPresentationNativeApi final : public toy3d::VulkanPresentationNativeApi
    {
    public:
        VkResult acquire_result = VK_SUCCESS;
        VkResult submit_result = VK_SUCCESS;
        VkResult present_result = VK_SUCCESS;
        VkResult wait_result = VK_SUCCESS;
        VkResult create_result = VK_SUCCESS;
        std::uint32_t acquire_count = 0;
        std::uint32_t submit_count = 0;
        std::uint32_t present_count = 0;
        std::uint32_t wait_count = 0;
        std::uint32_t create_count = 0;

        VkResult acquire_next_image(
            VkDevice,
            VkSwapchainKHR,
            std::uint64_t,
            VkSemaphore,
            VkFence,
            std::uint32_t* image_index) override
        {
            ++acquire_count;
            *image_index = 1;
            return acquire_result;
        }

        VkResult queue_submit(VkQueue, std::uint32_t, const VkSubmitInfo*, VkFence) override
        {
            ++submit_count;
            return submit_result;
        }

        VkResult queue_present(VkQueue, const VkPresentInfoKHR*) override
        {
            ++present_count;
            return present_result;
        }

        VkResult queue_wait_idle(VkQueue) override
        {
            ++wait_count;
            return wait_result;
        }

        VkResult create_swapchain(VkDevice, const VkSwapchainCreateInfoKHR*, VkSwapchainKHR*) override
        {
            ++create_count;
            return create_result;
        }

        VkResult create_image_view(VkDevice, const VkImageViewCreateInfo*, VkImageView*) override
        {
            ++create_count;
            return create_result;
        }

        VkResult create_semaphore(VkDevice, const VkSemaphoreCreateInfo*, VkSemaphore*) override
        {
            ++create_count;
            return create_result;
        }

        VkResult create_fence(VkDevice, const VkFenceCreateInfo*, VkFence*) override
        {
            ++create_count;
            return create_result;
        }

        VkResult create_command_pool(VkDevice, const VkCommandPoolCreateInfo*, VkCommandPool*) override
        {
            ++create_count;
            return create_result;
        }
    };

}

int main()
{
    using namespace toy3d;

    const VulkanPresentTransition success = evaluate_vulkan_present_result(VK_SUCCESS);
    require(success.status.succeeded(), "Successful present must remain successful.");
    require(success.phase == VulkanImagePresentationPhase::AwaitingWSI,
        "Successful present must await WSI completion proof.");

    const VulkanPresentTransition suboptimal = evaluate_vulkan_present_result(VK_SUBOPTIMAL_KHR);
    require(suboptimal.status.code() == RHIErrorCode::Suboptimal && suboptimal.recreate_required,
        "Suboptimal present must complete while scheduling recreation.");
    require(!suboptimal.terminal && suboptimal.phase == VulkanImagePresentationPhase::AwaitingWSI,
        "Suboptimal present must retain its WSI-owned semaphore.");

    const VulkanPresentTransition out_of_date = evaluate_vulkan_present_result(VK_ERROR_OUT_OF_DATE_KHR);
    require(out_of_date.status.code() == RHIErrorCode::OutOfDate && out_of_date.recreate_required,
        "Out-of-date present must schedule recreation.");
    require(!out_of_date.terminal &&
            out_of_date.phase == VulkanImagePresentationPhase::DiscardAfterGraphics,
        "Out-of-date present semaphore must be destroyed after graphics completion.");

    const VulkanPresentTransition device_lost = evaluate_vulkan_present_result(VK_ERROR_DEVICE_LOST);
    require(device_lost.status.code() == RHIErrorCode::DeviceLost && device_lost.terminal,
        "Device-lost present must latch terminal failure.");
    require(device_lost.phase == VulkanImagePresentationPhase::DiscardAfterGraphics,
        "Terminal present failure must never make the semaphore reusable.");

    const VulkanPresentTransition out_of_memory =
        evaluate_vulkan_present_result(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    require(out_of_memory.status.code() == RHIErrorCode::OutOfMemory &&
            out_of_memory.terminal,
        "Present allocation failure must preserve its terminal out-of-memory category.");
    const VulkanPresentTransition unknown_failure =
        evaluate_vulkan_present_result(VK_ERROR_INITIALIZATION_FAILED);
    require(unknown_failure.status.code() == RHIErrorCode::BackendFailure &&
            unknown_failure.terminal,
        "Unknown present failure must map to terminal backend failure.");

    require(vulkan_frame_slot_count(1) == 1 &&
            vulkan_frame_slot_count(2) == 2 &&
            vulkan_frame_slot_count(4) == 2,
        "Frame slots must be min(2, actual image count).");
    require(vulkan_swapchain_maintenance1_gate(true, true, true, true),
        "Maintenance1 must be enabled when every dependency and feature is available.");
    require(!vulkan_swapchain_maintenance1_gate(false, true, true, true) &&
            !vulkan_swapchain_maintenance1_gate(true, false, true, true) &&
            !vulkan_swapchain_maintenance1_gate(true, true, false, true) &&
            !vulkan_swapchain_maintenance1_gate(true, true, true, false),
        "Maintenance1 must fall back when any extension dependency or feature is absent.");
    require(choose_vulkan_generation_retirement(false, false, 2) ==
            VulkanGenerationRetirementMode::WaitSharedQueue,
        "VulkanPortable v1 recreation must drain only the shared queue.");
    require(choose_vulkan_generation_retirement(true, false, 2) ==
            VulkanGenerationRetirementMode::WaitPresentFences,
        "Maintenance1 must retire a presented generation through present fences.");
    require(choose_vulkan_generation_retirement(true, false, 0) ==
            VulkanGenerationRetirementMode::DestroyImmediately,
        "A generation with no pending presentation may retire without a stall.");
    require(choose_vulkan_generation_retirement(true, true, 1) ==
            VulkanGenerationRetirementMode::WaitSharedQueue,
        "An uncertain out-of-date present must override maintenance1 and drain the queue.");

    MockPresentationNativeApi native_api;
    std::uint32_t image_index = 0;
    native_api.acquire_result = VK_SUBOPTIMAL_KHR;
    require(native_api.acquire_next_image(
            VK_NULL_HANDLE, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, VK_NULL_HANDLE,
            &image_index) == VK_SUBOPTIMAL_KHR && image_index == 1,
        "The acquire seam must preserve injected WSI results and image identity.");
    native_api.submit_result = VK_ERROR_DEVICE_LOST;
    require(native_api.queue_submit(VK_NULL_HANDLE, 0, nullptr, VK_NULL_HANDLE) ==
            VK_ERROR_DEVICE_LOST,
        "The submit seam must preserve injected failures.");
    native_api.present_result = VK_ERROR_OUT_OF_DATE_KHR;
    require(native_api.queue_present(VK_NULL_HANDLE, nullptr) == VK_ERROR_OUT_OF_DATE_KHR,
        "The present seam must preserve injected out-of-date results.");
    require(native_api.queue_wait_idle(VK_NULL_HANDLE) == VK_SUCCESS,
        "The queue-wait seam must expose the fallback retirement boundary.");
    require(native_api.create_semaphore(VK_NULL_HANDLE, nullptr, nullptr) == VK_SUCCESS,
        "The native-object seam must expose directed creation failure injection.");
    require(native_api.acquire_count == 1 && native_api.submit_count == 1 &&
            native_api.present_count == 1 && native_api.wait_count == 1 &&
            native_api.create_count == 1,
        "Every directed native seam operation must be observable.");

    VulkanGenerationLifecycle interleaved_lifecycle(3, false);
    require(interleaved_lifecycle.image_count() == 3 &&
            vulkan_frame_slot_count(
                static_cast<std::uint32_t>(interleaved_lifecycle.image_count())) == 2,
        "A three-image generation must retain three image states and only two frame slots.");
    require(interleaved_lifecycle.record_acquire(0).succeeded(),
        "The first image must be acquirable.");
    interleaved_lifecycle.record_submit_success(0);
    require(interleaved_lifecycle.image_state(0).phase ==
            VulkanImagePresentationPhase::AwaitingWSI,
        "Submit completion alone must not make image zero presentation state reusable.");
    require(interleaved_lifecycle.record_present_result(0, VK_SUCCESS, false).status.succeeded(),
        "Image zero successful present must enter WSI ownership.");
    require(interleaved_lifecycle.record_acquire(1).succeeded(),
        "An interleaved second image must be acquirable independently.");
    interleaved_lifecycle.record_submit_success(1);
    require(interleaved_lifecycle.record_present_result(1, VK_SUCCESS, false).status.succeeded(),
        "Image one successful present must enter WSI ownership.");
    require(interleaved_lifecycle.record_acquire(0).succeeded() &&
            interleaved_lifecycle.image_state(0).phase ==
                VulkanImagePresentationPhase::Reusable &&
            interleaved_lifecycle.image_state(1).phase ==
                VulkanImagePresentationPhase::AwaitingWSI,
        "Reacquiring image zero must release only image zero presentation state.");
    require(interleaved_lifecycle.retirement_mode() ==
            VulkanGenerationRetirementMode::WaitSharedQueue,
        "The Vulkan 1.1 generation must retire through its shared queue.");

    VulkanGenerationLifecycle suboptimal_lifecycle(2, true);
    require(suboptimal_lifecycle.record_acquire(0).succeeded(),
        "Suboptimal sequence must acquire its image.");
    suboptimal_lifecycle.record_submit_success(0);
    const VulkanPresentTransition lifecycle_suboptimal =
        suboptimal_lifecycle.record_present_result(0, VK_SUBOPTIMAL_KHR, true);
    require(lifecycle_suboptimal.recreate_required && !lifecycle_suboptimal.terminal &&
            suboptimal_lifecycle.pending_present_fence_count() == 1 &&
            suboptimal_lifecycle.retirement_mode() ==
                VulkanGenerationRetirementMode::WaitPresentFences,
        "Suboptimal maintenance1 present must retire asynchronously through its present fence.");

    VulkanGenerationLifecycle out_of_date_lifecycle(2, true);
    require(out_of_date_lifecycle.record_acquire(1).succeeded(),
        "Out-of-date sequence must acquire its image.");
    out_of_date_lifecycle.record_submit_success(1);
    const VulkanPresentTransition lifecycle_out_of_date =
        out_of_date_lifecycle.record_present_result(
            1, VK_ERROR_OUT_OF_DATE_KHR, true);
    require(lifecycle_out_of_date.recreate_required && !lifecycle_out_of_date.terminal &&
            out_of_date_lifecycle.image_state(1).phase ==
                VulkanImagePresentationPhase::DiscardAfterGraphics &&
            out_of_date_lifecycle.discarded_semaphore_count() == 1 &&
            out_of_date_lifecycle.retirement_mode() ==
                VulkanGenerationRetirementMode::WaitSharedQueue,
        "Out-of-date present must discard uncertain synchronization after a queue drain.");

    VulkanGenerationLifecycle consecutive_out_of_date_lifecycle(2, true);
    for (std::uint32_t image = 0; image < 2; ++image)
    {
        require(consecutive_out_of_date_lifecycle.record_acquire(image).succeeded(),
            "Each directed out-of-date image must first be acquired.");
        consecutive_out_of_date_lifecycle.record_submit_success(image);
        consecutive_out_of_date_lifecycle.record_present_result(
            image, VK_ERROR_OUT_OF_DATE_KHR, true);
    }
    require(consecutive_out_of_date_lifecycle.discarded_semaphore_count() == 2 &&
            consecutive_out_of_date_lifecycle.retirement_mode() ==
                VulkanGenerationRetirementMode::WaitSharedQueue,
        "Consecutive out-of-date results must remain counted and require one generation drain.");

    VulkanGenerationLifecycle missing_present_fence_lifecycle(2, true);
    require(missing_present_fence_lifecycle.record_acquire(0).succeeded(),
        "Present-fence fallback sequence must acquire its image.");
    missing_present_fence_lifecycle.record_submit_success(0);
    missing_present_fence_lifecycle.record_present_result(0, VK_SUCCESS, false);
    require(missing_present_fence_lifecycle.retirement_mode() ==
            VulkanGenerationRetirementMode::WaitSharedQueue,
        "A missing optional present fence must fall back to shared-queue retirement.");

    VulkanGenerationLifecycle enabled_public_result_lifecycle(1, true);
    VulkanGenerationLifecycle disabled_public_result_lifecycle(1, false);
    require(enabled_public_result_lifecycle.record_acquire(0).succeeded() &&
            disabled_public_result_lifecycle.record_acquire(0).succeeded(),
        "Both capability paths must acquire through the same public frame boundary.");
    enabled_public_result_lifecycle.record_submit_success(0);
    disabled_public_result_lifecycle.record_submit_success(0);
    const RHIFrameEndResult enabled_public_result{
        7,
        enabled_public_result_lifecycle.record_present_result(
            0, VK_SUBOPTIMAL_KHR, true).status};
    const RHIFrameEndResult disabled_public_result{
        7,
        disabled_public_result_lifecycle.record_present_result(
            0, VK_SUBOPTIMAL_KHR, false).status};
    require(enabled_public_result.completion_value ==
                disabled_public_result.completion_value &&
            enabled_public_result.presentation_status.code() ==
                disabled_public_result.presentation_status.code() &&
            enabled_public_result.presentation_status.code() ==
                RHIErrorCode::Suboptimal,
        "Maintenance1 must not change public frame completion or presentation status semantics.");

    VulkanGenerationLifecycle reset_failure_lifecycle(2, true);
    reset_failure_lifecycle.require_queue_drain();
    require(reset_failure_lifecycle.retirement_mode() ==
            VulkanGenerationRetirementMode::WaitSharedQueue,
        "A present-fence reset failure must force shared-queue retirement.");

    VulkanGenerationLifecycle submit_failure_lifecycle(2, false);
    require(submit_failure_lifecycle.record_acquire(0).succeeded(),
        "Submit-failure sequence must acquire its image.");
    const RHIStatus submit_failure = submit_failure_lifecycle.record_submit_failure(
        0,
        RHIStatus::failure(RHIErrorCode::BackendFailure, "Injected submit failure"));
    require(submit_failure.code() == RHIErrorCode::BackendFailure &&
            submit_failure_lifecycle.image_state(0).phase ==
                VulkanImagePresentationPhase::DiscardAfterGraphics &&
            submit_failure_lifecycle.discarded_semaphore_count() == 1,
        "Submit failure must make the acquired image terminal and non-reusable.");
    const RHIStatus device_lost_failure = submit_failure_lifecycle.record_submit_failure(
        1,
        RHIStatus::failure(RHIErrorCode::DeviceLost, "Injected device loss"));
    require(device_lost_failure.code() == RHIErrorCode::DeviceLost,
        "Submit device loss must preserve its public terminal error.");

    VulkanGenerationLifecycle abort_lifecycle(2, false);
    require(abort_lifecycle.record_acquire(0).succeeded(),
        "Abort sequence must acquire its image.");
    abort_lifecycle.record_submit_success(0);
    const VulkanPresentTransition abort_present = abort_lifecycle.record_present_result(
        0, VK_SUCCESS, false);
    require(abort_present.status.succeeded() &&
            abort_lifecycle.image_state(0).phase ==
                VulkanImagePresentationPhase::AwaitingWSI,
        "Abort must use the normal minimal submit/present lifecycle.");
    VulkanGenerationLifecycle failed_abort_lifecycle(2, false);
    require(failed_abort_lifecycle.record_acquire(0).succeeded(),
        "Failed-abort sequence must acquire its image.");
    failed_abort_lifecycle.record_submit_success(0);
    const VulkanPresentTransition failed_abort_present =
        failed_abort_lifecycle.record_present_result(
            0, VK_ERROR_OUT_OF_DATE_KHR, false);
    const RHIStatus normalized_abort_failure =
        rhi_normalize_incomplete_acquired_frame_status(
            failed_abort_present.status,
            "Directed Vulkan abort");
    require(normalized_abort_failure.code() == RHIErrorCode::BackendFailure,
        "Abort that cannot complete presentation must become terminal after acquire.");

    native_api.create_result = VK_ERROR_OUT_OF_HOST_MEMORY;
    require(native_api.create_image_view(VK_NULL_HANDLE, nullptr, nullptr) ==
            VK_ERROR_OUT_OF_HOST_MEMORY && native_api.create_count == 2,
        "A generation object-creation failure must remain injectable before publication.");

    VulkanGenerationPublicationTracker publication_tracker;
    publication_tracker.publish(3);
    const VulkanGenerationPublicationObservation initial_publication =
        publication_tracker.observation();
    require(initial_publication.publication_id == 1 &&
            initial_publication.active_generation_count == 1 &&
            initial_publication.frame_slot_count == 2 &&
            initial_publication.image_state_count == 3,
        "Initial publication observation must expose one complete three-image generation.");
    publication_tracker.reject_construction();
    const VulkanGenerationPublicationObservation failed_creation_observation =
        publication_tracker.observation();
    require(failed_creation_observation.publication_id ==
                initial_publication.publication_id &&
            failed_creation_observation.active_generation_count == 1 &&
            failed_creation_observation.rejected_construction_count == 1,
        "Injected generation creation failure must leave the active publication unchanged.");
    publication_tracker.reject_construction();
    const VulkanGenerationPublicationObservation minimized_observation =
        publication_tracker.observation();
    require(minimized_observation.publication_id ==
                initial_publication.publication_id &&
            minimized_observation.rejected_construction_count == 2,
        "A deferred zero-extent minimize must not publish a generation.");
    publication_tracker.publish(3);
    require(publication_tracker.observation().publication_id == 2,
        "Restore or resize must publish exactly one complete replacement generation.");
    publication_tracker.publish(3);
    publication_tracker.publish(3);
    require(publication_tracker.observation().publication_id == 4,
        "Consecutive out-of-date rebuilds must remain observable as bounded commits.");
    publication_tracker.shutdown();
    const VulkanGenerationPublicationObservation shutdown_observation =
        publication_tracker.observation();
    require(shutdown_observation.publication_id == 4 &&
            shutdown_observation.active_generation_count == 0 &&
            shutdown_observation.frame_slot_count == 0 &&
            shutdown_observation.image_state_count == 0,
        "Shutdown observation must retain history while clearing active generation state.");

    return EXIT_SUCCESS;
}
