#include "drivers/vulkan/vulkan_viewport_context.h"

#include "drivers/vulkan/vulkan_command_context.h"
#include "drivers/vulkan/vulkan_deferred_deletion.h"
#include "drivers/vulkan/vulkan_queue.h"
#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_swapchain.h"
#include "drivers/vulkan/vulkan_upload_manager.h"

#include <set>
#include <string>
#include <utility>

namespace toy3d
{
    namespace
    {
        RHIStatus make_viewport_status(VkResult result, const char* operation)
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
            else if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            {
                code = RHIErrorCode::OutOfMemory;
            }
            return RHIStatus::failure(code, std::string(operation) + " failed with VkResult " +
                                                std::to_string(static_cast<int>(result)) + ".");
        }

        void get_present_source_sync(RHIAccess access, VkPipelineStageFlags& pipeline_stage, VkAccessFlags& access_mask)
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

        class VulkanFrameContext final : public RHIFrameContext
        {
          public:
            VulkanFrameContext(VulkanViewportContext& owner, RHITextureRef texture, RHITextureViewRef view,
                               std::uint32_t width, std::uint32_t height)
                : viewport(owner), output_texture(std::move(texture)), output_view(std::move(view)), frame_width(width),
                  frame_height(height)
            {
            }

            const RHITextureRef& present_texture() const override { return output_texture; }
            const RHITextureViewRef& present_view() const override { return output_view; }
            std::uint32_t width() const override { return frame_width; }
            std::uint32_t height() const override { return frame_height; }

            RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> create_graphics_command_context() override
            {
                return viewport.create_graphics_command_context();
            }

            VulkanViewportContext& owner() const { return viewport; }

