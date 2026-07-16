#include "drivers/vulkan/canonical/vulkan_command_context.h"

#include "drivers/vulkan/canonical/vulkan_device.h"
#include "drivers/vulkan/canonical/vulkan_resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace toy3d
{
    namespace
    {
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
            else if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            {
                code = RHIErrorCode::OutOfMemory;
            }
            return RHIStatus::failure(
                code,
                std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result)) + ".");
        }

        struct VulkanAccessState
        {
            VkPipelineStageFlags pipeline_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags access_mask = 0;
            VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            bool supports_buffer = false;
            bool supports_image = false;
        };

        RHIStatus get_vulkan_access_state(RHIAccess access, VulkanAccessState& state)
        {
            switch (access)
            {
            case RHIAccess::Common:
                state = {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, true, true};
                return RHIStatus::success();
            case RHIAccess::Present:
                state = {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, false, true};
                return RHIStatus::success();
            case RHIAccess::VertexBuffer:
                state = {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, true, false};
                return RHIStatus::success();
            case RHIAccess::IndexBuffer:
                state = {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_INDEX_READ_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, true, false};
                return RHIStatus::success();
            case RHIAccess::VertexOrIndexBuffer:
                state = {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, true, false};
                return RHIStatus::success();
            case RHIAccess::UniformBuffer:
                state = {VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, true, false};
                return RHIStatus::success();
            case RHIAccess::IndirectArguments:
                state = {VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, true, false};
                return RHIStatus::success();
            case RHIAccess::ShaderResourceGraphics:
                state = {VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true};
                return RHIStatus::success();
            case RHIAccess::ShaderResourceCompute:
                state = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true};
                return RHIStatus::success();
            case RHIAccess::UnorderedAccessGraphics:
                state = {VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, true, true};
                return RHIStatus::success();
            case RHIAccess::UnorderedAccessCompute:
                state = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, true, true};
                return RHIStatus::success();
            case RHIAccess::RenderTarget:
                state = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false, true};
                return RHIStatus::success();
            case RHIAccess::DepthStencilRead:
                state = {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, false, true};
                return RHIStatus::success();
            case RHIAccess::DepthStencilWrite:
                state = {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false, true};
                return RHIStatus::success();
            case RHIAccess::CopySource:
            case RHIAccess::ResolveSource:
                state = {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, true, true};
                return RHIStatus::success();
            case RHIAccess::CopyDestination:
            case RHIAccess::ResolveDestination:
                state = {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, true};
                return RHIStatus::success();
            default:
                return RHIStatus::failure(
                    RHIErrorCode::Unsupported,
                    "The requested RHI access mask has no canonical Vulkan transition mapping.");
            }
        }

        RHIResult<VkImageAspectFlags> to_vk_image_aspect(RHITextureAspect aspect, VkFormat format)
        {
            const bool has_depth = is_vk_depth_format(format);
            const bool has_stencil = is_vk_stencil_format(format);
            switch (aspect)
            {
            case RHITextureAspect::Color:
                if (!has_depth)
                {
                    return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_COLOR_BIT);
                }
                break;
            case RHITextureAspect::Depth:
                if (has_depth)
                {
                    return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_DEPTH_BIT);
                }
                break;
            case RHITextureAspect::Stencil:
                if (has_stencil)
                {
                    return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_STENCIL_BIT);
                }
                break;
            case RHITextureAspect::DepthStencil:
                if (has_depth && has_stencil)
                {
                    return RHIResult<VkImageAspectFlags>::success(
                        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
                }
                break;
            }
            return RHIResult<VkImageAspectFlags>::failure(
                RHIErrorCode::InvalidArgument,
                "Texture transition aspect is incompatible with its Vulkan format.");
        }

        bool is_full_texture_range(const RHITextureDesc& desc, const RHISubresourceRange& range)
        {
            const bool all_mips = range.mip_count == RHI_ALL_MIPS || range.mip_count == desc.mip_levels;
            const bool all_layers = range.layer_count == RHI_ALL_LAYERS || range.layer_count == desc.array_layers;
            return range.first_mip == 0 && range.first_layer == 0 && all_mips && all_layers;
        }

        RHIResult<std::uint32_t> color_format_bytes_per_texel(RHIFormat format)
        {
            switch (format)
            {
            case RHIFormat::R8UNorm:
            case RHIFormat::R8SNorm:
                return RHIResult<std::uint32_t>::success(1);
            case RHIFormat::R16Float:
            case RHIFormat::R16UInt:
                return RHIResult<std::uint32_t>::success(2);
            case RHIFormat::R16G16Float:
                return RHIResult<std::uint32_t>::success(4);
            case RHIFormat::R8G8B8A8UNorm:
            case RHIFormat::R8G8B8A8UNormSRGB:
            case RHIFormat::B8G8R8A8UNorm:
            case RHIFormat::B8G8R8A8UNormSRGB:
            case RHIFormat::R8G8B8A8SNorm:
            case RHIFormat::R10G10B10A2UNorm:
            case RHIFormat::R11G11B10Float:
            case RHIFormat::R32Float:
            case RHIFormat::R32UInt:
                return RHIResult<std::uint32_t>::success(4);
            case RHIFormat::R16G16B16A16Float:
            case RHIFormat::R32G32Float:
                return RHIResult<std::uint32_t>::success(8);
            case RHIFormat::R32G32B32Float:
                return RHIResult<std::uint32_t>::success(12);
            case RHIFormat::R32G32B32A32Float:
                return RHIResult<std::uint32_t>::success(16);
            default:
                return RHIResult<std::uint32_t>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan texture upload currently supports only uncompressed color formats.");
            }
        }

        void record_staging_buffer_barrier(VkCommandBuffer command_buffer, VkBuffer staging_buffer)
        {
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = staging_buffer;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(
                command_buffer,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                1,
                &barrier,
                0,
                nullptr);
        }

        RHIResult<VkAttachmentLoadOp> to_vk_load_operation(RHILoadOperation operation)
        {
            switch (operation)
            {
            case RHILoadOperation::Load:
                return RHIResult<VkAttachmentLoadOp>::success(VK_ATTACHMENT_LOAD_OP_LOAD);
            case RHILoadOperation::Clear:
                return RHIResult<VkAttachmentLoadOp>::success(VK_ATTACHMENT_LOAD_OP_CLEAR);
            case RHILoadOperation::Discard:
                return RHIResult<VkAttachmentLoadOp>::success(VK_ATTACHMENT_LOAD_OP_DONT_CARE);
            }
            return RHIResult<VkAttachmentLoadOp>::failure(RHIErrorCode::InvalidArgument, "Invalid RHI load operation.");
        }

        RHIResult<VkAttachmentStoreOp> to_vk_store_operation(RHIStoreOperation operation)
        {
            switch (operation)
            {
            case RHIStoreOperation::Store:
                return RHIResult<VkAttachmentStoreOp>::success(VK_ATTACHMENT_STORE_OP_STORE);
            case RHIStoreOperation::Discard:
                return RHIResult<VkAttachmentStoreOp>::success(VK_ATTACHMENT_STORE_OP_DONT_CARE);
            }
            return RHIResult<VkAttachmentStoreOp>::failure(RHIErrorCode::InvalidArgument, "Invalid RHI store operation.");
        }
    }

    VulkanCommandList::VulkanCommandList(
        VulkanViewportContext& owner,
        VkCommandBuffer command_buffer,
        std::uint64_t frame_id,
        std::string debug_name)
        : RHICommandList(std::move(debug_name))
        , viewport_owner(&owner)
        , vk_command_buffer(command_buffer)
        , command_frame_id(frame_id)
    {
    }

    VkCommandBuffer VulkanCommandList::command_buffer() const
    {
        return vk_command_buffer;
    }

    bool VulkanCommandList::belongs_to(const VulkanViewportContext& viewport, std::uint64_t frame_id) const
    {
        return viewport_owner == &viewport && command_frame_id == frame_id;
    }

    void VulkanCommandList::retain_resource(const RHIResourceRef& resource)
    {
        if (!resource)
        {
            return;
        }
        const auto existing = std::find(resources.begin(), resources.end(), resource);
        if (existing == resources.end())
        {
            resources.push_back(resource);
        }
    }

    const std::vector<RHIResourceRef>& VulkanCommandList::retained_resources() const
    {
        return resources;
    }

    void VulkanCommandList::retain_staging_buffer(std::shared_ptr<VulkanStagingBuffer> staging_buffer)
    {
        if (staging_buffer)
        {
            staging_buffers.push_back(std::move(staging_buffer));
        }
    }

    const std::vector<std::shared_ptr<VulkanStagingBuffer>>& VulkanCommandList::retained_staging_buffers() const
    {
        return staging_buffers;
    }

    void VulkanCommandList::retain_texture_view(const RHITextureViewRef& view)
    {
        if (view && std::find(texture_views.begin(), texture_views.end(), view) == texture_views.end())
        {
            texture_views.push_back(view);
        }
    }

    const std::vector<RHITextureViewRef>& VulkanCommandList::retained_texture_views() const
    {
        return texture_views;
    }

    void VulkanCommandList::retain_graphics_pipeline(const RHIGraphicsPipelineRef& pipeline)
    {
        if (pipeline && std::find(graphics_pipelines.begin(), graphics_pipelines.end(), pipeline) == graphics_pipelines.end())
        {
            graphics_pipelines.push_back(pipeline);
        }
    }

    const std::vector<RHIGraphicsPipelineRef>& VulkanCommandList::retained_graphics_pipelines() const
    {
        return graphics_pipelines;
    }

    void VulkanCommandList::retain_binding_set(const RHIBindingSetRef& binding_set)
    {
        if (binding_set && std::find(binding_sets.begin(), binding_sets.end(), binding_set) == binding_sets.end())
        {
            binding_sets.push_back(binding_set);
        }
    }

    const std::vector<RHIBindingSetRef>& VulkanCommandList::retained_binding_sets() const
    {
        return binding_sets;
    }

    void VulkanCommandList::retain_render_pass_resources(std::shared_ptr<VulkanRenderPassResources> resources)
    {
        if (resources)
        {
            render_pass_resources.push_back(std::move(resources));
        }
    }

    const std::vector<std::shared_ptr<VulkanRenderPassResources>>&
        VulkanCommandList::retained_render_pass_resources() const
    {
        return render_pass_resources;
    }

    RHIStatus VulkanCommandList::begin_recording_by_context()
    {
        return mark_recording();
    }

    RHIStatus VulkanCommandList::close_by_context()
    {
        return mark_closed();
    }

    RHIStatus VulkanCommandList::mark_submitted_by_viewport()
    {
        return mark_submitted();
    }

    VulkanGraphicsCommandContext::VulkanGraphicsCommandContext(
        VulkanDevice& device,
        VulkanViewportContext& viewport,
        VkCommandPool command_pool,
        std::uint64_t frame_id)
        : vulkan_device(device)
        , viewport_context(viewport)
        , vk_command_pool(command_pool)
        , recording_frame_id(frame_id)
    {
    }

    RHIStatus VulkanGraphicsCommandContext::begin_recording(const std::string& debug_name)
    {
        if (recording_command_list)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "A Vulkan graphics command context can record only one command list.");
        }
        if (vk_command_pool == VK_NULL_HANDLE)
        {
            return RHIStatus::failure(RHIErrorCode::BackendFailure, "Vulkan command context has no command pool.");
        }

        VkCommandBufferAllocateInfo allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate_info.commandPool = vk_command_pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = 1;
        RHIStatus status = make_vulkan_status(
            vkAllocateCommandBuffers(vulkan_device.device(), &allocate_info, &vk_command_buffer),
            "vkAllocateCommandBuffers");
        if (!status)
        {
            return status;
        }

        VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        status = make_vulkan_status(vkBeginCommandBuffer(vk_command_buffer, &begin_info), "vkBeginCommandBuffer");
        if (!status)
        {
            return status;
        }

        auto command_list = std::make_shared<VulkanCommandList>(
            viewport_context,
            vk_command_buffer,
            recording_frame_id,
            debug_name);
        status = command_list->begin_recording_by_context();
        if (!status)
        {
            return status;
        }
        recording_command_list = std::move(command_list);
        graphics_state.reset();
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::transition_resources(
        const std::vector<RHIResourceTransition>& transitions)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        for (const RHIResourceTransition& transition : transitions)
        {
            const RHIStatus validation = validate_resource_transition(transition);
            if (!validation)
            {
                return validation;
            }

            VulkanAccessState before_state;
            VulkanAccessState after_state;
            RHIStatus state_status = get_vulkan_access_state(transition.before, before_state);
            if (!state_status)
            {
                return state_status;
            }
            state_status = get_vulkan_access_state(transition.after, after_state);
            if (!state_status)
            {
                return state_status;
            }

            const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(transition.resource);
            if (buffer)
            {
                if (!before_state.supports_buffer || !after_state.supports_buffer)
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan buffer transition uses an image-only access state.");
                }
                if (transition.subresources.aspect != RHITextureAspect::Color ||
                    transition.subresources.first_mip != 0 || transition.subresources.first_layer != 0 ||
                    transition.subresources.mip_count != RHI_ALL_MIPS ||
                    transition.subresources.layer_count != RHI_ALL_LAYERS)
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan buffer transition must use the default full-resource subresource range.");
                }
                if (buffer->current_access() != transition.before)
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan buffer transition before access does not match the tracked resource state.");
                }
                VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                barrier.srcAccessMask = before_state.access_mask;
                barrier.dstAccessMask = after_state.access_mask;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = buffer->buffer();
                barrier.offset = 0;
                barrier.size = VK_WHOLE_SIZE;
                vkCmdPipelineBarrier(
                    vk_command_buffer,
                    before_state.pipeline_stage,
                    after_state.pipeline_stage,
                    0,
                    0,
                    nullptr,
                    1,
                    &barrier,
                    0,
                    nullptr);
                buffer->set_current_access(transition.after);
                recording_command_list->retain_resource(transition.resource);
                continue;
            }

            const auto texture = std::dynamic_pointer_cast<VulkanTexture>(transition.resource);
            if (!texture)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan transition requires a resource created by the canonical Vulkan device.");
            }
            if (!before_state.supports_image || !after_state.supports_image)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan texture transition uses a buffer-only access state.");
            }
            if (!is_full_texture_range(texture->desc(), transition.subresources))
            {
                return RHIStatus::failure(
                    RHIErrorCode::Unsupported,
                    "Canonical Vulkan transitions currently require the full texture subresource range.");
            }
            if (texture->current_access() != transition.before)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan texture transition before access does not match the tracked resource state.");
            }
            const VkFormat format = vulkan_format_from_rhi(texture->desc().format);
            const auto aspect = to_vk_image_aspect(transition.subresources.aspect, format);
            if (!aspect)
            {
                return RHIStatus::failure(aspect.status().code(), aspect.status().message());
            }
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = texture->image_layout();
            barrier.newLayout = after_state.image_layout;
            barrier.srcAccessMask = texture->has_undefined_initial_layout() ? 0 : before_state.access_mask;
            barrier.dstAccessMask = after_state.access_mask;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture->image();
            barrier.subresourceRange.aspectMask = aspect.value();
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = texture->desc().mip_levels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = texture->desc().array_layers;
            vkCmdPipelineBarrier(
                vk_command_buffer,
                texture->has_undefined_initial_layout() ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : before_state.pipeline_stage,
                after_state.pipeline_stage,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);
            texture->set_state(after_state.image_layout, transition.after);
            recording_command_list->retain_resource(transition.resource);
        }
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::copy_buffer(const RHIBufferCopyDesc& desc)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        const RHIStatus validation = validate_buffer_copy_desc(desc);
        if (!validation)
        {
            return validation;
        }
        const auto source = std::dynamic_pointer_cast<VulkanBuffer>(desc.source);
        const auto destination = std::dynamic_pointer_cast<VulkanBuffer>(desc.destination);
        if (!source || !destination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer copy requires canonical Vulkan source and destination buffers.");
        }
        if (source->current_access() != RHIAccess::CopySource ||
            destination->current_access() != RHIAccess::CopyDestination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer copy requires CopySource and CopyDestination resource states.");
        }
        VkBufferCopy region{};
        region.srcOffset = desc.source_offset;
        region.dstOffset = desc.destination_offset;
        region.size = desc.size;
        vkCmdCopyBuffer(vk_command_buffer, source->buffer(), destination->buffer(), 1, &region);
        recording_command_list->retain_resource(desc.source);
        recording_command_list->retain_resource(desc.destination);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::upload_buffer(const RHIBufferUploadDesc& desc)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        const RHIStatus validation = validate_buffer_upload_desc(desc);
        if (!validation)
        {
            return validation;
        }
        const auto destination = std::dynamic_pointer_cast<VulkanBuffer>(desc.destination);
        if (!destination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer upload requires a buffer created by the canonical Vulkan device.");
        }
        if (destination->current_access() != RHIAccess::CopyDestination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer upload requires the destination buffer in CopyDestination state.");
        }
        const auto staging_buffer = create_vulkan_staging_buffer(
            vulkan_device.physical_device(),
            vulkan_device.device(),
            desc.source.data,
            desc.source.size);
        if (!staging_buffer)
        {
            return RHIStatus::failure(staging_buffer.status().code(), staging_buffer.status().message());
        }
        record_staging_buffer_barrier(vk_command_buffer, staging_buffer.value()->buffer());
        VkBufferCopy region{};
        region.dstOffset = desc.destination_offset;
        region.size = desc.source.size;
        vkCmdCopyBuffer(vk_command_buffer, staging_buffer.value()->buffer(), destination->buffer(), 1, &region);
        recording_command_list->retain_resource(desc.destination);
        recording_command_list->retain_staging_buffer(staging_buffer.value());
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::copy_texture(const RHITextureCopyDesc& desc)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        const RHIStatus validation = validate_texture_copy_desc(desc);
        if (!validation)
        {
            return validation;
        }
        const auto source = std::dynamic_pointer_cast<VulkanTexture>(desc.source.texture);
        const auto destination = std::dynamic_pointer_cast<VulkanTexture>(desc.destination.texture);
        if (!source || !destination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture copy requires canonical Vulkan source and destination textures.");
        }
        if (source->desc().format != destination->desc().format)
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "Canonical Vulkan texture copy currently requires identical source and destination formats.");
        }
        if (source->current_access() != RHIAccess::CopySource ||
            destination->current_access() != RHIAccess::CopyDestination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture copy requires CopySource and CopyDestination resource states.");
        }
        const VkFormat format = vulkan_format_from_rhi(source->desc().format);
        if (is_vk_depth_format(format))
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "Canonical Vulkan texture copy currently supports color textures only.");
        }
        VkImageCopy region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = desc.source.mip;
        region.srcSubresource.baseArrayLayer = desc.source.layer;
        region.srcSubresource.layerCount = 1;
        region.srcOffset = {
            static_cast<std::int32_t>(desc.source.offset.x),
            static_cast<std::int32_t>(desc.source.offset.y),
            static_cast<std::int32_t>(desc.source.offset.z)};
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = desc.destination.mip;
        region.dstSubresource.baseArrayLayer = desc.destination.layer;
        region.dstSubresource.layerCount = 1;
        region.dstOffset = {
            static_cast<std::int32_t>(desc.destination.offset.x),
            static_cast<std::int32_t>(desc.destination.offset.y),
            static_cast<std::int32_t>(desc.destination.offset.z)};
        region.extent = {desc.extent.width, desc.extent.height, desc.extent.depth};
        vkCmdCopyImage(
            vk_command_buffer,
            source->image(),
            source->image_layout(),
            destination->image(),
            destination->image_layout(),
            1,
            &region);
        recording_command_list->retain_resource(desc.source.texture);
        recording_command_list->retain_resource(desc.destination.texture);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::upload_texture(const RHITextureUploadDesc& desc)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        const RHIStatus validation = validate_texture_upload_desc(desc);
        if (!validation)
        {
            return validation;
        }
        const auto destination = std::dynamic_pointer_cast<VulkanTexture>(desc.destination.texture);
        if (!destination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture upload requires a texture created by the canonical Vulkan device.");
        }
        if (destination->current_access() != RHIAccess::CopyDestination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture upload requires the destination texture in CopyDestination state.");
        }
        if (destination->desc().sample_count != 1 || is_vk_depth_format(vulkan_format_from_rhi(destination->desc().format)))
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "Canonical Vulkan texture upload currently supports only single-sample color textures.");
        }
        const auto bytes_per_texel = color_format_bytes_per_texel(destination->desc().format);
        if (!bytes_per_texel)
        {
            return RHIStatus::failure(bytes_per_texel.status().code(), bytes_per_texel.status().message());
        }
        const std::size_t minimum_row_pitch = static_cast<std::size_t>(desc.extent.width) * bytes_per_texel.value();
        if (desc.source.row_pitch < minimum_row_pitch || desc.source.row_pitch % bytes_per_texel.value() != 0 ||
            desc.source.slice_pitch < desc.source.row_pitch * static_cast<std::size_t>(desc.extent.height) ||
            desc.source.slice_pitch % desc.source.row_pitch != 0 ||
            desc.source.size < desc.source.slice_pitch * static_cast<std::size_t>(desc.extent.depth))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture upload source pitches or data size do not cover the requested region.");
        }
        const auto staging_buffer = create_vulkan_staging_buffer(
            vulkan_device.physical_device(),
            vulkan_device.device(),
            desc.source.data,
            desc.source.size);
        if (!staging_buffer)
        {
            return RHIStatus::failure(staging_buffer.status().code(), staging_buffer.status().message());
        }
        record_staging_buffer_barrier(vk_command_buffer, staging_buffer.value()->buffer());
        VkBufferImageCopy region{};
        region.bufferRowLength = static_cast<std::uint32_t>(desc.source.row_pitch / bytes_per_texel.value());
        region.bufferImageHeight = static_cast<std::uint32_t>(desc.source.slice_pitch / desc.source.row_pitch);
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = desc.destination.mip;
        region.imageSubresource.baseArrayLayer = desc.destination.layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {
            static_cast<std::int32_t>(desc.destination.offset.x),
            static_cast<std::int32_t>(desc.destination.offset.y),
            static_cast<std::int32_t>(desc.destination.offset.z)};
        region.imageExtent = {desc.extent.width, desc.extent.height, desc.extent.depth};
        vkCmdCopyBufferToImage(
            vk_command_buffer,
            staging_buffer.value()->buffer(),
            destination->image(),
            destination->image_layout(),
            1,
            &region);
        recording_command_list->retain_resource(desc.destination.texture);
        recording_command_list->retain_staging_buffer(staging_buffer.value());
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::write_gpu_fence(const RHIGPUFenceRef&)
    {
        return unsupported_while_recording("Vulkan GPU fence writes are not implemented yet.");
    }

    RHIResult<RHICommandListRef> VulkanGraphicsCommandContext::finish_recording()
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return RHIResult<RHICommandListRef>::failure(status.code(), status.message());
        }
        if (active_render_pass)
        {
            return RHIResult<RHICommandListRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics command recording cannot finish with an active render pass.");
        }

        const RHIStatus end_status = make_vulkan_status(vkEndCommandBuffer(vk_command_buffer), "vkEndCommandBuffer");
        if (!end_status)
        {
            return RHIResult<RHICommandListRef>::failure(end_status.code(), end_status.message());
        }
        const RHIStatus close_status = recording_command_list->close_by_context();
        if (!close_status)
        {
            return RHIResult<RHICommandListRef>::failure(close_status.code(), close_status.message());
        }
        return RHIResult<RHICommandListRef>::success(recording_command_list);
    }

    RHIStatus VulkanGraphicsCommandContext::begin_render_pass(const RHIRenderPassDesc& desc)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        if (active_render_pass)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "A Vulkan render pass is already active.");
        }
        const RHIStatus validation = validate_render_pass_desc(desc);
        if (!validation)
        {
            return validation;
        }
        if (desc.has_depth_stencil_attachment || desc.color_attachments.empty())
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "Canonical Vulkan render passes currently support one or more color attachments without depth/stencil.");
        }

        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> attachment_references;
        std::vector<VkImageView> image_views;
        std::vector<VkClearValue> clear_values;
        std::vector<RHIFormat> color_formats;
        attachments.reserve(desc.color_attachments.size());
        attachment_references.reserve(desc.color_attachments.size());
        image_views.reserve(desc.color_attachments.size());
        clear_values.reserve(desc.color_attachments.size());
        color_formats.reserve(desc.color_attachments.size());
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        for (std::uint32_t index = 0; index < desc.color_attachments.size(); ++index)
        {
            const RHIColorAttachmentDesc& attachment = desc.color_attachments[index];
            if (attachment.resolve_view)
            {
                return RHIStatus::failure(
                    RHIErrorCode::Unsupported,
                    "Canonical Vulkan render-pass resolve attachments are not implemented yet.");
            }
            const auto view = std::dynamic_pointer_cast<VulkanTextureView>(attachment.view);
            const auto texture = view ? std::dynamic_pointer_cast<VulkanTexture>(view->texture()) : nullptr;
            if (!view || !texture)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan render pass requires canonical Vulkan color attachment views.");
            }
            if (texture->current_access() != RHIAccess::RenderTarget)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan color attachment must be transitioned to RenderTarget before begin_render_pass.");
            }
            const auto load_operation = to_vk_load_operation(attachment.load);
            const auto store_operation = to_vk_store_operation(attachment.store);
            if (!load_operation || !store_operation)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan render pass received an invalid attachment load or store operation.");
            }
            const std::uint32_t mip = attachment.view->desc().subresources.first_mip;
            const std::uint32_t attachment_width = std::max(1U, texture->desc().width >> mip);
            const std::uint32_t attachment_height = std::max(1U, texture->desc().height >> mip);
            if ((width != 0 && (width != attachment_width || height != attachment_height)))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Vulkan render-pass attachment extents do not match.");
            }
            width = attachment_width;
            height = attachment_height;

            VkAttachmentDescription vk_attachment{};
            vk_attachment.format = vulkan_format_from_rhi(texture->desc().format);
            vk_attachment.samples = static_cast<VkSampleCountFlagBits>(texture->desc().sample_count);
            vk_attachment.loadOp = load_operation.value();
            vk_attachment.storeOp = store_operation.value();
            vk_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            vk_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            vk_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            vk_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments.push_back(vk_attachment);
            color_formats.push_back(texture->desc().format);
            attachment_references.push_back({index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            image_views.push_back(view->image_view());

            VkClearValue clear_value{};
            if (attachment.load == RHILoadOperation::Clear)
            {
                const vec4& color = attachment.clear_value.get_clear_color();
                clear_value.color.float32[0] = color.x;
                clear_value.color.float32[1] = color.y;
                clear_value.color.float32[2] = color.z;
                clear_value.color.float32[3] = color.w;
            }
            clear_values.push_back(clear_value);
            recording_command_list->retain_resource(texture);
            recording_command_list->retain_texture_view(attachment.view);
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(attachment_references.size());
        subpass.pColorAttachments = attachment_references.data();
        VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        render_pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        render_pass_info.pAttachments = attachments.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        VkRenderPass render_pass = VK_NULL_HANDLE;
        RHIStatus create_status = make_vulkan_status(
            vkCreateRenderPass(vulkan_device.device(), &render_pass_info, nullptr, &render_pass),
            "vkCreateRenderPass");
        if (!create_status)
        {
            return create_status;
        }

        VkFramebufferCreateInfo framebuffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = static_cast<std::uint32_t>(image_views.size());
        framebuffer_info.pAttachments = image_views.data();
        framebuffer_info.width = width;
        framebuffer_info.height = height;
        framebuffer_info.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        create_status = make_vulkan_status(
            vkCreateFramebuffer(vulkan_device.device(), &framebuffer_info, nullptr, &framebuffer),
            "vkCreateFramebuffer");
        if (!create_status)
        {
            vkDestroyRenderPass(vulkan_device.device(), render_pass, nullptr);
            return create_status;
        }
        active_render_pass = std::make_shared<VulkanRenderPassResources>(
            vulkan_device.device(),
            render_pass,
            framebuffer,
            std::move(color_formats),
            desc.color_attachments.front().view->texture()->desc().sample_count);
        VkRenderPassBeginInfo begin_info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin_info.renderPass = active_render_pass->render_pass();
        begin_info.framebuffer = active_render_pass->framebuffer();
        begin_info.renderArea.extent = {width, height};
        begin_info.clearValueCount = static_cast<std::uint32_t>(clear_values.size());
        begin_info.pClearValues = clear_values.data();
        vkCmdBeginRenderPass(vk_command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::end_render_pass()
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        if (!active_render_pass)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "No Vulkan render pass is active.");
        }
        vkCmdEndRenderPass(vk_command_buffer);
        recording_command_list->retain_render_pass_resources(std::move(active_render_pass));
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::set_graphics_pipeline(const RHIGraphicsPipelineRef& pipeline)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        if (!active_render_pass)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "A Vulkan graphics pipeline can be bound only inside an active render pass.");
        }
        const auto vulkan_pipeline = std::dynamic_pointer_cast<VulkanGraphicsPipeline>(pipeline);
        if (!vulkan_pipeline)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics command recording requires a canonical Vulkan graphics pipeline.");
        }
        if (!vulkan_pipeline->is_compatible_with(*active_render_pass))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "The Vulkan graphics pipeline is incompatible with the active render pass attachments.");
        }
        graphics_state.set_pipeline(pipeline);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::set_viewport(const RHIViewport& viewport)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
            !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
            !std::isfinite(viewport.min_depth) || !std::isfinite(viewport.max_depth) ||
            viewport.width <= 0.0F || viewport.height <= 0.0F ||
            viewport.min_depth < 0.0F || viewport.max_depth > 1.0F || viewport.min_depth > viewport.max_depth)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Vulkan viewport dimensions or depth range are invalid.");
        }
        graphics_state.set_viewport(viewport);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::set_scissor(const RHIRect& rect)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        if (rect.width == 0 || rect.height == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Vulkan scissor extent must be non-zero.");
        }
        graphics_state.set_scissor(rect);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::set_vertex_buffers(
        const std::vector<RHIVertexBufferBinding>& bindings)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        for (const RHIVertexBufferBinding& binding : bindings)
        {
            const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(binding.buffer);
            if (!buffer || !rhi_has_any_flag(buffer->desc().usage, RHIResourceUsage::VertexBuffer))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan vertex-buffer bindings require Vulkan buffers created with VertexBuffer usage.");
            }
            if (binding.offset >= buffer->desc().size || binding.stride == 0)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan vertex-buffer binding offset or stride is invalid.");
            }
        }
        graphics_state.set_vertex_buffers(bindings);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::set_index_buffer(const RHIIndexBufferBinding& binding)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(binding.buffer);
        if (!buffer || !rhi_has_any_flag(buffer->desc().usage, RHIResourceUsage::IndexBuffer))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan index-buffer binding requires a Vulkan buffer created with IndexBuffer usage.");
        }
        if (binding.offset >= buffer->desc().size)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Vulkan index-buffer offset is outside the buffer.");
        }
        graphics_state.set_index_buffer(binding);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::bind_binding_set(const RHIBindingSetRef& binding_set)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        if (!std::dynamic_pointer_cast<VulkanBindingSet>(binding_set))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan command recording requires a canonical Vulkan binding set.");
        }
        graphics_state.set_binding_set(binding_set);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::draw(const RHIDrawArgs& args)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        const RHIStatus validation = validate_draw_args(args);
        if (!validation)
        {
            return validation;
        }
        const RHIStatus flush_status = flush_graphics_state(false);
        if (!flush_status)
        {
            return flush_status;
        }
        vkCmdDraw(vk_command_buffer, args.vertex_count, args.instance_count, args.first_vertex, args.first_instance);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::draw_indexed(const RHIDrawIndexedArgs& args)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        const RHIStatus validation = validate_draw_indexed_args(args);
        if (!validation)
        {
            return validation;
        }
        const RHIStatus flush_status = flush_graphics_state(true);
        if (!flush_status)
        {
            return flush_status;
        }
        vkCmdDrawIndexed(
            vk_command_buffer,
            args.index_count,
            args.instance_count,
            args.first_index,
            args.vertex_offset,
            args.first_instance);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::flush_graphics_state(bool indexed_draw)
    {
        if (!active_render_pass)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Vulkan draw requires an active render pass.");
        }
        const auto pipeline = std::dynamic_pointer_cast<VulkanGraphicsPipeline>(graphics_state.pipeline());
        if (!pipeline || !pipeline->is_compatible_with(*active_render_pass))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan draw requires a compatible canonical Vulkan graphics pipeline.");
        }
        if (!graphics_state.has_viewport() || !graphics_state.has_scissor())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan draw requires viewport and scissor state.");
        }

        const auto& vertex_layouts = pipeline->desc().vertex_buffers;
        const auto& vertex_bindings = graphics_state.vertex_buffers();
        if (vertex_bindings.size() != vertex_layouts.size())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan vertex-buffer bindings must match the graphics pipeline layout count.");
        }
        for (std::size_t index = 0; index < vertex_bindings.size(); ++index)
        {
            if (vertex_bindings[index].stride != vertex_layouts[index].stride)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan vertex-buffer stride is incompatible with the graphics pipeline.");
            }
        }
        if (indexed_draw && !graphics_state.has_index_buffer())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Vulkan indexed draw requires an index buffer.");
        }

        std::array<bool, 5> required_groups{};
        for (const RHIBindingLayoutEntry& entry : pipeline->desc().binding_layout->desc().entries)
        {
            if (rhi_has_any_flag(entry.stages, RHIShaderStageFlags::AllGraphics))
            {
                required_groups[static_cast<std::size_t>(entry.group)] = true;
            }
        }
        std::array<std::shared_ptr<VulkanBindingSet>, 5> bound_sets{};
        for (const RHIBindingSetRef& binding_set : graphics_state.binding_sets())
        {
            const std::size_t group_index = static_cast<std::size_t>(binding_set->group());
            if (group_index < bound_sets.size() && required_groups[group_index])
            {
                const auto vulkan_set = std::dynamic_pointer_cast<VulkanBindingSet>(binding_set);
                if (!vulkan_set || binding_set->layout() != pipeline->desc().binding_layout)
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan binding set layout is incompatible with the graphics pipeline.");
                }
                for (const RHIBindingValue& value : binding_set->desc().bindings)
                {
                    if (value.buffer)
                    {
                        const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(value.buffer);
                        if (!buffer || buffer->current_access() != RHIAccess::UniformBuffer)
                        {
                            return RHIStatus::failure(
                                RHIErrorCode::InvalidArgument,
                                "Vulkan uniform buffer must be transitioned to UniformBuffer before draw.");
                        }
                    }
                    if (value.texture_view)
                    {
                        const auto texture = std::dynamic_pointer_cast<VulkanTexture>(value.texture_view->texture());
                        if (!texture || texture->current_access() != RHIAccess::ShaderResourceGraphics)
                        {
                            return RHIStatus::failure(
                                RHIErrorCode::InvalidArgument,
                                "Vulkan sampled texture must be transitioned to ShaderResourceGraphics before draw.");
                        }
                    }
                }
                bound_sets[group_index] = vulkan_set;
            }
        }
        for (std::size_t group_index = 0; group_index < required_groups.size(); ++group_index)
        {
            if (required_groups[group_index] && !bound_sets[group_index])
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan draw is missing a required graphics binding group.");
            }
        }

        const VulkanGraphicsStateDirty dirty_flags = graphics_state.dirty_flags();
        const bool pipeline_dirty = rhi_has_any_flag(dirty_flags, VulkanGraphicsStateDirty::Pipeline);
        if (pipeline_dirty)
        {
            vkCmdBindPipeline(vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline());
        }
        if (pipeline_dirty || rhi_has_any_flag(dirty_flags, VulkanGraphicsStateDirty::Bindings))
        {
            for (std::size_t group_index = 0; group_index < bound_sets.size(); ++group_index)
            {
                if (!bound_sets[group_index])
                {
                    continue;
                }
                const VkDescriptorSet descriptor_set = bound_sets[group_index]->descriptor_set();
                vkCmdBindDescriptorSets(
                    vk_command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline->pipeline_layout(),
                    static_cast<std::uint32_t>(group_index),
                    1,
                    &descriptor_set,
                    0,
                    nullptr);
                recording_command_list->retain_binding_set(bound_sets[group_index]);
            }
        }
        if (pipeline_dirty || rhi_has_any_flag(dirty_flags, VulkanGraphicsStateDirty::VertexBuffers))
        {
            for (std::size_t index = 0; index < vertex_bindings.size(); ++index)
            {
                const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(vertex_bindings[index].buffer);
                const VkBuffer vk_buffer = buffer->buffer();
                const VkDeviceSize offset = vertex_bindings[index].offset;
                vkCmdBindVertexBuffers(
                    vk_command_buffer,
                    vertex_layouts[index].binding,
                    1,
                    &vk_buffer,
                    &offset);
                recording_command_list->retain_resource(vertex_bindings[index].buffer);
            }
        }
        if (indexed_draw && (pipeline_dirty || rhi_has_any_flag(dirty_flags, VulkanGraphicsStateDirty::IndexBuffer)))
        {
            const RHIIndexBufferBinding& binding = graphics_state.index_buffer();
            const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(binding.buffer);
            const VkIndexType index_type = binding.format == RHIIndexFormat::UInt16
                ? VK_INDEX_TYPE_UINT16
                : VK_INDEX_TYPE_UINT32;
            vkCmdBindIndexBuffer(vk_command_buffer, buffer->buffer(), binding.offset, index_type);
            recording_command_list->retain_resource(binding.buffer);
        }
        if (pipeline_dirty || rhi_has_any_flag(dirty_flags, VulkanGraphicsStateDirty::Viewport))
        {
            const RHIViewport& viewport = graphics_state.viewport();
            VkViewport vk_viewport{};
            vk_viewport.x = viewport.x;
            vk_viewport.y = viewport.y;
            vk_viewport.width = viewport.width;
            vk_viewport.height = viewport.height;
            vk_viewport.minDepth = viewport.min_depth;
            vk_viewport.maxDepth = viewport.max_depth;
            vkCmdSetViewport(vk_command_buffer, 0, 1, &vk_viewport);
        }
        if (pipeline_dirty || rhi_has_any_flag(dirty_flags, VulkanGraphicsStateDirty::Scissor))
        {
            const RHIRect& scissor = graphics_state.scissor();
            VkRect2D vk_scissor{};
            vk_scissor.offset = {scissor.x, scissor.y};
            vk_scissor.extent = {scissor.width, scissor.height};
            vkCmdSetScissor(vk_command_buffer, 0, 1, &vk_scissor);
        }
        recording_command_list->retain_graphics_pipeline(graphics_state.pipeline());
        graphics_state.clear_dirty_flags(VulkanGraphicsStateDirty::All);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::require_recording() const
    {
        if (!recording_command_list || recording_command_list->state() != RHICommandListState::Recording)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics commands require an active recording command list.");
        }
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::unsupported_while_recording(const char* operation) const
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        return RHIStatus::failure(RHIErrorCode::Unsupported, operation);
    }
}
