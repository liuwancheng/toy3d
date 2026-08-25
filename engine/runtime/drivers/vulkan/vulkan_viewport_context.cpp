#include "drivers/vulkan/vulkan_viewport_context.h"

#include "drivers/vulkan/vulkan_command_context.h"
#include "drivers/vulkan/vulkan_device.h"
#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_queue.h"
#include "drivers/vulkan/vulkan_presentation_lifecycle.h"
#include "drivers/vulkan/vulkan_upload_manager.h"
#include "logging/logger.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

namespace toy3d
{
    namespace
    {
        void get_present_source_sync(
            RHIAccess access,
            VkPipelineStageFlags& pipeline_stage,
            VkAccessFlags& access_mask)
        {
            pipeline_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            access_mask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            switch (access)
            {
            case RHIAccess::RenderTarget:
                pipeline_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                access_mask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;
            case RHIAccess::CopyDestination:
            case RHIAccess::ResolveDestination:
                pipeline_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;
            case RHIAccess::CopySource:
            case RHIAccess::ResolveSource:
                pipeline_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                access_mask = VK_ACCESS_TRANSFER_READ_BIT;
                break;
            case RHIAccess::ShaderResourceGraphics:
                pipeline_stage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
                access_mask = VK_ACCESS_SHADER_READ_BIT;
                break;
            case RHIAccess::Present:
                pipeline_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                access_mask = 0;
                break;
            default:
                break;
            }
        }