          private:
            VulkanViewportContext& viewport;
            RHITextureRef output_texture;
            RHITextureViewRef output_view;
            std::uint32_t frame_width = 0;
            std::uint32_t frame_height = 0;
        };
    } // namespace

    VulkanViewportContext::VulkanViewportContext(const RHIDevice& owner, VkPhysicalDevice physical_device,
                                                 VkDevice device, VkSurfaceKHR surface, std::uint32_t queue_family,
                                                 VulkanQueue& queue, VulkanUploadManager& uploads,
                                                 VulkanDeferredDeletionQueue& deletions, RHISurfaceRef rhi_surface,
                                                 RHIViewportContextDesc desc)
        : RHIViewportContext(owner, desc.debug_name), owner_device(owner), vk_physical_device(physical_device),
          vk_device(device), vk_surface(surface), graphics_queue_family(queue_family), graphics_queue(queue),
          upload_manager(uploads), deletion_queue(deletions), viewport_surface(std::move(rhi_surface)),
          viewport_desc(std::move(desc)), resize_pending(true), pending_width(viewport_desc.width),
          pending_height(viewport_desc.height)
    {
    }

    VulkanViewportContext::~VulkanViewportContext()
    {
        if (vk_device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(vk_device);
            graphics_queue.completed_value();
        }
        destroy_frame_slots(frame_slots);
        swapchain.reset();
    }

    RHIResult<std::unique_ptr<RHIFrameContext>> VulkanViewportContext::begin_frame()
    {
        if (!presentation_failure)
        {
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(presentation_failure.code(),
                                                                        presentation_failure.message());
        }
        if (frame_active)
        {
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(
                RHIErrorCode::InvalidArgument, "A Vulkan viewport context already has an active frame.");
        }
        if (resize_pending || !swapchain)
        {
            const RHIStatus status = recreate_swapchain();
            if (!status)
            {
                latch_presentation_failure(status);
                return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
            }
        }

        VulkanFrameSlot& slot = frame_slots[current_frame_slot];
        RHIStatus status = make_viewport_status(
            vkWaitForFences(vk_device, 1, &slot.submission_fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        if (!status)
        {
            latch_presentation_failure(status);
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
        }
        const RHIQueueCompletionValue completed_value = graphics_queue.completed_value();
        upload_manager.release_completed(completed_value);
        deletion_queue.release_completed(vk_device, completed_value);
        slot.submitted_command_lists.clear();
        slot.submitted_resources.clear();
        slot.submitted_upload_pages.clear();
        slot.submitted_texture_views.clear();
        slot.submitted_graphics_pipelines.clear();
        slot.submitted_binding_sets.clear();
        slot.submitted_render_pass_resources.clear();
        status = make_viewport_status(vkResetCommandPool(vk_device, slot.command_pool, 0), "vkResetCommandPool");
        if (!status)
        {
            latch_presentation_failure(status);
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
        }

        auto acquire_result = swapchain->acquire_image(slot.image_acquired);
        if (!acquire_result)
        {
            if (acquire_result.status().code() == RHIErrorCode::OutOfDate)
            {
                resize_pending = true;
            }
            else
            {
                latch_presentation_failure(acquire_result.status());
            }
            return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(acquire_result.status().code(),
                                                                        acquire_result.status().message());
        }
        active_image_index = acquire_result.value().image_index;
        VulkanSwapchainImage& acquired_image = swapchain->image(active_image_index);
        if (acquired_image.last_submission_fence != VK_NULL_HANDLE &&
            acquired_image.last_submission_fence != slot.submission_fence)
        {
            status = make_viewport_status(
                vkWaitForFences(vk_device, 1, &acquired_image.last_submission_fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences");
            if (!status)
            {
                latch_incomplete_active_frame_failure(status, "vkWaitForFences");
                return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(presentation_failure.code(),
                                                                            presentation_failure.message());
            }
        }

        ++active_frame_id;
        frame_active = true;
        resize_pending = acquire_result.value().presentation_status.code() == RHIErrorCode::Suboptimal;
        const VkExtent2D extent = swapchain->extent();
        return RHIResult<std::unique_ptr<RHIFrameContext>>::success(std::make_unique<VulkanFrameContext>(
            *this, acquired_image.texture, acquired_image.view, extent.width, extent.height));
    }

    RHIResult<RHIFrameEndResult> VulkanViewportContext::end_frame(std::unique_ptr<RHIFrameContext> frame,
                                                                  const std::vector<RHICommandListRef>& command_lists)
    {
        auto* vulkan_frame = dynamic_cast<VulkanFrameContext*>(frame.get());
        const auto fail_before_submit = [this](const RHIStatus& failure)
        {
            if (!frame_active)
            {
                return RHIResult<RHIFrameEndResult>::failure(failure.code(), failure.message());
            }
            const RHIStatus recovery = abort_active_frame();
            const RHIStatus reported = recovery || rhi_is_recoverable_viewport_status(recovery) ? failure : recovery;
            return RHIResult<RHIFrameEndResult>::failure(reported.code(), reported.message());
        };
        if (!frame_active || vulkan_frame == nullptr || &vulkan_frame->owner() != this)
        {
            const RHIStatus invalid = RHIStatus::failure(
                RHIErrorCode::InvalidArgument, "Frame context does not belong to this Vulkan viewport context.");
            if (vulkan_frame != nullptr && &vulkan_frame->owner() != this)
            {
                VulkanViewportContext& owner = vulkan_frame->owner();
                const RHIStatus recovery = owner.abort_frame(std::move(frame));
                const RHIStatus reported =
                    recovery || rhi_is_recoverable_viewport_status(recovery) ? invalid : recovery;
                return RHIResult<RHIFrameEndResult>::failure(reported.code(), reported.message());
            }
            return fail_before_submit(invalid);
        }

        std::vector<VulkanCommandList*> vulkan_lists;
        vulkan_lists.reserve(command_lists.size());
        std::set<const RHICommandList*> unique_lists;
        RHIStatus validation =
            command_lists.size() == 1
                ? RHIStatus::success()
                : RHIStatus::failure(
                      RHIErrorCode::InvalidArgument,
                      "The first renderer stage requires exactly one Vulkan viewport business command list.");
        for (const RHICommandListRef& command_list : command_lists)
        {
            if (!validation)
            {
                break;
            }
            auto* vulkan_list = dynamic_cast<VulkanCommandList*>(command_list.get());
            if (!command_list || !unique_lists.emplace(command_list.get()).second || vulkan_list == nullptr ||
                !vulkan_list->is_owned_by(owner_device) || !vulkan_list->belongs_to(*this, active_frame_id) ||
                vulkan_list->state() != RHICommandListState::Closed)
            {
                validation = RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan viewport submission requires one unique, closed command list from the active frame.");
                break;
            }
            for (const VulkanCommandList* previous : vulkan_lists)
            {
                if (vulkan_list->has_state_overlap(*previous))
                {
                    validation = RHIStatus::failure(
                        RHIErrorCode::Unsupported,
                        "Vulkan cannot yet submit command lists with overlapping state transitions.");
                    break;
                }
            }
            vulkan_lists.push_back(vulkan_list);
        }
        if (!validation)
        {
            return fail_before_submit(validation);
        }

        const RHIStatus submit_status = submit_active_frame(vulkan_lists);
        if (!submit_status)
        {
            finish_active_frame();
            return RHIResult<RHIFrameEndResult>::failure(submit_status.code(), submit_status.message());
        }

        VulkanFrameSlot& slot = frame_slots[current_frame_slot];
        slot.submitted_command_lists.insert(slot.submitted_command_lists.end(), command_lists.begin(),
                                            command_lists.end());
        for (VulkanCommandList* command_list : vulkan_lists)
        {
            slot.submitted_resources.insert(slot.submitted_resources.end(), command_list->retained_resources().begin(),
                                            command_list->retained_resources().end());
            slot.submitted_upload_pages.insert(slot.submitted_upload_pages.end(),
                                               command_list->retained_upload_pages().begin(),
                                               command_list->retained_upload_pages().end());
            slot.submitted_texture_views.insert(slot.submitted_texture_views.end(),
                                                command_list->retained_texture_views().begin(),
                                                command_list->retained_texture_views().end());
            slot.submitted_graphics_pipelines.insert(slot.submitted_graphics_pipelines.end(),
                                                     command_list->retained_graphics_pipelines().begin(),
                                                     command_list->retained_graphics_pipelines().end());
            slot.submitted_binding_sets.insert(slot.submitted_binding_sets.end(),
                                               command_list->retained_binding_sets().begin(),
                                               command_list->retained_binding_sets().end());
            slot.submitted_render_pass_resources.insert(slot.submitted_render_pass_resources.end(),
                                                        command_list->retained_render_pass_resources().begin(),
                                                        command_list->retained_render_pass_resources().end());
        }

        RHIStatus post_submit_status = RHIStatus::success();
        for (VulkanCommandList* command_list : vulkan_lists)
        {
            post_submit_status = command_list->mark_submitted_by_viewport();
            if (!post_submit_status)
            {
                post_submit_status = latch_presentation_failure(post_submit_status);
                break;
            }
        }
        const RHIStatus present_status =
            rhi_normalize_submitted_presentation_status(present_active_image(), "Vulkan presentation");
        if (!present_status && !rhi_is_recoverable_viewport_status(present_status))
        {
            latch_presentation_failure(present_status);
        }
        const RHIStatus reported_status = post_submit_status ? present_status : post_submit_status;
        const RHIFrameEndResult result{slot.completion_value, reported_status};
        finish_active_frame();
        return RHIResult<RHIFrameEndResult>::success(result);
    }

    RHIStatus VulkanViewportContext::abort_frame(std::unique_ptr<RHIFrameContext> frame)
    {
        auto* vulkan_frame = dynamic_cast<VulkanFrameContext*>(frame.get());
        if (!frame_active || vulkan_frame == nullptr || &vulkan_frame->owner() != this)
        {
            const RHIStatus invalid = RHIStatus::failure(
                RHIErrorCode::InvalidArgument, "Frame context does not belong to this active Vulkan viewport frame.");
            if (vulkan_frame != nullptr && &vulkan_frame->owner() != this)
            {
                VulkanViewportContext& owner = vulkan_frame->owner();
                const RHIStatus recovery = owner.abort_frame(std::move(frame));
                return recovery || rhi_is_recoverable_viewport_status(recovery) ? invalid : recovery;
            }
            if (!frame_active)
            {
                return invalid;
            }
            const RHIStatus recovery = abort_active_frame();
            return recovery || rhi_is_recoverable_viewport_status(recovery) ? invalid : recovery;
        }
        return abort_active_frame();
    }

    RHIStatus VulkanViewportContext::request_resize(std::uint32_t width, std::uint32_t height)
    {
        pending_width = width;
        pending_height = height;
        resize_pending = true;
        if (width == 0 || height == 0)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "A zero-sized viewport cannot be presented.");
        }
        return RHIStatus::success();
    }

    RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> VulkanViewportContext::create_graphics_command_context()
    {
        if (!frame_active || !swapchain || frame_slots.empty())
        {
            return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::failure(
                RHIErrorCode::InvalidArgument, "Vulkan graphics command contexts require an active viewport frame.");
        }
        return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::success(
            std::make_unique<VulkanGraphicsCommandContext>(owner_device, vk_device, upload_manager, *this,
                                                           frame_slots[current_frame_slot].command_pool,
                                                           active_frame_id));
    }

    RHIStatus VulkanViewportContext::recreate_swapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        RHIStatus status = make_viewport_status(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device, vk_surface, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if (!status)
        {
            return status;
        }
        if (pending_width == 0 || pending_height == 0 || capabilities.currentExtent.width == 0 ||
            capabilities.currentExtent.height == 0)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "The Vulkan presentation surface has a zero extent.");
        }

        if (swapchain)
        {
            ++recreate_queue_idle_count;
            status = graphics_queue.wait_idle();
            if (!status)
            {
                return status;
            }
            const RHIQueueCompletionValue completed_value = graphics_queue.completed_value();
            upload_manager.release_completed(completed_value);
            deletion_queue.release_completed(vk_device, completed_value);
        }

        RHIViewportContextDesc replacement_desc = viewport_desc;
        replacement_desc.width = pending_width;
        replacement_desc.height = pending_height;
        auto replacement_result =
            VulkanSwapchain::create(owner_device, vk_physical_device, vk_device, vk_surface, replacement_desc,
                                    swapchain ? swapchain->native_handle() : VK_NULL_HANDLE);
        if (!replacement_result)
        {
            ++rejected_swapchain_construction_count;
            return replacement_result.status();
        }
        std::unique_ptr<VulkanSwapchain> replacement = std::move(replacement_result).value();
        std::vector<VulkanFrameSlot> replacement_slots;
        status = create_frame_slots(vulkan_frame_slot_count(replacement->image_count()), replacement_slots);
        if (!status)
        {
            ++rejected_swapchain_construction_count;
            destroy_frame_slots(replacement_slots);
            return status;
        }

        destroy_frame_slots(frame_slots);
        swapchain = std::move(replacement);
        frame_slots = std::move(replacement_slots);
        current_frame_slot = 0;
        viewport_desc = std::move(replacement_desc);
        pending_width = viewport_desc.width;
        pending_height = viewport_desc.height;
        resize_pending = false;
        ++swapchain_publication_id;
        return RHIStatus::success();
    }

    RHIStatus VulkanViewportContext::create_frame_slots(std::uint32_t count, std::vector<VulkanFrameSlot>& output_slots)
    {
        if (count == 0)
        {
            return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                      "Vulkan swapchain returned no images for frame-slot creation.");
        }
        output_slots.resize(count);
        for (VulkanFrameSlot& slot : output_slots)
        {
            VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            RHIStatus status = make_viewport_status(
                vkCreateSemaphore(vk_device, &semaphore_info, nullptr, &slot.image_acquired), "vkCreateSemaphore");
            if (!status)
            {
                return status;
            }
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            status = make_viewport_status(vkCreateFence(vk_device, &fence_info, nullptr, &slot.submission_fence),
                                          "vkCreateFence");
            if (!status)
            {
                return status;
            }
            VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool_info.queueFamilyIndex = graphics_queue_family;
            status = make_viewport_status(vkCreateCommandPool(vk_device, &pool_info, nullptr, &slot.command_pool),
                                          "vkCreateCommandPool");
            if (!status)
            {
                return status;
            }
            VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            command_info.commandPool = slot.command_pool;
            command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command_info.commandBufferCount = 1;
            status = make_viewport_status(
                vkAllocateCommandBuffers(vk_device, &command_info, &slot.present_transition_command_buffer),
                "vkAllocateCommandBuffers");
            if (!status)
            {
                return status;
            }
        }
        return RHIStatus::success();
    }

    void VulkanViewportContext::destroy_frame_slots(std::vector<VulkanFrameSlot>& slots)
    {
        const VkDevice device = vk_device;
        for (VulkanFrameSlot& slot : slots)
        {
            if (device != VK_NULL_HANDLE && slot.image_acquired != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, slot.image_acquired, nullptr);
            }
            if (device != VK_NULL_HANDLE && slot.submission_fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(device, slot.submission_fence, nullptr);
            }
            if (device != VK_NULL_HANDLE && slot.command_pool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(device, slot.command_pool, nullptr);
            }
        }
        slots.clear();
    }

    VulkanViewportObservation VulkanViewportContext::observation_snapshot() const
    {
        VulkanViewportObservation observation;
        observation.swapchain_publication_id = swapchain_publication_id;
        observation.rejected_swapchain_construction_count = rejected_swapchain_construction_count;
        observation.frame_slot_count = frame_slots.size();
        observation.swapchain_image_count = swapchain ? swapchain->image_count() : 0;
        observation.recreate_queue_idle_count = recreate_queue_idle_count;
        return observation;
    }

    RHIStatus VulkanViewportContext::submit_active_frame(const std::vector<VulkanCommandList*>& command_lists)
    {
        VulkanFrameSlot& slot = frame_slots[current_frame_slot];
        VulkanSwapchainImage& image = swapchain->image(active_image_index);
        const auto active_texture = std::dynamic_pointer_cast<VulkanTexture>(image.texture);
        if (!active_texture)
        {
            return latch_incomplete_active_frame_failure(
                RHIStatus::failure(RHIErrorCode::BackendFailure,
                                   "Vulkan viewport lost its native swapchain texture wrapper."),
                "Vulkan present transition recording");
        }

        VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        RHIStatus status = make_viewport_status(
            vkBeginCommandBuffer(slot.present_transition_command_buffer, &begin_info), "vkBeginCommandBuffer");
        if (!status)
        {
            return latch_incomplete_active_frame_failure(status, "vkBeginCommandBuffer");
        }
        VkImageLayout layout = active_texture->image_layout();
        RHIAccess access = active_texture->current_access();
        for (const VulkanCommandList* command_list : command_lists)
        {
            command_list->try_get_tracked_texture_state(active_texture, layout, access);
        }
        if (layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            VkPipelineStageFlags source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags source_access = 0;
            if (layout != VK_IMAGE_LAYOUT_UNDEFINED)
            {
                get_present_source_sync(access, source_stage, source_access);
            }
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = layout;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcAccessMask = source_access;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(slot.present_transition_command_buffer, source_stage,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        status = make_viewport_status(vkEndCommandBuffer(slot.present_transition_command_buffer), "vkEndCommandBuffer");
        if (!status)
        {
            return latch_incomplete_active_frame_failure(status, "vkEndCommandBuffer");
        }
        status = make_viewport_status(vkResetFences(vk_device, 1, &slot.submission_fence), "vkResetFences");
        if (!status)
        {
            return latch_incomplete_active_frame_failure(status, "vkResetFences");
        }

        std::vector<VkCommandBuffer> command_buffers;
        command_buffers.reserve(command_lists.size() + 1U);
        for (const VulkanCommandList* command_list : command_lists)
        {
            command_buffers.push_back(command_list->command_buffer());
        }
        command_buffers.push_back(slot.present_transition_command_buffer);
        VulkanQueue& queue = graphics_queue;
        const auto submit_result =
            queue.submit_viewport(command_lists, command_buffers, *active_texture, slot.image_acquired,
                                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, image.rendering_done, slot.submission_fence);
        if (!submit_result)
        {
            image.last_submission_fence = VK_NULL_HANDLE;
            vkDestroyFence(vk_device, slot.submission_fence, nullptr);
            slot.submission_fence = VK_NULL_HANDLE;
            return latch_incomplete_active_frame_failure(submit_result.status(), "Vulkan viewport submission");
        }
        slot.completion_value = submit_result.value().completion_value;
        image.last_submission_fence = slot.submission_fence;
        return RHIStatus::success();
    }

    RHIStatus VulkanViewportContext::present_active_image()
    {
        VulkanSwapchainImage& image = swapchain->image(active_image_index);
        const RHIStatus status =
            swapchain->present(graphics_queue.native_handle(), active_image_index, image.rendering_done);
        if (status.code() == RHIErrorCode::Suboptimal || status.code() == RHIErrorCode::OutOfDate)
        {
            resize_pending = true;
        }
        return status;
    }

    RHIStatus VulkanViewportContext::abort_active_frame()
    {
        RHIStatus status = submit_active_frame({});
        if (status)
        {
            const auto active_texture =
                std::dynamic_pointer_cast<VulkanTexture>(swapchain->image(active_image_index).texture);
            if (!active_texture)
            {
                status = latch_incomplete_active_frame_failure(
                    RHIStatus::failure(RHIErrorCode::BackendFailure,
                                       "Vulkan viewport lost its swapchain texture after abort submission."),
                    "Vulkan aborted-frame presentation");
            }
            else
            {
                active_texture->set_state(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, RHIAccess::Present);
                status = present_active_image();
                if (!status)
                {
                    status = latch_incomplete_active_frame_failure(status, "Vulkan aborted-frame presentation");
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
        return presentation_failure ? status : presentation_failure;
    }

    RHIStatus VulkanViewportContext::latch_incomplete_active_frame_failure(const RHIStatus& status,
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
        if (!frame_slots.empty())
        {
            current_frame_slot = (current_frame_slot + 1U) % static_cast<std::uint32_t>(frame_slots.size());
        }
    }
} // namespace toy3d
