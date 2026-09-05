#include "drivers/rhi/rhi_viewport_context.h"
#include "drivers/vulkan/vulkan_swapchain.h"
#include "drivers/vulkan/vulkan_viewport_context.h"

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
}

int main()
{
    using namespace toy3d;

    require(map_vulkan_acquire_result(VK_SUCCESS).succeeded(),
        "Successful acquire must remain successful.");
    require(map_vulkan_acquire_result(VK_SUBOPTIMAL_KHR).code() ==
            RHIErrorCode::Suboptimal,
        "Suboptimal acquire must preserve its recoverable status.");
    require(map_vulkan_acquire_result(VK_ERROR_OUT_OF_DATE_KHR).code() ==
            RHIErrorCode::OutOfDate,
        "Out-of-date acquire must schedule a clean-boundary recreate.");
    require(map_vulkan_acquire_result(VK_ERROR_DEVICE_LOST).code() ==
            RHIErrorCode::DeviceLost,
        "Acquire device loss must preserve its terminal category.");

    require(map_vulkan_present_result(VK_SUCCESS).succeeded(),
        "Successful present must remain successful.");
    require(map_vulkan_present_result(VK_SUBOPTIMAL_KHR).code() ==
            RHIErrorCode::Suboptimal,
        "Suboptimal present must preserve the submitted frame fact.");
    require(map_vulkan_present_result(VK_ERROR_OUT_OF_DATE_KHR).code() ==
            RHIErrorCode::OutOfDate,
        "Out-of-date present must preserve the submitted frame fact.");
    require(map_vulkan_present_result(VK_ERROR_OUT_OF_DEVICE_MEMORY).code() ==
            RHIErrorCode::OutOfMemory,
        "Present allocation failure must retain its diagnostic category.");
    require(map_vulkan_present_result(VK_ERROR_INITIALIZATION_FAILED).code() ==
            RHIErrorCode::BackendFailure,
        "Unknown present failures must map to backend failure.");

    require(vulkan_frame_slot_count(1) == 1 &&
            vulkan_frame_slot_count(2) == 2 &&
            vulkan_frame_slot_count(3) == 2,
        "Frame slots must be min(2, actual image count).");

    VulkanFrameSlot first_slot;
    VulkanFrameSlot second_slot;
    first_slot.completion_value = 11;
    second_slot.completion_value = 12;
    require(first_slot.completion_value != second_slot.completion_value,
        "Frame slots must retain independent CPU/GPU completion identity.");

    VulkanSwapchainImage first_image;
    VulkanSwapchainImage second_image;
    first_image.rendering_done = reinterpret_cast<VkSemaphore>(1);
    second_image.rendering_done = reinterpret_cast<VkSemaphore>(2);
    first_image.last_submission_fence = reinterpret_cast<VkFence>(3);
    second_image.last_submission_fence = reinterpret_cast<VkFence>(4);
    require(first_image.rendering_done != second_image.rendering_done &&
            first_image.last_submission_fence != second_image.last_submission_fence,
        "Each swapchain image must keep independent presentation identity.");

    const RHIFrameEndResult submitted_out_of_date{
        7,
        map_vulkan_present_result(VK_ERROR_OUT_OF_DATE_KHR)};
    require(submitted_out_of_date.completion_value == 7 &&
            submitted_out_of_date.presentation_status.code() == RHIErrorCode::OutOfDate,
        "Present status must not erase a successful business completion.");

    const RHIStatus aborted_out_of_date =
        rhi_normalize_incomplete_acquired_frame_status(
            map_vulkan_present_result(VK_ERROR_OUT_OF_DATE_KHR),
            "Directed Vulkan abort");
    require(aborted_out_of_date.code() == RHIErrorCode::BackendFailure,
        "An abort that cannot close presentation must become terminal.");
    return EXIT_SUCCESS;
}