        RHIStatus make_vulkan_status(VkResult result, const char* operation)
        {
            if (result == VK_SUCCESS)
            {
                return RHIStatus::success();
            }
            RHIErrorCode code = RHIErrorCode::BackendFailure;
            if (result == VK_ERROR_DEVICE_LOST)
            {
                code = RHIErrorCode::DeviceLost;
            }
            else if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                code = RHIErrorCode::OutOfDate;
            }
            else if (result == VK_SUBOPTIMAL_KHR)
            {
                code = RHIErrorCode::Suboptimal;
            }
            else if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            {
                code = RHIErrorCode::OutOfMemory;
            }
            return RHIStatus::failure(
                code,
                std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result)) + ".");
        }

        VkPresentModeKHR to_vk_present_mode(RHIPresentMode mode)
        {
            switch (mode)
            {
            case RHIPresentMode::Immediate:
                return VK_PRESENT_MODE_IMMEDIATE_KHR;
            case RHIPresentMode::Mailbox:
                return VK_PRESENT_MODE_MAILBOX_KHR;
            case RHIPresentMode::Fifo:
            default:
                return VK_PRESENT_MODE_FIFO_KHR;
            }
        }

        class VulkanFrameContext final : public RHIFrameContext
        {
        public:
            VulkanFrameContext(
                VulkanViewportContext& owner,
                RHITextureRef texture,
                RHITextureViewRef view,
                std::uint32_t width,
                std::uint32_t height)
                : viewport(owner)
                , output_texture(std::move(texture))
                , output_view(std::move(view))
                , frame_width(width)
                , frame_height(height)
            {
            }

            const RHITextureRef& present_texture() const override
            {
                return output_texture;
            }

            const RHITextureViewRef& present_view() const override
            {
                return output_view;
            }

            std::uint32_t width() const override
            {
                return frame_width;
            }

            std::uint32_t height() const override
            {
                return frame_height;
            }

            RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>
                create_graphics_command_context() override
            {
                return viewport.create_graphics_command_context();
            }

            VulkanViewportContext& owner() const
            {
                return viewport;
            }

        private:
            VulkanViewportContext& viewport;
            RHITextureRef output_texture;
            RHITextureViewRef output_view;
            std::uint32_t frame_width = 0;
            std::uint32_t frame_height = 0;
        };
    }

    struct VulkanViewportContext::FrameSlot
    {
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkFence completion_fence = VK_NULL_HANDLE;
        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkCommandBuffer present_command_buffer = VK_NULL_HANDLE;
        RHIQueueCompletionValue completion_value = 0;
        // Keep each submitted command list as the lifetime root for every
        // native payload captured while recording. The typed collections below
        // remain for completion-value bookkeeping and upload-page retirement.
        std::vector<RHICommandListRef> submitted_command_lists;
        std::vector<RHIResourceRef> submitted_resources;
        std::vector<std::shared_ptr<VulkanUploadPage>> submitted_upload_pages;
        std::vector<RHITextureViewRef> submitted_texture_views;
        std::vector<RHIGraphicsPipelineRef> submitted_graphics_pipelines;
        std::vector<RHIBindingSetRef> submitted_binding_sets;
        std::vector<VulkanRenderPassResourcesRef> submitted_render_pass_resources;
    };

    struct VulkanViewportContext::SwapchainImagePresentationState
    {
        VkSemaphore render_finished = VK_NULL_HANDLE;
        VkFence image_fence = VK_NULL_HANDLE;
        VkFence present_fence = VK_NULL_HANDLE;
    };

    struct VulkanViewportContext::SwapchainGeneration
    {
        explicit SwapchainGeneration(VulkanViewportContext& viewport)
            : owner(viewport)
        {
        }

        ~SwapchainGeneration()
        {
            owner.destroy_generation(*this);
        }

        VulkanViewportContext& owner;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        std::vector<VkImage> images;
        std::vector<VkImageView> image_views;
        std::vector<RHITextureRef> present_textures;
        std::vector<RHITextureViewRef> present_views;
        std::vector<FrameSlot> frame_slots;
        std::vector<SwapchainImagePresentationState> image_states;
        std::vector<VkFence> deferred_present_fences;
        std::unique_ptr<VulkanGenerationLifecycle> lifecycle;
        std::uint32_t current_frame_slot = 0;
    };

    VulkanViewportContext::VulkanViewportContext(
        VulkanDevice& device,
        RHISurfaceRef surface,
        RHIViewportContextDesc desc)
        : vulkan_device(device)
        , viewport_surface(std::move(surface))
        , viewport_desc(std::move(desc))
        , publication_tracker(
            std::make_unique<VulkanGenerationPublicationTracker>())
    {
    }

    VulkanViewportContext::~VulkanViewportContext()
    {
        if (vulkan_device.device() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(vulkan_device.device());
            vulkan_device.graphics_queue().completed_value();
        }
        active_generation.reset();
        retired_generations.clear();
        publication_tracker->shutdown();
    }

    RHIResult<std::unique_ptr<RHIFrameContext>> VulkanViewportContext::begin_frame()
    {
        if (!presentation_failure)
        {
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(
                presentation_failure.code(),
                presentation_failure.message());
        }
        if (frame_active)
        {
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(
                RHIErrorCode::InvalidArgument,
                "A Vulkan viewport context already has an active frame.");
        }
        const RHIStatus retirement_status = collect_retired_generations();
        if (!retirement_status)
        {
            latch_presentation_failure(retirement_status);
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(
                retirement_status.code(), retirement_status.message());
        }
        if (resize_pending || !active_generation)
        {
            const RHIStatus status = recreate_swapchain();
            if (!status)
            {
                latch_presentation_failure(status);
                return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
            }
        }

        FrameSlot& slot = active_generation->frame_slots[active_generation->current_frame_slot];
        RHIStatus status = make_vulkan_status(
            vkWaitForFences(vulkan_device.device(), 1, &slot.completion_fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences");
        if (!status)
        {
            latch_presentation_failure(status);
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
        }
        const RHIQueueCompletionValue completed_value = vulkan_device.graphics_queue().completed_value();
        vulkan_device.release_completed_work(completed_value);
        slot.submitted_command_lists.clear();
        slot.submitted_resources.clear();
        slot.submitted_upload_pages.clear();
        slot.submitted_texture_views.clear();
        slot.submitted_graphics_pipelines.clear();
        slot.submitted_binding_sets.clear();
        slot.submitted_render_pass_resources.clear();
        status = make_vulkan_status(vkResetCommandPool(vulkan_device.device(), slot.command_pool, 0), "vkResetCommandPool");
        if (!status)
        {
            latch_presentation_failure(status);
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
        }

        VkResult result = vulkan_device.presentation_native_api().acquire_next_image(
            vulkan_device.device(),
            active_generation->swapchain,
            UINT64_MAX,
            slot.image_available,
            VK_NULL_HANDLE,
            &active_image_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            resize_pending = true;
            status = make_vulkan_status(result, "vkAcquireNextImageKHR");
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            status = make_vulkan_status(result, "vkAcquireNextImageKHR");
            latch_presentation_failure(status);
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
        }

        SwapchainImagePresentationState& image_state =
            active_generation->image_states[active_image_index];
        const VkFence image_fence = image_state.image_fence;
        if (image_fence != VK_NULL_HANDLE && image_fence != slot.completion_fence)
        {
            status = make_vulkan_status(
                vkWaitForFences(vulkan_device.device(), 1, &image_fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences");
            if (!status)
            {
                // Acquire already signaled image_available. A failed wait means
                // the previous image use cannot be proven complete, so neither
                // the image nor this frame slot may be safely reused.
                latch_incomplete_active_frame_failure(status, "vkWaitForFences");
                return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
            }
        }

        const bool present_fence_pending =
            active_generation->lifecycle->image_state(
                active_image_index).present_fence_pending;
        if (image_state.present_fence != VK_NULL_HANDLE && present_fence_pending)
        {
            const VkResult fence_status = vkGetFenceStatus(
                vulkan_device.device(), image_state.present_fence);
            if (fence_status == VK_NOT_READY)
            {
                active_generation->deferred_present_fences.push_back(
                    image_state.present_fence);
                image_state.present_fence = VK_NULL_HANDLE;
                VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                status = make_vulkan_status(
                    vulkan_device.presentation_native_api().create_fence(
                        vulkan_device.device(), &fence_info, &image_state.present_fence),
                    "vkCreateFence");
                if (!status)
                {
                    latch_incomplete_active_frame_failure(
                        status, "Vulkan present-fence replacement");
                    return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(
                        presentation_failure.code(), presentation_failure.message());
                }
                // Bound the retained fence lifetime without blocking the
                // rendering thread or the platform resize message pump.
                active_generation->lifecycle->require_queue_drain();
                resize_pending = true;
            }
            else if (fence_status != VK_SUCCESS)
            {
                status = make_vulkan_status(fence_status, "vkGetFenceStatus");
                latch_incomplete_active_frame_failure(
                    status, "Vulkan present-fence status");
                return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(
                    presentation_failure.code(), presentation_failure.message());
            }
            else
            {
                const RHIStatus reset_status = make_vulkan_status(
                    vkResetFences(
                        vulkan_device.device(), 1, &image_state.present_fence),
                    "vkResetFences");
                if (!reset_status)
                {
                    vkDestroyFence(
                        vulkan_device.device(), image_state.present_fence, nullptr);
                    image_state.present_fence = VK_NULL_HANDLE;
                    active_generation->lifecycle->require_queue_drain();
                    TOY_LOG_WARN(
                        "{}; continuing with shared-queue generation retirement.",
                        reset_status.message());
                }
            }
        }
        // Reacquiring the same image is the Vulkan 1.1 proof that WSI has
        // finished waiting on this image's render_finished semaphore.
        status = active_generation->lifecycle->record_acquire(active_image_index);
        if (!status)
        {
            latch_incomplete_active_frame_failure(status, "Vulkan image reacquire");
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(
                presentation_failure.code(), presentation_failure.message());
        }
        ++active_frame_id;
        frame_active = true;
        resize_pending = result == VK_SUBOPTIMAL_KHR;
        return RHIResult<std::unique_ptr<RHIFrameContext>>::success(
            std::make_unique<VulkanFrameContext>(
                *this,
                active_generation->present_textures[active_image_index],
                active_generation->present_views[active_image_index],
                active_generation->extent.width,
                active_generation->extent.height));
    }

    RHIResult<RHIFrameEndResult> VulkanViewportContext::end_frame(
        std::unique_ptr<RHIFrameContext> frame,
        const std::vector<RHICommandListRef>& command_lists)
    {
        auto* vulkan_frame = dynamic_cast<VulkanFrameContext*>(frame.get());
        const auto fail_before_business_submit = [this](const RHIStatus& failure)
        {
            if (!frame_active)
            {
                return RHIResult<RHIFrameEndResult>::failure(
                    failure.code(), failure.message());
            }
            const RHIStatus recovery_status = abort_active_frame();
            const RHIStatus reported_status =
                recovery_status || rhi_is_recoverable_viewport_status(recovery_status)
                ? failure
                : recovery_status;
            return RHIResult<RHIFrameEndResult>::failure(
                reported_status.code(), reported_status.message());
        };
        if (!frame_active || vulkan_frame == nullptr || &vulkan_frame->owner() != this)
        {
            const RHIStatus invalid_frame_status = RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Frame context does not belong to this Vulkan viewport context.");
            if (vulkan_frame != nullptr && &vulkan_frame->owner() != this)
            {
                VulkanViewportContext& owner = vulkan_frame->owner();
                const RHIStatus owner_recovery_status = owner.abort_frame(std::move(frame));
                const RHIStatus reported_status =
                    owner_recovery_status || rhi_is_recoverable_viewport_status(owner_recovery_status)
                    ? invalid_frame_status
                    : owner_recovery_status;
                return RHIResult<RHIFrameEndResult>::failure(
                    reported_status.code(), reported_status.message());
            }
            return fail_before_business_submit(invalid_frame_status);
        }

        std::vector<VulkanCommandList*> vulkan_command_lists;
        vulkan_command_lists.reserve(command_lists.size());
        std::set<const RHICommandList*> unique_command_lists;
        RHIStatus validation_status = command_lists.size() == 1
            ? RHIStatus::success()
            : RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "The first renderer stage requires exactly one Vulkan viewport business command list.");
        for (const RHICommandListRef& command_list : command_lists)
        {
            if (!validation_status)
            {
                break;
            }
            if (!command_list)
            {
                validation_status = RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan viewport submission received a null command list.");
                break;
            }
            if (!unique_command_lists.emplace(command_list.get()).second)
            {
                validation_status = RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "A command list cannot appear twice in one Vulkan viewport submission.");
                break;
            }
            auto* vulkan_command_list = dynamic_cast<VulkanCommandList*>(command_list.get());
            if (vulkan_command_list == nullptr ||
                !vulkan_command_list->is_owned_by(vulkan_device) ||
                !vulkan_command_list->belongs_to(*this, active_frame_id))
            {
                validation_status = RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan viewport submission received a command list from another backend or viewport.");
                break;
            }
            if (vulkan_command_list->state() != RHICommandListState::Closed)
            {
                validation_status = RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan viewport submission requires closed command lists.");
                break;
            }
            for (const VulkanCommandList* previous : vulkan_command_lists)
            {
                if (vulkan_command_list->has_state_overlap(*previous))
                {
                    validation_status = RHIStatus::failure(
                        RHIErrorCode::Unsupported,
                        "Vulkan cannot yet submit multiple command lists with overlapping state transitions.");
                    break;
                }
            }
            if (!validation_status)
            {
                break;
            }
            vulkan_command_lists.push_back(vulkan_command_list);
        }

        if (!validation_status)
        {
            return fail_before_business_submit(validation_status);
        }

        RHIStatus status = submit_active_frame(vulkan_command_lists);
        if (!status)
        {
            finish_active_frame();
            return RHIResult<RHIFrameEndResult>::failure(status.code(), status.message());
        }

        FrameSlot& slot = active_generation->frame_slots[active_generation->current_frame_slot];
        slot.submitted_command_lists.insert(
            slot.submitted_command_lists.end(), command_lists.begin(), command_lists.end());
        for (const VulkanCommandList* command_list : vulkan_command_lists)
        {
            const std::vector<RHIResourceRef>& resources = command_list->retained_resources();
            slot.submitted_resources.insert(slot.submitted_resources.end(), resources.begin(), resources.end());
            const std::vector<std::shared_ptr<VulkanUploadPage>>& upload_pages =
                command_list->retained_upload_pages();
            slot.submitted_upload_pages.insert(
                slot.submitted_upload_pages.end(), upload_pages.begin(), upload_pages.end());
            const std::vector<RHITextureViewRef>& texture_views = command_list->retained_texture_views();
            slot.submitted_texture_views.insert(
                slot.submitted_texture_views.end(), texture_views.begin(), texture_views.end());
            const std::vector<RHIGraphicsPipelineRef>& graphics_pipelines =
                command_list->retained_graphics_pipelines();
            slot.submitted_graphics_pipelines.insert(
                slot.submitted_graphics_pipelines.end(), graphics_pipelines.begin(), graphics_pipelines.end());
            const std::vector<RHIBindingSetRef>& binding_sets = command_list->retained_binding_sets();
            slot.submitted_binding_sets.insert(
                slot.submitted_binding_sets.end(), binding_sets.begin(), binding_sets.end());
            const std::vector<VulkanRenderPassResourcesRef>& render_pass_resources =
                command_list->retained_render_pass_resources();
            slot.submitted_render_pass_resources.insert(
                slot.submitted_render_pass_resources.end(),
                render_pass_resources.begin(), render_pass_resources.end());
        }
        RHIStatus post_submit_status = RHIStatus::success();
        for (VulkanCommandList* command_list : vulkan_command_lists)
        {
            post_submit_status = command_list->mark_submitted_by_viewport();
            if (!post_submit_status)
            {
                post_submit_status = latch_presentation_failure(post_submit_status);
                break;
            }
        }
        const RHIStatus present_status = rhi_normalize_submitted_presentation_status(
            present_active_image(), "Vulkan presentation");
        if (!present_status && !rhi_is_recoverable_viewport_status(present_status))
        {
            latch_presentation_failure(present_status);
        }
        const RHIStatus reported_presentation_status =
            post_submit_status ? present_status : post_submit_status;
        const RHIFrameEndResult result{slot.completion_value, reported_presentation_status};
        finish_active_frame();
        return RHIResult<RHIFrameEndResult>::success(result);
    }

    RHIStatus VulkanViewportContext::abort_frame(std::unique_ptr<RHIFrameContext> frame)
    {
        auto* vulkan_frame = dynamic_cast<VulkanFrameContext*>(frame.get());
        if (!frame_active || vulkan_frame == nullptr || &vulkan_frame->owner() != this)
        {
            const RHIStatus invalid_frame_status = RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Frame context does not belong to this active Vulkan viewport frame.");
            if (vulkan_frame != nullptr && &vulkan_frame->owner() != this)
            {
                VulkanViewportContext& owner = vulkan_frame->owner();
                const RHIStatus owner_recovery_status = owner.abort_frame(std::move(frame));
                return owner_recovery_status || rhi_is_recoverable_viewport_status(owner_recovery_status)
                    ? invalid_frame_status
                    : owner_recovery_status;
            }
            if (!frame_active)
            {
                return invalid_frame_status;
            }
            const RHIStatus recovery_status = abort_active_frame();
            return recovery_status || rhi_is_recoverable_viewport_status(recovery_status)
                ? invalid_frame_status
                : recovery_status;
        }
        return abort_active_frame();
    }

    RHIStatus VulkanViewportContext::request_resize(std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "A zero-sized viewport cannot be presented.");
        }
        pending_width = width;
        pending_height = height;
        resize_pending = true;
        return RHIStatus::success();
    }

    RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>
        VulkanViewportContext::create_graphics_command_context()
    {
        if (!frame_active || !active_generation || active_generation->frame_slots.empty())
        {
            return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics command contexts can be created only for an active viewport frame.");
        }
        return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::success(
            std::make_unique<VulkanGraphicsCommandContext>(
                vulkan_device,
                *this,
                active_generation->frame_slots[active_generation->current_frame_slot].command_pool,
                active_frame_id));
    }

    RHIStatus VulkanViewportContext::recreate_swapchain()
    {
        if (pending_width != 0)
        {
            viewport_desc.width = pending_width;
            viewport_desc.height = pending_height;
        }
        const VkSwapchainKHR old_swapchain = active_generation
            ? active_generation->swapchain
            : VK_NULL_HANDLE;
        std::unique_ptr<SwapchainGeneration> new_generation;
        RHIStatus status = create_swapchain(old_swapchain, new_generation);
        if (!status)
        {
            publication_tracker->reject_construction();
            return status;
        }

        std::unique_ptr<SwapchainGeneration> old_generation = std::move(active_generation);
        active_generation = std::move(new_generation);
        publication_tracker->publish(
            static_cast<std::uint32_t>(active_generation->images.size()));
        if (old_generation)
        {
            status = retire_generation(std::move(old_generation));
            if (!status)
            {
                return status;
            }
        }
        pending_width = 0;
        pending_height = 0;
        resize_pending = false;
        return RHIStatus::success();
    }

    RHIStatus VulkanViewportContext::create_swapchain(
        VkSwapchainKHR old_swapchain,
        std::unique_ptr<SwapchainGeneration>& output_generation)
    {
        auto generation = std::make_unique<SwapchainGeneration>(*this);
        VkSwapchainKHR& vk_swapchain = generation->swapchain;
        VkExtent2D& swapchain_extent = generation->extent;
        std::vector<VkImage>& swapchain_images = generation->images;
        std::vector<VkImageView>& swapchain_image_views = generation->image_views;
        std::vector<RHITextureRef>& present_textures = generation->present_textures;
        std::vector<RHITextureViewRef>& present_views = generation->present_views;
        std::vector<FrameSlot>& frame_slots = generation->frame_slots;
        std::vector<SwapchainImagePresentationState>& image_presentation_states =
            generation->image_states;
        VkSurfaceCapabilitiesKHR capabilities{};
        RHIStatus status = make_vulkan_status(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                vulkan_device.physical_device(),
                vulkan_device.primary_surface_handle(),
                &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if (!status)
        {
            return status;
        }
        if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0)
        {
            return RHIStatus::failure(
                RHIErrorCode::NotReady,
                "The Vulkan presentation surface has a zero extent and cannot create a swapchain yet.");
        }

        const VkFormat requested_format = vulkan_format_from_pixel_format(viewport_desc.format);
        if (requested_format == VK_FORMAT_UNDEFINED)
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported, "The requested RHI viewport format has no Vulkan mapping.");
        }

        std::uint32_t format_count = 0;
        status = make_vulkan_status(
            vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_device.physical_device(), vulkan_device.primary_surface_handle(), &format_count, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (!status)
        {
            return status;
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        status = make_vulkan_status(
            vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_device.physical_device(), vulkan_device.primary_surface_handle(), &format_count, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (!status)
        {
            return status;
        }
        const auto format_it = std::find_if(
            formats.begin(), formats.end(), [requested_format](const VkSurfaceFormatKHR& format)
            {
                return format.format == requested_format;
            });
        if (format_it == formats.end())
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported, "The primary surface does not support the requested viewport format.");
        }

        std::uint32_t present_mode_count = 0;
        status = make_vulkan_status(
            vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_device.physical_device(), vulkan_device.primary_surface_handle(), &present_mode_count, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
        if (!status)
        {
            return status;
        }
        std::vector<VkPresentModeKHR> present_modes(present_mode_count);
        status = make_vulkan_status(
            vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_device.physical_device(), vulkan_device.primary_surface_handle(), &present_mode_count, present_modes.data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
        if (!status)
        {
            return status;
        }
        const VkPresentModeKHR requested_present_mode = to_vk_present_mode(viewport_desc.present_mode);
        if (std::find(present_modes.begin(), present_modes.end(), requested_present_mode) == present_modes.end())
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported, "The primary surface does not support the requested present mode.");
        }

        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            // std::clamp applies Vulkan's inclusive surface extent limits
            // directly and keeps width/height handling symmetric.
            extent.width = std::clamp(viewport_desc.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(viewport_desc.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        std::uint32_t image_count = std::max(viewport_desc.image_count, capabilities.minImageCount);
        if (capabilities.maxImageCount != 0)
        {
            image_count = std::min(image_count, capabilities.maxImageCount);
        }

        const VkImageUsageFlags required_usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if ((capabilities.supportedUsageFlags & required_usage) != required_usage)
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "The primary surface does not support the required presentation image usage.");
        }

        VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        const VkCompositeAlphaFlagBitsKHR alpha_candidates[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
        for (VkCompositeAlphaFlagBitsKHR candidate : alpha_candidates)
        {
            if ((capabilities.supportedCompositeAlpha & candidate) != 0)
            {
                composite_alpha = candidate;
                break;
            }
        }

        VkSwapchainCreateInfoKHR create_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        create_info.surface = vulkan_device.primary_surface_handle();
        create_info.minImageCount = image_count;
        create_info.imageFormat = format_it->format;
        create_info.imageColorSpace = format_it->colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = required_usage;
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.preTransform = capabilities.currentTransform;
        create_info.compositeAlpha = composite_alpha;
        create_info.presentMode = requested_present_mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = old_swapchain;
        status = make_vulkan_status(
            vulkan_device.presentation_native_api().create_swapchain(
                vulkan_device.device(), &create_info, &vk_swapchain),
            "vkCreateSwapchainKHR");
        if (!status)
        {
            vk_swapchain = VK_NULL_HANDLE;
            return status;
        }

        swapchain_extent = extent;
        std::uint32_t actual_image_count = 0;
        status = make_vulkan_status(vkGetSwapchainImagesKHR(vulkan_device.device(), vk_swapchain, &actual_image_count, nullptr), "vkGetSwapchainImagesKHR");
        if (!status)
        {
            return status;
        }
        swapchain_images.resize(actual_image_count);
        status = make_vulkan_status(vkGetSwapchainImagesKHR(vulkan_device.device(), vk_swapchain, &actual_image_count, swapchain_images.data()), "vkGetSwapchainImagesKHR");
        if (!status)
        {
            return status;
        }

        swapchain_image_views.resize(actual_image_count, VK_NULL_HANDLE);
        present_textures.reserve(actual_image_count);
        present_views.reserve(actual_image_count);
        for (std::uint32_t index = 0; index < actual_image_count; ++index)
        {
            VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view_info.image = swapchain_images[index];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format_it->format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.layerCount = 1;
            status = make_vulkan_status(
                vulkan_device.presentation_native_api().create_image_view(
                    vulkan_device.device(), &view_info, &swapchain_image_views[index]),
                "vkCreateImageView");
            if (!status)
            {
                return status;
            }

            RHITextureDesc texture_desc;
            texture_desc.width = extent.width;
            texture_desc.height = extent.height;
            texture_desc.format = viewport_desc.format;
            texture_desc.usage =
                RHIResourceUsage::RenderTarget |
                RHIResourceUsage::CopyDestination;
            texture_desc.initial_access = RHIAccess::Present;
            texture_desc.debug_name = viewport_desc.debug_name + ".Image" + std::to_string(index);
            RHITextureRef texture = std::make_shared<VulkanTexture>(
                vulkan_device,
                std::move(texture_desc),
                swapchain_images[index],
                VK_IMAGE_LAYOUT_UNDEFINED,
                RHIAccess::Present);

            RHITextureViewDesc view_desc;
            view_desc.type = RHIResourceViewType::RenderTarget;
            view_desc.dimension = RHITextureViewDimension::Texture2D;
            view_desc.format = viewport_desc.format;
            view_desc.debug_name = viewport_desc.debug_name + ".View" + std::to_string(index);
            present_textures.push_back(texture);
            present_views.push_back(std::make_shared<VulkanTextureView>(
                std::move(texture),
                std::move(view_desc),
                vulkan_device.device(),
                swapchain_image_views[index],
                false));
        }

        frame_slots.resize(vulkan_frame_slot_count(actual_image_count));
        for (FrameSlot& slot : frame_slots)
        {
            VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            status = make_vulkan_status(
                vulkan_device.presentation_native_api().create_semaphore(
                    vulkan_device.device(), &semaphore_info, &slot.image_available),
                "vkCreateSemaphore");
            if (!status)
            {
                return status;
            }
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            status = make_vulkan_status(
                vulkan_device.presentation_native_api().create_fence(
                    vulkan_device.device(), &fence_info, &slot.completion_fence),
                "vkCreateFence");
            if (!status)
            {
                return status;
            }
            VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool_info.queueFamilyIndex = vulkan_device.graphics_queue_family_index();
            status = make_vulkan_status(
                vulkan_device.presentation_native_api().create_command_pool(
                    vulkan_device.device(), &pool_info, &slot.command_pool),
                "vkCreateCommandPool");
            if (!status)
            {
                return status;
            }
            VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            command_info.commandPool = slot.command_pool;
            command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command_info.commandBufferCount = 1;
            status = make_vulkan_status(vkAllocateCommandBuffers(vulkan_device.device(), &command_info, &slot.present_command_buffer), "vkAllocateCommandBuffers");
            if (!status)
            {
                return status;
            }
        }

        image_presentation_states.resize(actual_image_count);
        generation->lifecycle = std::make_unique<VulkanGenerationLifecycle>(
            actual_image_count,
            vulkan_device.swapchain_maintenance1_enabled());
        for (SwapchainImagePresentationState& image_state : image_presentation_states)
        {
            VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            status = make_vulkan_status(
                vulkan_device.presentation_native_api().create_semaphore(
                    vulkan_device.device(), &semaphore_info, &image_state.render_finished),
                "vkCreateSemaphore");
            if (!status)
            {
                return status;
            }
            if (vulkan_device.swapchain_maintenance1_enabled())
            {
                VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                status = make_vulkan_status(
                    vulkan_device.presentation_native_api().create_fence(
                        vulkan_device.device(), &fence_info, &image_state.present_fence),
                    "vkCreateFence");
                if (!status)
                {
                    return status;
                }
            }
        }
        generation->format = format_it->format;
        output_generation = std::move(generation);
        return RHIStatus::success();
    }

    void VulkanViewportContext::destroy_generation(SwapchainGeneration& generation)
    {
        std::vector<RHITextureViewRef>& present_views = generation.present_views;
        std::vector<RHITextureRef>& present_textures = generation.present_textures;
        std::vector<VkImageView>& swapchain_image_views = generation.image_views;
        std::vector<FrameSlot>& frame_slots = generation.frame_slots;
        std::vector<SwapchainImagePresentationState>& image_presentation_states =
            generation.image_states;
        std::vector<VkImage>& swapchain_images = generation.images;
        present_views.clear();
        present_textures.clear();
        const VkDevice device = vulkan_device.device();
        for (VkImageView view : swapchain_image_views)
        {
            if (view != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device, view, nullptr);
            }
        }
        swapchain_image_views.clear();
        for (FrameSlot& slot : frame_slots)
        {
            if (device == VK_NULL_HANDLE)
            {
                continue;
            }
            if (slot.image_available != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, slot.image_available, nullptr);
            }
            if (slot.completion_fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(device, slot.completion_fence, nullptr);
            }
            if (slot.command_pool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(device, slot.command_pool, nullptr);
            }
        }
        frame_slots.clear();
        for (SwapchainImagePresentationState& image_state : image_presentation_states)
        {
            if (device != VK_NULL_HANDLE && image_state.render_finished != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, image_state.render_finished, nullptr);
            }
            if (device != VK_NULL_HANDLE && image_state.present_fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(device, image_state.present_fence, nullptr);
            }
        }
        image_presentation_states.clear();
        for (VkFence fence : generation.deferred_present_fences)
        {
            if (device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(device, fence, nullptr);
            }
        }
        generation.deferred_present_fences.clear();
        swapchain_images.clear();
        if (generation.swapchain != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device, generation.swapchain, nullptr);
        }
        generation.swapchain = VK_NULL_HANDLE;
    }

    RHIStatus VulkanViewportContext::retire_generation(
        std::unique_ptr<SwapchainGeneration> generation)
    {
        if (!generation)
        {
            return RHIStatus::success();
        }
        const VulkanGenerationRetirementMode retirement_mode =
            generation->lifecycle->retirement_mode();
        if (retirement_mode == VulkanGenerationRetirementMode::DestroyImmediately)
        {
            return RHIStatus::success();
        }
        if (retirement_mode == VulkanGenerationRetirementMode::WaitPresentFences)
        {
            retired_generations.push_back(std::move(generation));
            return collect_retired_generations();
        }

        ++fallback_queue_drain_count;
        const RHIStatus status = vulkan_device.graphics_queue().wait_idle();
        if (!status)
        {
            // Keep ownership until terminal cleanup; a failed queue wait does
            // not prove that this generation's WSI objects are safe to destroy.
            retired_generations.push_back(std::move(generation));
            return status;
        }
        vulkan_device.release_completed_work(
            vulkan_device.graphics_queue().completed_value());
        return RHIStatus::success();
    }

    RHIStatus VulkanViewportContext::collect_retired_generations()
    {
        auto generation = retired_generations.begin();
        while (generation != retired_generations.end())
        {
            bool ready = true;
            for (std::uint32_t image_index = 0;
                 image_index < (*generation)->lifecycle->image_count();
                 ++image_index)
            {
                if (!(*generation)->lifecycle->image_state(
                        image_index).present_fence_pending)
                {
                    continue;
                }
                const VkResult result = vkGetFenceStatus(
                    vulkan_device.device(),
                    (*generation)->image_states[image_index].present_fence);
                if (result == VK_NOT_READY)
                {
                    ready = false;
                    break;
                }
                if (result != VK_SUCCESS)
                {
                    return make_vulkan_status(result, "vkGetFenceStatus");
                }
            }
            if (!ready)
            {
                ++generation;
                continue;
            }
            generation = retired_generations.erase(generation);
        }
        return RHIStatus::success();
    }

    VulkanViewportObservation VulkanViewportContext::observation_snapshot() const
    {
        VulkanViewportObservation observation;
        const VulkanGenerationPublicationObservation publication =
            publication_tracker->observation();
        observation.generation_publication_id = publication.publication_id;
        observation.rejected_generation_construction_count =
            publication.rejected_construction_count;
        observation.active_generation_count = publication.active_generation_count;
        observation.retired_generation_count = retired_generations.size();
        observation.fallback_queue_drain_count = fallback_queue_drain_count;
        observation.discarded_semaphore_count = discarded_semaphore_count;
        observation.swapchain_maintenance1_enabled =
            vulkan_device.swapchain_maintenance1_enabled();
        if (active_generation)
        {
            observation.frame_slot_count = publication.frame_slot_count;
            observation.image_state_count = publication.image_state_count;
            observation.pending_present_fence_count =
                active_generation->lifecycle->pending_present_fence_count();
        }
        for (const auto& generation : retired_generations)
        {
            observation.pending_present_fence_count +=
                generation->lifecycle->pending_present_fence_count();
        }
        return observation;
    }

    RHIStatus VulkanViewportContext::submit_active_frame(const std::vector<VulkanCommandList*>& command_lists)
    {
        FrameSlot& slot = active_generation->frame_slots[active_generation->current_frame_slot];
        SwapchainImagePresentationState& image_state =
            active_generation->image_states[active_image_index];
        const auto active_texture = std::dynamic_pointer_cast<VulkanTexture>(
            active_generation->present_textures[active_image_index]);
        if (!active_texture)
        {
            return latch_incomplete_active_frame_failure(RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Vulkan viewport lost its native swapchain texture wrapper."),
                "Vulkan present transition recording");
        }
        VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        RHIStatus status = make_vulkan_status(vkBeginCommandBuffer(slot.present_command_buffer, &begin_info), "vkBeginCommandBuffer");
        if (!status)
        {
            return latch_incomplete_active_frame_failure(status, "vkBeginCommandBuffer");
        }
        VkImageLayout layout_before_present = active_texture->image_layout();
        RHIAccess access_before_present = active_texture->current_access();
        for (const VulkanCommandList* command_list : command_lists)
        {
            command_list->try_get_tracked_texture_state(
                active_texture, layout_before_present, access_before_present);
        }
        if (layout_before_present != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            VkPipelineStageFlags source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags source_access = 0;
            if (layout_before_present != VK_IMAGE_LAYOUT_UNDEFINED)
            {
                get_present_source_sync(access_before_present, source_stage, source_access);
            }
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = layout_before_present;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcAccessMask = source_access;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = active_generation->images[active_image_index];
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(
                slot.present_command_buffer,
                source_stage,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);
        }
        status = make_vulkan_status(vkEndCommandBuffer(slot.present_command_buffer), "vkEndCommandBuffer");
        if (!status)
        {
            return latch_incomplete_active_frame_failure(status, "vkEndCommandBuffer");
        }
        status = make_vulkan_status(vkResetFences(vulkan_device.device(), 1, &slot.completion_fence), "vkResetFences");
        if (!status)
        {
            return latch_incomplete_active_frame_failure(status, "vkResetFences");
        }
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        std::vector<VkCommandBuffer> command_buffers;
        command_buffers.reserve(command_lists.size() + 1U);
        for (const VulkanCommandList* command_list : command_lists)
        {
            command_buffers.push_back(command_list->command_buffer());
        }
        command_buffers.push_back(slot.present_command_buffer);
        auto& queue = static_cast<VulkanQueue&>(vulkan_device.graphics_queue());
        const auto submit_result = queue.submit_viewport(
            command_lists,
            command_buffers,
            *active_texture,
            slot.image_available,
            wait_stage,
            image_state.render_finished,
            slot.completion_fence);
        if (!submit_result)
        {
            // image_available may remain signaled and the acquired image was
            // not returned to the presentation engine. Retrying this viewport
            // would reuse synchronization with an unknown state.
            const RHIStatus lifecycle_failure =
                active_generation->lifecycle->record_submit_failure(
                    active_image_index,
                    submit_result.status());
            latch_incomplete_active_frame_failure(
                lifecycle_failure,
                "Vulkan viewport submission");
            const VkFence discarded_fence = slot.completion_fence;
            for (SwapchainImagePresentationState& presentation_state : active_generation->image_states)
            {
                if (presentation_state.image_fence == discarded_fence)
                {
                    presentation_state.image_fence = VK_NULL_HANDLE;
                }
            }
            vkDestroyFence(vulkan_device.device(), discarded_fence, nullptr);
            slot.completion_fence = VK_NULL_HANDLE;
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            const RHIStatus fence_status = make_vulkan_status(
                vulkan_device.presentation_native_api().create_fence(
                    vulkan_device.device(), &fence_info, &slot.completion_fence),
                "vkCreateFence");
            if (!fence_status)
            {
                latch_incomplete_active_frame_failure(fence_status, "vkCreateFence");
                return fence_status;
            }
            return presentation_failure;
        }
        slot.completion_value = submit_result.value().completion_value;
        if (slot.completion_value != 0)
        {
            image_state.image_fence = slot.completion_fence;
            active_generation->lifecycle->record_submit_success(
                active_image_index);
        }
        return RHIStatus::success();
    }

    RHIStatus VulkanViewportContext::present_active_image()
    {
        SwapchainImagePresentationState& image_state =
            active_generation->image_states[active_image_index];
        VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &image_state.render_finished;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &active_generation->swapchain;
        present_info.pImageIndices = &active_image_index;
        VulkanSwapchainPresentFenceInfo present_fence_info;
        if (image_state.present_fence != VK_NULL_HANDLE)
        {
            present_fence_info.swapchain_count = 1;
            present_fence_info.fences = &image_state.present_fence;
            present_info.pNext = &present_fence_info;
        }
        const VkResult result = vulkan_device.presentation_native_api().queue_present(
            vulkan_device.graphics_queue_handle(), &present_info);
        const VulkanPresentTransition transition =
            active_generation->lifecycle->record_present_result(
                active_image_index,
                result,
                image_state.present_fence != VK_NULL_HANDLE);
        if (transition.phase == VulkanImagePresentationPhase::DiscardAfterGraphics)
        {
            ++discarded_semaphore_count;
        }
        resize_pending = resize_pending || transition.recreate_required;
        if (transition.terminal)
        {
            latch_presentation_failure(transition.status);
        }
        return transition.status;
    }

    RHIStatus VulkanViewportContext::abort_active_frame()
    {
        RHIStatus status = submit_active_frame({});
        if (status)
        {
            const auto active_texture =
                std::dynamic_pointer_cast<VulkanTexture>(
                    active_generation->present_textures[active_image_index]);
            if (!active_texture)
            {
                status = latch_incomplete_active_frame_failure(RHIStatus::failure(
                    RHIErrorCode::BackendFailure,
                    "Vulkan viewport lost its native swapchain texture wrapper after abort submission."),
                    "Vulkan aborted-frame presentation");
            }
            else
            {
                active_texture->set_state(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, RHIAccess::Present);
                status = present_active_image();
                if (!status)
                {
                    status = latch_incomplete_active_frame_failure(
                        status,
                        "Vulkan aborted-frame presentation");
                }
            }
        }
        finish_active_frame();
        return status;
    }

    RHIStatus VulkanViewportContext::latch_presentation_failure(const RHIStatus& status)
    {
        if (!status && !rhi_is_recoverable_viewport_status(status) && presentation_failure)
        {
            presentation_failure = status;
        }
        if (!presentation_failure)
        {
            return presentation_failure;
        }
        return status;
    }

    RHIStatus VulkanViewportContext::latch_incomplete_active_frame_failure(
        const RHIStatus& status,
        const char* operation)
    {
        if (presentation_failure)
        {
            presentation_failure = rhi_normalize_incomplete_acquired_frame_status(status, operation);
        }
        return presentation_failure;
    }

    void VulkanViewportContext::finish_active_frame()
    {
        frame_active = false;
        active_generation->current_frame_slot =
            (active_generation->current_frame_slot + 1U) %
            static_cast<std::uint32_t>(active_generation->frame_slots.size());
    }
}
