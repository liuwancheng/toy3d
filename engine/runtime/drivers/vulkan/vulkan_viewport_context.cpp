#include "drivers/vulkan/vulkan_viewport_context.h"

#include "drivers/vulkan/vulkan_command_context.h"
#include "drivers/vulkan/vulkan_device.h"
#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_queue.h"
#include "drivers/vulkan/vulkan_upload_manager.h"

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
        VkSemaphore render_finished = VK_NULL_HANDLE;
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

    VulkanViewportContext::VulkanViewportContext(
        VulkanDevice& device,
        RHISurfaceRef surface,
        RHIViewportContextDesc desc)
        : vulkan_device(device)
        , viewport_surface(std::move(surface))
        , viewport_desc(std::move(desc))
    {
    }

    VulkanViewportContext::~VulkanViewportContext()
    {
        if (vulkan_device.device() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(vulkan_device.device());
            vulkan_device.graphics_queue().completed_value();
        }
        const VkSwapchainKHR old_swapchain = vk_swapchain;
        destroy_swapchain();
        if (old_swapchain != VK_NULL_HANDLE && vulkan_device.device() != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(vulkan_device.device(), old_swapchain, nullptr);
        }
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
        if (resize_pending || vk_swapchain == VK_NULL_HANDLE)
        {
            const RHIStatus status = recreate_swapchain();
            if (!status)
            {
                latch_presentation_failure(status);
                return RHIResult<std::unique_ptr<RHIFrameContext>>::failure(status.code(), status.message());
            }
        }

        FrameSlot& slot = frame_slots[current_frame_slot];
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

        VkResult result = vkAcquireNextImageKHR(
            vulkan_device.device(),
            vk_swapchain,
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

        const VkFence image_fence = image_fences[active_image_index];
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

        ++active_frame_id;
        frame_active = true;
        resize_pending = result == VK_SUBOPTIMAL_KHR;
        return RHIResult<std::unique_ptr<RHIFrameContext>>::success(
            std::make_unique<VulkanFrameContext>(
                *this,
                present_textures[active_image_index],
                present_views[active_image_index],
                swapchain_extent.width,
                swapchain_extent.height));
    }

    RHIStatus VulkanViewportContext::end_frame(
        std::unique_ptr<RHIFrameContext> frame,
        const std::vector<RHICommandListRef>& command_lists)
    {
        auto* vulkan_frame = dynamic_cast<VulkanFrameContext*>(frame.get());
        if (!frame_active || vulkan_frame == nullptr || &vulkan_frame->owner() != this)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Frame context does not belong to this Vulkan viewport context.");
        }

        std::vector<VulkanCommandList*> vulkan_command_lists;
        vulkan_command_lists.reserve(command_lists.size());
        std::set<const RHICommandList*> unique_command_lists;
        RHIStatus validation_status = RHIStatus::success();
        for (const RHICommandListRef& command_list : command_lists)
        {
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
            if (vulkan_command_list == nullptr || !vulkan_command_list->belongs_to(*this, active_frame_id))
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
            validation_status = vulkan_command_list->validate_committed_resource_states();
            if (!validation_status)
            {
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
            const RHIStatus recovery_status = abort_frame(std::move(frame));
            return recovery_status ? validation_status : recovery_status;
        }

        RHIStatus status = submit_active_frame(vulkan_command_lists);
        if (status)
        {
            FrameSlot& slot = frame_slots[current_frame_slot];
            slot.submitted_command_lists.insert(
                slot.submitted_command_lists.end(), command_lists.begin(), command_lists.end());
            for (const VulkanCommandList* command_list : vulkan_command_lists)
            {
                command_list->commit_resource_states();
                const std::vector<RHIResourceRef>& resources = command_list->retained_resources();
                slot.submitted_resources.insert(slot.submitted_resources.end(), resources.begin(), resources.end());
                for (const RHIResourceRef& resource : resources)
                {
                    if (const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(resource))
                    {
                        buffer->mark_used(slot.completion_value);
                    }
                    else if (const auto texture = std::dynamic_pointer_cast<VulkanTexture>(resource))
                    {
                        texture->mark_used(slot.completion_value);
                    }
                }
                const std::vector<std::shared_ptr<VulkanUploadPage>>& upload_pages =
                    command_list->retained_upload_pages();
                slot.submitted_upload_pages.insert(
                    slot.submitted_upload_pages.end(), upload_pages.begin(), upload_pages.end());
                vulkan_device.upload_manager().mark_submitted(upload_pages, slot.completion_value);
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
            const auto active_texture =
                std::dynamic_pointer_cast<VulkanTexture>(present_textures[active_image_index]);
            if (active_texture)
            {
                active_texture->set_state(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, RHIAccess::Present);
            }
            RHIStatus command_list_status = RHIStatus::success();
            for (VulkanCommandList* command_list : vulkan_command_lists)
            {
                command_list_status = command_list->mark_submitted_by_viewport();
                if (!command_list_status)
                {
                    break;
                }
            }
            const RHIStatus present_status = present_active_image();
            if (!present_status && !rhi_is_recoverable_viewport_status(present_status))
            {
                status = present_status;
            }
            else if (!command_list_status)
            {
                status = latch_presentation_failure(command_list_status);
            }
            else
            {
                status = present_status;
            }
        }
        finish_active_frame();
        return status;
    }

    RHIStatus VulkanViewportContext::abort_frame(std::unique_ptr<RHIFrameContext> frame)
    {
        auto* vulkan_frame = dynamic_cast<VulkanFrameContext*>(frame.get());
        if (!frame_active || vulkan_frame == nullptr || &vulkan_frame->owner() != this)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Frame context does not belong to this active Vulkan viewport frame.");
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
        if (!frame_active || frame_slots.empty())
        {
            return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics command contexts can be created only for an active viewport frame.");
        }
        return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::success(
            std::make_unique<VulkanGraphicsCommandContext>(
                vulkan_device,
                *this,
                frame_slots[current_frame_slot].command_pool,
                active_frame_id));
    }

    RHIStatus VulkanViewportContext::recreate_swapchain()
    {
        if (pending_width != 0)
        {
            viewport_desc.width = pending_width;
            viewport_desc.height = pending_height;
        }
        RHIStatus status = make_vulkan_status(vkDeviceWaitIdle(vulkan_device.device()), "vkDeviceWaitIdle");
        if (!status)
        {
            return status;
        }
        vulkan_device.graphics_queue().completed_value();

        const VkSwapchainKHR old_swapchain = vk_swapchain;
        destroy_swapchain();
        status = create_swapchain(old_swapchain);
        if (!status)
        {
            if (old_swapchain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(vulkan_device.device(), old_swapchain, nullptr);
            }
            return status;
        }
        if (old_swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(vulkan_device.device(), old_swapchain, nullptr);
        }
        pending_width = 0;
        pending_height = 0;
        resize_pending = false;
        current_frame_slot = 0;
        return RHIStatus::success();
    }

    RHIStatus VulkanViewportContext::create_swapchain(VkSwapchainKHR old_swapchain)
    {
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

        const VkFormat requested_format = vulkan_format_from_rhi(viewport_desc.format);
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
        status = make_vulkan_status(vkCreateSwapchainKHR(vulkan_device.device(), &create_info, nullptr, &vk_swapchain), "vkCreateSwapchainKHR");
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
            status = make_vulkan_status(vkCreateImageView(vulkan_device.device(), &view_info, nullptr, &swapchain_image_views[index]), "vkCreateImageView");
            if (!status)
            {
                return status;
            }

            RHITextureDesc texture_desc;
            texture_desc.width = extent.width;
            texture_desc.height = extent.height;
            texture_desc.format = viewport_desc.format;
            texture_desc.usage = rhi_enum_or(RHIResourceUsage::RenderTarget, RHIResourceUsage::CopyDestination);
            texture_desc.initial_access = RHIAccess::Present;
            texture_desc.debug_name = viewport_desc.debug_name + ".Image" + std::to_string(index);
            RHITextureRef texture = std::make_shared<VulkanTexture>(
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

        frame_slots.resize(actual_image_count);
        for (FrameSlot& slot : frame_slots)
        {
            VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            status = make_vulkan_status(vkCreateSemaphore(vulkan_device.device(), &semaphore_info, nullptr, &slot.image_available), "vkCreateSemaphore");
            if (!status)
            {
                return status;
            }
            status = make_vulkan_status(vkCreateSemaphore(vulkan_device.device(), &semaphore_info, nullptr, &slot.render_finished), "vkCreateSemaphore");
            if (!status)
            {
                return status;
            }
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            status = make_vulkan_status(vkCreateFence(vulkan_device.device(), &fence_info, nullptr, &slot.completion_fence), "vkCreateFence");
            if (!status)
            {
                return status;
            }
            VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool_info.queueFamilyIndex = vulkan_device.graphics_queue_family_index();
            status = make_vulkan_status(vkCreateCommandPool(vulkan_device.device(), &pool_info, nullptr, &slot.command_pool), "vkCreateCommandPool");
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

        image_fences.assign(actual_image_count, VK_NULL_HANDLE);
        return RHIStatus::success();
    }

    void VulkanViewportContext::destroy_swapchain()
    {
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
            if (slot.render_finished != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, slot.render_finished, nullptr);
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
        image_fences.clear();
        swapchain_images.clear();
        vk_swapchain = VK_NULL_HANDLE;
    }

    RHIStatus VulkanViewportContext::submit_active_frame(const std::vector<VulkanCommandList*>& command_lists)
    {
        FrameSlot& slot = frame_slots[current_frame_slot];
        const auto active_texture = std::dynamic_pointer_cast<VulkanTexture>(present_textures[active_image_index]);
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
            barrier.image = swapchain_images[active_image_index];
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
            command_buffers,
            slot.image_available,
            wait_stage,
            slot.render_finished,
            slot.completion_fence);
        if (!submit_result)
        {
            // image_available may remain signaled and the acquired image was
            // not returned to the presentation engine. Retrying this viewport
            // would reuse synchronization with an unknown state.
            latch_incomplete_active_frame_failure(
                submit_result.status(),
                "Vulkan viewport submission");
            const VkFence discarded_fence = slot.completion_fence;
            for (VkFence& image_fence : image_fences)
            {
                if (image_fence == discarded_fence)
                {
                    image_fence = VK_NULL_HANDLE;
                }
            }
            vkDestroyFence(vulkan_device.device(), discarded_fence, nullptr);
            slot.completion_fence = VK_NULL_HANDLE;
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            const RHIStatus fence_status = make_vulkan_status(
                vkCreateFence(vulkan_device.device(), &fence_info, nullptr, &slot.completion_fence),
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
            image_fences[active_image_index] = slot.completion_fence;
        }
        return RHIStatus::success();
    }

    RHIStatus VulkanViewportContext::present_active_image()
    {
        FrameSlot& slot = frame_slots[current_frame_slot];
        VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &slot.render_finished;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &vk_swapchain;
        present_info.pImageIndices = &active_image_index;
        const VkResult result = vkQueuePresentKHR(vulkan_device.graphics_queue_handle(), &present_info);
        if (result == VK_SUBOPTIMAL_KHR)
        {
            resize_pending = true;
            return RHIStatus::failure(
                RHIErrorCode::Suboptimal,
                "vkQueuePresentKHR completed with a suboptimal swapchain; recreation is pending.");
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            resize_pending = true;
        }
        const RHIStatus status = make_vulkan_status(result, "vkQueuePresentKHR");
        if (!status && !rhi_is_recoverable_viewport_status(status))
        {
            presentation_failure = status;
        }
        return status;
    }

    RHIStatus VulkanViewportContext::abort_active_frame()
    {
        RHIStatus status = submit_active_frame({});
        if (status)
        {
            const auto active_texture =
                std::dynamic_pointer_cast<VulkanTexture>(present_textures[active_image_index]);
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
            }
        }
        finish_active_frame();
        return status;
    }

    RHIStatus VulkanViewportContext::latch_presentation_failure(const RHIStatus& status)
    {
        if (!status && !rhi_is_recoverable_viewport_status(status))
        {
            presentation_failure = status;
        }
        return status;
    }

    RHIStatus VulkanViewportContext::latch_incomplete_active_frame_failure(
        const RHIStatus& status,
        const char* operation)
    {
        presentation_failure = rhi_normalize_incomplete_acquired_frame_status(status, operation);
        return presentation_failure;
    }

    void VulkanViewportContext::finish_active_frame()
    {
        frame_active = false;
        current_frame_slot = (current_frame_slot + 1U) % static_cast<std::uint32_t>(frame_slots.size());
    }
}
