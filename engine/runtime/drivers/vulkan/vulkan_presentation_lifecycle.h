#pragma once

#include "drivers/rhi/rhi_result.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace toy3d
{
    constexpr const char* vulkan_surface_maintenance1_extension_name =
        "VK_EXT_surface_maintenance1";
    constexpr const char* vulkan_swapchain_maintenance1_extension_name =
        "VK_EXT_swapchain_maintenance1";

    // The repository Vulkan 1.2.198 headers predate maintenance1. These
    // backend-private ABI structs match the published extension layout without
    // raising Toy3d's VulkanPortable v1 baseline or modifying third-party code.
    struct VulkanSwapchainMaintenance1Features
    {
        VkStructureType s_type = static_cast<VkStructureType>(1000275000);
        void* next = nullptr;
        VkBool32 swapchain_maintenance1 = VK_FALSE;
    };

    struct VulkanSwapchainPresentFenceInfo
    {
        VkStructureType s_type = static_cast<VkStructureType>(1000275001);
        const void* next = nullptr;
        std::uint32_t swapchain_count = 0;
        const VkFence* fences = nullptr;
    };

    enum class VulkanImagePresentationPhase
    {
        Reusable,
        AwaitingWSI,
        DiscardAfterGraphics
    };

    struct VulkanPresentTransition
    {
        RHIStatus status;
        VulkanImagePresentationPhase phase = VulkanImagePresentationPhase::Reusable;
        bool recreate_required = false;
        bool terminal = false;
    };

    // Keeps the Vulkan result table independent from native objects so tests
    // can exhaust every WSI outcome without a window or driver.
    VulkanPresentTransition evaluate_vulkan_present_result(VkResult result);
    std::uint32_t vulkan_frame_slot_count(std::uint32_t actual_image_count);
    bool vulkan_swapchain_maintenance1_gate(
        bool instance_dependencies_enabled,
        bool swapchain_extension_enabled,
        bool maintenance1_extension_available,
        bool maintenance1_feature_supported);

    enum class VulkanGenerationRetirementMode
    {
        DestroyImmediately,
        WaitSharedQueue,
        WaitPresentFences
    };

    VulkanGenerationRetirementMode choose_vulkan_generation_retirement(
        bool maintenance1_enabled,
        bool requires_queue_drain,
        std::size_t pending_present_fence_count);

    struct VulkanGenerationPublicationObservation
    {
        std::uint64_t publication_id = 0;
        std::size_t active_generation_count = 0;
        std::size_t frame_slot_count = 0;
        std::size_t image_state_count = 0;
        std::uint64_t rejected_construction_count = 0;
    };

    // Records the transactional publication boundary independently from
    // native handles. Failed or deferred construction never calls publish,
    // so diagnostics continue to describe the last complete generation.
    class VulkanGenerationPublicationTracker final
    {
    public:
        void publish(std::uint32_t image_count);
        void reject_construction();
        void shutdown();
        VulkanGenerationPublicationObservation observation() const;

    private:
        VulkanGenerationPublicationObservation current;
    };

    struct VulkanImageLifecycleState
    {
        VulkanImagePresentationPhase phase = VulkanImagePresentationPhase::Reusable;
        bool present_fence_pending = false;
    };

    // Authoritative per-generation WSI state machine used by the viewport and
    // directed tests. Native handles remain in VulkanViewportContext.
    class VulkanGenerationLifecycle final
    {
    public:
        VulkanGenerationLifecycle(
            std::uint32_t image_count,
            bool maintenance1_enabled);

        RHIStatus record_acquire(std::uint32_t image_index);
        RHIStatus record_submit_failure(
            std::uint32_t image_index,
            const RHIStatus& failure);
        void record_submit_success(std::uint32_t image_index);
        VulkanPresentTransition record_present_result(
            std::uint32_t image_index,
            VkResult result,
            bool has_present_fence);
        void require_queue_drain();

        const VulkanImageLifecycleState& image_state(std::uint32_t image_index) const;
        std::size_t image_count() const;
        std::size_t pending_present_fence_count() const;
        std::uint64_t discarded_semaphore_count() const;
        VulkanGenerationRetirementMode retirement_mode() const;

    private:
        std::vector<VulkanImageLifecycleState> image_states;
        bool maintenance1 = false;
        bool queue_drain_required = false;
        std::uint64_t discarded_semaphores = 0;
    };

    class VulkanPresentationNativeApi
    {
    public:
        virtual ~VulkanPresentationNativeApi() = default;

        virtual VkResult acquire_next_image(
            VkDevice device,
            VkSwapchainKHR swapchain,
            std::uint64_t timeout,
            VkSemaphore semaphore,
            VkFence fence,
            std::uint32_t* image_index) = 0;
        virtual VkResult queue_submit(
            VkQueue queue,
            std::uint32_t submit_count,
            const VkSubmitInfo* submits,
            VkFence fence) = 0;
        virtual VkResult queue_present(VkQueue queue, const VkPresentInfoKHR* present_info) = 0;
        virtual VkResult queue_wait_idle(VkQueue queue) = 0;

        virtual VkResult create_swapchain(
            VkDevice device,
            const VkSwapchainCreateInfoKHR* create_info,
            VkSwapchainKHR* swapchain) = 0;
        virtual VkResult create_image_view(
            VkDevice device,
            const VkImageViewCreateInfo* create_info,
            VkImageView* image_view) = 0;
        virtual VkResult create_semaphore(
            VkDevice device,
            const VkSemaphoreCreateInfo* create_info,
            VkSemaphore* semaphore) = 0;
        virtual VkResult create_fence(
            VkDevice device,
            const VkFenceCreateInfo* create_info,
            VkFence* fence) = 0;
        virtual VkResult create_command_pool(
            VkDevice device,
            const VkCommandPoolCreateInfo* create_info,
            VkCommandPool* command_pool) = 0;
    };

    class VulkanPresentationNativeApiDefault final : public VulkanPresentationNativeApi
    {
    public:
        VkResult acquire_next_image(
            VkDevice device,
            VkSwapchainKHR swapchain,
            std::uint64_t timeout,
            VkSemaphore semaphore,
            VkFence fence,
            std::uint32_t* image_index) override;
        VkResult queue_submit(
            VkQueue queue,
            std::uint32_t submit_count,
            const VkSubmitInfo* submits,
            VkFence fence) override;
        VkResult queue_present(VkQueue queue, const VkPresentInfoKHR* present_info) override;
        VkResult queue_wait_idle(VkQueue queue) override;
        VkResult create_swapchain(
            VkDevice device,
            const VkSwapchainCreateInfoKHR* create_info,
            VkSwapchainKHR* swapchain) override;
        VkResult create_image_view(
            VkDevice device,
            const VkImageViewCreateInfo* create_info,
            VkImageView* image_view) override;
        VkResult create_semaphore(
            VkDevice device,
            const VkSemaphoreCreateInfo* create_info,
            VkSemaphore* semaphore) override;
        VkResult create_fence(
            VkDevice device,
            const VkFenceCreateInfo* create_info,
            VkFence* fence) override;
        VkResult create_command_pool(
            VkDevice device,
            const VkCommandPoolCreateInfo* create_info,
            VkCommandPool* command_pool) override;
    };
}
