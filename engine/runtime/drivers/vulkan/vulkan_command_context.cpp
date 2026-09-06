#include "drivers/vulkan/vulkan_command_context.h"

#include "drivers/vulkan/vulkan_binding_creation.h"
#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_type_mapping.h"
#include "drivers/vulkan/vulkan_upload_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace toy3d
{
    VulkanCommandPool::VulkanCommandPool(
        VkDevice device,
        VkCommandPool command_pool)
        : vk_device(device)
        , vk_command_pool(command_pool)
    {
    }

    VulkanCommandPool::~VulkanCommandPool()
    {
        if (vk_device != VK_NULL_HANDLE && vk_command_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(vk_device, vk_command_pool, nullptr);
        }
    }

    namespace
    {
        bool uses_constant_blend_factor(const RHIGraphicsPipelineDesc& desc)
        {
            for (std::uint32_t index = 0; index < desc.color_attachment_count; ++index)
            {
                const auto& blend = desc.color_blend_attachments[index];
                if (!blend.blend_enable)
                {
                    continue;
                }
                if (blend.source_color_factor == RHIBlendFactor::ConstantColor ||
                    blend.source_color_factor == RHIBlendFactor::OneMinusConstantColor ||
                    blend.destination_color_factor == RHIBlendFactor::ConstantColor ||
                    blend.destination_color_factor == RHIBlendFactor::OneMinusConstantColor ||
                    blend.source_alpha_factor == RHIBlendFactor::ConstantColor ||
                    blend.source_alpha_factor == RHIBlendFactor::OneMinusConstantColor ||
                    blend.destination_alpha_factor == RHIBlendFactor::ConstantColor ||
                    blend.destination_alpha_factor == RHIBlendFactor::OneMinusConstantColor)
                {
                    return true;
                }
            }
            return false;
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
            else if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            {
                code = RHIErrorCode::OutOfMemory;
            }
            return RHIStatus::failure(
                code,
                std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result)) + ".");
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

    }

    VulkanCommandList::VulkanCommandList(
        const RHIDevice& device,
        VulkanViewportContext& owner,
        VkCommandBuffer command_buffer,
        std::uint64_t frame_id,
        std::string debug_name)
        : RHICommandList(device, std::move(debug_name))
        , viewport_owner(&owner)
        , vk_command_buffer(command_buffer)
        , command_frame_id(frame_id)
    {
    }

    VulkanCommandList::VulkanCommandList(
        const RHIDevice& device,
        std::shared_ptr<VulkanCommandPool> command_pool,
        VkCommandBuffer command_buffer,
        std::string debug_name)
        : RHICommandList(device, std::move(debug_name))
        , owned_command_pool(std::move(command_pool))
        , vk_command_buffer(command_buffer)
    {
    }

    VkCommandBuffer VulkanCommandList::command_buffer() const
    {
        return vk_command_buffer;
    }

    bool VulkanCommandList::is_device_level() const
    {
        return viewport_owner == nullptr && owned_command_pool != nullptr;
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

    void VulkanCommandList::retain_upload_page(const std::shared_ptr<VulkanUploadPage>& upload_page)
    {
        if (upload_page && std::find(upload_pages.begin(), upload_pages.end(), upload_page) == upload_pages.end())
        {
            upload_pages.push_back(upload_page);
        }
    }

    const std::vector<std::shared_ptr<VulkanUploadPage>>& VulkanCommandList::retained_upload_pages() const
    {
        return upload_pages;
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

    void VulkanCommandList::retain_binding_packet(
        const std::shared_ptr<VulkanBindingPacket>& binding_packet)
    {
        if (binding_packet &&
            std::find(binding_packets.begin(), binding_packets.end(), binding_packet) ==
                binding_packets.end())
        {
            binding_packets.push_back(binding_packet);
        }
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

    RHIAccess VulkanCommandList::tracked_buffer_access(const std::shared_ptr<VulkanBuffer>& buffer) const
    {
        const auto found = buffer_states.find(buffer.get());
        return found == buffer_states.end() ? buffer->current_access() : found->second.final_access;
    }

    RHIResult<VulkanTextureSubresourceState> VulkanCommandList::tracked_texture_state(
        const std::shared_ptr<VulkanTexture>& texture,
        const RHISubresourceRange& range) const
    {
        const std::uint32_t mip_count = range.mip_count == RHI_ALL_MIPS
            ? texture->desc().mip_levels - range.first_mip
            : range.mip_count;
        const std::uint32_t layer_count = range.layer_count == RHI_ALL_LAYERS
            ? texture->desc().array_layers - range.first_layer
            : range.layer_count;
        const auto found = texture_states.find(texture.get());
        bool has_result = false;
        VulkanTextureSubresourceState result;
        const auto inspect = [&](RHITextureAspect aspect, std::uint32_t mip, std::uint32_t layer) -> bool
        {
            VulkanTextureSubresourceState state = texture->subresource_state(aspect, mip, layer);
            if (found != texture_states.end())
            {
                const auto entry = std::find_if(
                    found->second.entries.begin(), found->second.entries.end(),
                    [aspect, mip, layer](const TextureState::Entry& candidate)
                    {
                        return candidate.aspect == aspect && candidate.mip == mip && candidate.layer == layer;
                    });
                if (entry != found->second.entries.end())
                {
                    state = entry->final;
                }
            }
            if (!has_result)
            {
                result = state;
                has_result = true;
                return true;
            }
            return result.layout == state.layout && result.access == state.access;
        };
        for (std::uint32_t layer = range.first_layer; layer < range.first_layer + layer_count; ++layer)
        {
            for (std::uint32_t mip = range.first_mip; mip < range.first_mip + mip_count; ++mip)
            {
                if (!inspect(range.aspect == RHITextureAspect::DepthStencil ? RHITextureAspect::Depth : range.aspect, mip, layer) ||
                    (range.aspect == RHITextureAspect::DepthStencil &&
                     !inspect(RHITextureAspect::Stencil, mip, layer)))
                {
                    return RHIResult<VulkanTextureSubresourceState>::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan texture subresource range has mixed tracked states.");
                }
            }
        }
        return RHIResult<VulkanTextureSubresourceState>::success(result);
    }

    bool VulkanCommandList::try_get_tracked_texture_state(
        const std::shared_ptr<VulkanTexture>& texture,
        VkImageLayout& layout,
        RHIAccess& access) const
    {
        if (texture_states.find(texture.get()) == texture_states.end())
        {
            return false;
        }
        RHISubresourceRange range;
        const auto state = tracked_texture_state(texture, range);
        if (!state)
        {
            return false;
        }
        layout = state.value().layout;
        access = state.value().access;
        return true;
    }

    void VulkanCommandList::track_buffer_transition(
        const std::shared_ptr<VulkanBuffer>& buffer,
        RHIAccess access)
    {
        const auto found = buffer_states.find(buffer.get());
        if (found == buffer_states.end())
        {
            buffer_states.emplace(buffer.get(), BufferState{buffer, buffer->current_access(), access});
            return;
        }
        found->second.final_access = access;
    }

    void VulkanCommandList::track_texture_transition(
        const std::shared_ptr<VulkanTexture>& texture,
        const RHISubresourceRange& range,
        VkImageLayout layout,
        RHIAccess access)
    {
        auto insertion = texture_states.emplace(texture.get(), TextureState{});
        TextureState& tracked = insertion.first->second;
        tracked.resource = texture;
        const std::uint32_t mip_count = range.mip_count == RHI_ALL_MIPS
            ? texture->desc().mip_levels - range.first_mip : range.mip_count;
        const std::uint32_t layer_count = range.layer_count == RHI_ALL_LAYERS
            ? texture->desc().array_layers - range.first_layer : range.layer_count;
        const auto update = [&](RHITextureAspect aspect, std::uint32_t mip, std::uint32_t layer)
        {
            const auto entry = std::find_if(
                tracked.entries.begin(), tracked.entries.end(),
                [aspect, mip, layer](const TextureState::Entry& candidate)
                {
                    return candidate.aspect == aspect && candidate.mip == mip && candidate.layer == layer;
                });
            if (entry == tracked.entries.end())
            {
                const VulkanTextureSubresourceState initial = texture->subresource_state(aspect, mip, layer);
                tracked.entries.push_back({aspect, mip, layer, initial, {layout, access}});
            }
            else
            {
                entry->final = {layout, access};
            }
        };
        for (std::uint32_t layer = range.first_layer; layer < range.first_layer + layer_count; ++layer)
        {
            for (std::uint32_t mip = range.first_mip; mip < range.first_mip + mip_count; ++mip)
            {
                update(range.aspect == RHITextureAspect::DepthStencil ? RHITextureAspect::Depth : range.aspect, mip, layer);
                if (range.aspect == RHITextureAspect::DepthStencil)
                {
                    update(RHITextureAspect::Stencil, mip, layer);
                }
            }
        }
    }

    RHIStatus VulkanCommandList::validate_committed_resource_states() const
    {
        for (const auto& entry : buffer_states)
        {
            const BufferState& state = entry.second;
            if (state.resource->current_access() != state.initial_access)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan command list buffer initial state no longer matches the committed submit-order state.");
            }
        }
        for (const auto& entry : texture_states)
        {
            const TextureState& state = entry.second;
            const bool mismatch = std::any_of(
                state.entries.begin(), state.entries.end(),
                [&state](const TextureState::Entry& subresource)
                {
                    const VulkanTextureSubresourceState committed = state.resource->subresource_state(
                        subresource.aspect, subresource.mip, subresource.layer);
                    return committed.layout != subresource.initial.layout ||
                        committed.access != subresource.initial.access;
                });
            if (mismatch)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan command list texture initial state no longer matches the committed submit-order state.");
            }
        }
        return RHIStatus::success();
    }

    bool VulkanCommandList::has_state_overlap(const VulkanCommandList& other) const
    {
        const auto other_retains = [&other](const RHIResource* resource)
        {
            return std::any_of(
                other.resources.begin(),
                other.resources.end(),
                [resource](const RHIResourceRef& retained) { return retained.get() == resource; });
        };
        for (const auto& entry : buffer_states)
        {
            if (other.buffer_states.find(entry.first) != other.buffer_states.end() || other_retains(entry.first))
            {
                return true;
            }
        }
        for (const auto& entry : texture_states)
        {
            if (other.texture_states.find(entry.first) != other.texture_states.end() || other_retains(entry.first))
            {
                return true;
            }
        }
        for (const RHIResourceRef& retained : resources)
        {
            const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(retained);
            const auto texture = std::dynamic_pointer_cast<VulkanTexture>(retained);
            if ((buffer && other.buffer_states.find(buffer.get()) != other.buffer_states.end()) ||
                (texture && other.texture_states.find(texture.get()) != other.texture_states.end()))
            {
                return true;
            }
        }
        return false;
    }

    void VulkanCommandList::commit_resource_states() const
    {
        for (const auto& entry : buffer_states)
        {
            entry.second.resource->set_current_access(entry.second.final_access);
        }
        for (const auto& entry : texture_states)
        {
            const TextureState& state = entry.second;
            for (const TextureState::Entry& subresource : state.entries)
            {
                state.resource->set_subresource_state(
                    subresource.aspect, subresource.mip, subresource.layer, subresource.final);
            }
        }
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
        const RHIDevice& owner,
        VkDevice device,
        VulkanUploadManager& upload_manager,
        VulkanViewportContext& viewport,
        VkCommandPool command_pool,
        std::uint64_t frame_id)
        : RHIGraphicsCommandContext(owner)
        , owner_device(owner)
        , vk_device(device)
        , upload_manager(upload_manager)
        , viewport_context(&viewport)
        , vk_command_pool(command_pool)
        , recording_frame_id(frame_id)
    {
    }

    VulkanGraphicsCommandContext::VulkanGraphicsCommandContext(
        const RHIDevice& owner,
        VkDevice device,
        VulkanUploadManager& upload_manager,
        std::shared_ptr<VulkanCommandPool> command_pool)
        : RHIGraphicsCommandContext(owner)
        , owner_device(owner)
        , vk_device(device)
        , upload_manager(upload_manager)
        , owned_command_pool(std::move(command_pool))
        , vk_command_pool(
            owned_command_pool ? owned_command_pool->handle() : VK_NULL_HANDLE)
    {
    }

    RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>
        create_vulkan_graphics_command_context(
            const RHIDevice& owner,
            VkDevice device,
            std::uint32_t graphics_queue_family,
            VulkanUploadManager& upload_manager)
    {
        if (device == VK_NULL_HANDLE ||
            graphics_queue_family == VK_QUEUE_FAMILY_IGNORED)
        {
            return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::failure(
                RHIErrorCode::NotReady,
                "Vulkan device-level command context requires valid native device state.");
        }

        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = graphics_queue_family;
        VkCommandPool command_pool = VK_NULL_HANDLE;
        const RHIStatus status = make_vulkan_status(
            vkCreateCommandPool(device, &pool_info, nullptr, &command_pool),
            "vkCreateCommandPool");
        if (!status)
        {
            return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::failure(
                status.code(), status.message());
        }

        auto owned_pool = std::make_shared<VulkanCommandPool>(device, command_pool);
        return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::success(
            std::make_unique<VulkanGraphicsCommandContext>(
                owner,
                device,
                upload_manager,
                std::move(owned_pool)));
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
            vkAllocateCommandBuffers(vk_device, &allocate_info, &vk_command_buffer),
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

        std::shared_ptr<VulkanCommandList> command_list;
        if (viewport_context != nullptr)
        {
            command_list = std::make_shared<VulkanCommandList>(
                owner_device,
                *viewport_context,
                vk_command_buffer,
                recording_frame_id,
                debug_name);
        }
        else
        {
            command_list = std::make_shared<VulkanCommandList>(
                owner_device,
                owned_command_pool,
                vk_command_buffer,
                debug_name);
        }
        status = command_list->begin_recording_by_context();
        if (!status)
        {
            return status;
        }
        recording_command_list = std::move(command_list);
        graphics_state.reset();
        active_binding_packets.fill(nullptr);
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
            if (!transition.resource->is_owned_by(owner_device))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan transition cannot use a resource created by another device.");
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
                if (recording_command_list->tracked_buffer_access(buffer) != transition.before)
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
                recording_command_list->track_buffer_transition(buffer, transition.after);
                recording_command_list->retain_resource(transition.resource);
                continue;
            }

            const auto texture = std::dynamic_pointer_cast<VulkanTexture>(transition.resource);
            if (!texture)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan transition requires a resource created by the Vulkan device.");
            }
            if (!before_state.supports_image || !after_state.supports_image)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan texture transition uses a buffer-only access state.");
            }
            const bool uses_depth_stencil_access =
                transition.before == RHIAccess::DepthStencilRead ||
                transition.before == RHIAccess::DepthStencilWrite ||
                transition.after == RHIAccess::DepthStencilRead ||
                transition.after == RHIAccess::DepthStencilWrite;
            if (uses_depth_stencil_access &&
                !EnumHasAnyFlags(texture->desc().usage, RHIResourceUsage::DepthStencil))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan depth-stencil transitions require a texture created with DepthStencil usage.");
            }
            const auto tracked_state = recording_command_list->tracked_texture_state(texture, transition.subresources);
            if (!tracked_state)
            {
                return RHIStatus::failure(tracked_state.status().code(), tracked_state.status().message());
            }
            if (tracked_state.value().access != transition.before)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan texture transition before access does not match the tracked resource state.");
            }
            const VkFormat format = vulkan_format_from_pixel_format(texture->desc().format);
            const auto aspect = to_vk_image_aspect(transition.subresources.aspect, format);
            if (!aspect)
            {
                return RHIStatus::failure(aspect.status().code(), aspect.status().message());
            }
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = tracked_state.value().layout;
            barrier.newLayout = after_state.image_layout;
            const bool old_layout_is_undefined =
                tracked_state.value().layout == VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.srcAccessMask = old_layout_is_undefined ? 0 : before_state.access_mask;
            barrier.dstAccessMask = after_state.access_mask;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture->image();
            barrier.subresourceRange.aspectMask = aspect.value();
            barrier.subresourceRange.baseMipLevel = transition.subresources.first_mip;
            barrier.subresourceRange.levelCount = transition.subresources.mip_count == RHI_ALL_MIPS
                ? texture->desc().mip_levels - transition.subresources.first_mip
                : transition.subresources.mip_count;
            barrier.subresourceRange.baseArrayLayer = transition.subresources.first_layer;
            barrier.subresourceRange.layerCount = transition.subresources.layer_count == RHI_ALL_LAYERS
                ? texture->desc().array_layers - transition.subresources.first_layer
                : transition.subresources.layer_count;
            vkCmdPipelineBarrier(
                vk_command_buffer,
                old_layout_is_undefined ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : before_state.pipeline_stage,
                after_state.pipeline_stage,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);
            recording_command_list->track_texture_transition(
                texture, transition.subresources, after_state.image_layout, transition.after);
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
        if (!desc.source->is_owned_by(owner_device) ||
            !desc.destination->is_owned_by(owner_device))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer copy cannot use resources created by another device.");
        }
        const auto source = std::dynamic_pointer_cast<VulkanBuffer>(desc.source);
        const auto destination = std::dynamic_pointer_cast<VulkanBuffer>(desc.destination);
        if (!source || !destination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer copy requires Vulkan source and destination buffers.");
        }
        if (recording_command_list->tracked_buffer_access(source) != RHIAccess::CopySource ||
            recording_command_list->tracked_buffer_access(destination) != RHIAccess::CopyDestination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer copy requires CopySource and CopyDestination resource states.");
        }
        recording_command_list->track_buffer_transition(source, RHIAccess::CopySource);
        recording_command_list->track_buffer_transition(destination, RHIAccess::CopyDestination);
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
        if (!desc.destination->is_owned_by(owner_device))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer upload cannot use a resource created by another device.");
        }
        const auto destination = std::dynamic_pointer_cast<VulkanBuffer>(desc.destination);
        if (!destination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer upload requires a buffer created by the Vulkan device.");
        }
        if (recording_command_list->tracked_buffer_access(destination) != RHIAccess::CopyDestination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan buffer upload requires the destination buffer in CopyDestination state.");
        }
        recording_command_list->track_buffer_transition(destination, RHIAccess::CopyDestination);
        const auto upload = upload_manager.upload(desc.source.data, desc.source.size, 4);
        if (!upload)
        {
            return RHIStatus::failure(upload.status().code(), upload.status().message());
        }
        record_staging_buffer_barrier(vk_command_buffer, upload.value().buffer());
        VkBufferCopy region{};
        region.srcOffset = upload.value().offset;
        region.dstOffset = desc.destination_offset;
        region.size = desc.source.size;
        vkCmdCopyBuffer(vk_command_buffer, upload.value().buffer(), destination->buffer(), 1, &region);
        recording_command_list->retain_resource(desc.destination);
        recording_command_list->retain_upload_page(upload.value().page);
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
        if (!desc.source.texture->is_owned_by(owner_device) ||
            !desc.destination.texture->is_owned_by(owner_device))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture copy cannot use resources created by another device.");
        }
        const auto source = std::dynamic_pointer_cast<VulkanTexture>(desc.source.texture);
        const auto destination = std::dynamic_pointer_cast<VulkanTexture>(desc.destination.texture);
        if (!source || !destination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture copy requires Vulkan source and destination textures.");
        }
        if (source->desc().format != destination->desc().format)
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "Vulkan texture copy currently requires identical source and destination formats.");
        }
        const RHISubresourceRange source_range{
            RHITextureAspect::Color, desc.source.mip, 1, desc.source.layer, 1};
        const RHISubresourceRange destination_range{
            RHITextureAspect::Color, desc.destination.mip, 1, desc.destination.layer, 1};
        const auto source_state = recording_command_list->tracked_texture_state(source, source_range);
        const auto destination_state = recording_command_list->tracked_texture_state(destination, destination_range);
        if (!source_state || !destination_state ||
            source_state.value().access != RHIAccess::CopySource ||
            destination_state.value().access != RHIAccess::CopyDestination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture copy requires CopySource and CopyDestination resource states.");
        }
        recording_command_list->track_texture_transition(
            source, source_range, source_state.value().layout, source_state.value().access);
        recording_command_list->track_texture_transition(
            destination,
            destination_range,
            destination_state.value().layout,
            destination_state.value().access);
        const VkFormat format = vulkan_format_from_pixel_format(source->desc().format);
        if (is_vk_depth_format(format))
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "Vulkan texture copy currently supports color textures only.");
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
            source_state.value().layout,
            destination->image(),
            destination_state.value().layout,
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
        if (!desc.destination.texture->is_owned_by(owner_device))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture upload cannot use a resource created by another device.");
        }
        const auto destination = std::dynamic_pointer_cast<VulkanTexture>(desc.destination.texture);
        if (!destination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture upload requires a texture created by the Vulkan device.");
        }
        const RHISubresourceRange destination_range{
            RHITextureAspect::Color, desc.destination.mip, 1, desc.destination.layer, 1};
        const auto destination_state = recording_command_list->tracked_texture_state(destination, destination_range);
        if (!destination_state || destination_state.value().access != RHIAccess::CopyDestination)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture upload requires the destination texture in CopyDestination state.");
        }
        recording_command_list->track_texture_transition(
            destination,
            destination_range,
            destination_state.value().layout,
            destination_state.value().access);
        if (destination->desc().sample_count != 1 || is_vk_depth_format(vulkan_format_from_pixel_format(destination->desc().format)))
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "Vulkan texture upload currently supports only single-sample color textures.");
        }
        const std::uint32_t block_width = pixel_format_block_width(destination->desc().format);
        const std::uint32_t block_height = pixel_format_block_height(destination->desc().format);
        const std::uint32_t bytes_per_block = pixel_format_bytes_per_block(destination->desc().format);
        if (block_width == 0 || block_height == 0 || bytes_per_block == 0)
        {
            return RHIStatus::failure(
                RHIErrorCode::Unsupported,
                "Vulkan texture upload does not support the requested pixel format.");
        }
        const std::uint64_t row_length =
            static_cast<std::uint64_t>(desc.source.row_pitch / bytes_per_block) * block_width;
        const std::uint64_t image_height =
            static_cast<std::uint64_t>(desc.source.slice_pitch / desc.source.row_pitch) * block_height;
        if (row_length > std::numeric_limits<std::uint32_t>::max() ||
            image_height > std::numeric_limits<std::uint32_t>::max())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan texture upload block pitches exceed native limits.");
        }
        const auto upload = upload_manager.upload(
            desc.source.data,
            desc.source.size,
            std::max<VkDeviceSize>(4, bytes_per_block));
        if (!upload)
        {
            return RHIStatus::failure(upload.status().code(), upload.status().message());
        }
        record_staging_buffer_barrier(vk_command_buffer, upload.value().buffer());
        VkBufferImageCopy region{};
        region.bufferOffset = upload.value().offset;
        region.bufferRowLength = static_cast<std::uint32_t>(row_length);
        region.bufferImageHeight = static_cast<std::uint32_t>(image_height);
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
            upload.value().buffer(),
            destination->image(),
            destination_state.value().layout,
            1,
            &region);
        recording_command_list->retain_resource(desc.destination.texture);
        recording_command_list->retain_upload_page(upload.value().page);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::write_gpu_fence(const RHIGPUFenceRef& fence)
    {
        if (fence && !fence->is_owned_by(owner_device))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan command recording cannot use a GPU fence created by another device.");
        }
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
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> color_attachment_references;
        std::vector<VkImageView> image_views;
        std::vector<VkClearValue> clear_values;
        std::vector<PixelFormat> color_formats;
        const std::size_t attachment_count = desc.color_attachments.size() +
            (desc.has_depth_stencil_attachment ? 1U : 0U);
        attachments.reserve(attachment_count);
        color_attachment_references.reserve(desc.color_attachments.size());
        image_views.reserve(attachment_count);
        clear_values.reserve(attachment_count);
        color_formats.reserve(desc.color_attachments.size());
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t sample_count = 0;
        bool depth_read_only = false;
        bool stencil_read_only = false;
        for (std::uint32_t index = 0; index < desc.color_attachments.size(); ++index)
        {
            const RHIColorAttachmentDesc& attachment = desc.color_attachments[index];
            if (attachment.resolve_view)
            {
                return RHIStatus::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan render-pass resolve attachments are not implemented yet.");
            }
            const auto view = std::dynamic_pointer_cast<VulkanTextureView>(attachment.view);
            const auto texture = view ? std::dynamic_pointer_cast<VulkanTexture>(view->texture()) : nullptr;
            if (!view || !texture || !view->is_owned_by(owner_device) ||
                !texture->is_owned_by(owner_device))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan render pass requires Vulkan color attachment views.");
            }
            const auto attachment_state = recording_command_list->tracked_texture_state(
                texture, attachment.view->desc().subresources);
            if (!attachment_state || attachment_state.value().access != RHIAccess::RenderTarget)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan color attachment must be transitioned to RenderTarget before begin_render_pass.");
            }
            recording_command_list->track_texture_transition(
                texture,
                attachment.view->desc().subresources,
                attachment_state.value().layout,
                attachment_state.value().access);
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
            sample_count = texture->desc().sample_count;

            VkAttachmentDescription vk_attachment{};
            vk_attachment.format = vulkan_format_from_pixel_format(texture->desc().format);
            vk_attachment.samples = static_cast<VkSampleCountFlagBits>(texture->desc().sample_count);
            vk_attachment.loadOp = load_operation.value();
            vk_attachment.storeOp = store_operation.value();
            vk_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            vk_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            vk_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            vk_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments.push_back(vk_attachment);
            color_formats.push_back(texture->desc().format);
            color_attachment_references.push_back({index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
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

        VkAttachmentReference depth_stencil_reference{};
        PixelFormat depth_stencil_format = PixelFormat::Unknown;
        if (desc.has_depth_stencil_attachment)
        {
            const RHIDepthStencilAttachmentDesc& attachment = desc.depth_stencil_attachment;
            const auto view = std::dynamic_pointer_cast<VulkanTextureView>(attachment.view);
            const auto texture = view ? std::dynamic_pointer_cast<VulkanTexture>(view->texture()) : nullptr;
            if (!view || !texture || !view->is_owned_by(owner_device) ||
                !texture->is_owned_by(owner_device))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan render pass requires a Vulkan depth-stencil attachment view.");
            }
            const VkFormat vk_format = vulkan_format_from_pixel_format(attachment.view->desc().format);
            if (!is_vk_depth_format(vk_format))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan depth-stencil attachment requires a depth format.");
            }
            const bool has_stencil = is_vk_stencil_format(vk_format);
            if (has_stencil && attachment.view->desc().depth_read_only !=
                attachment.view->desc().stencil_read_only)
            {
                return RHIStatus::failure(
                    RHIErrorCode::Unsupported,
                    "VulkanPortable v1 does not require separate depth and stencil layouts; "
                    "mixed read-only and writable depth-stencil aspects are unsupported by this backend path.");
            }
            const bool read_only = attachment.view->desc().depth_read_only &&
                (!has_stencil || attachment.view->desc().stencil_read_only);
            const RHIAccess required_access = read_only
                ? RHIAccess::DepthStencilRead
                : RHIAccess::DepthStencilWrite;
            const auto attachment_state = recording_command_list->tracked_texture_state(
                texture, attachment.view->desc().subresources);
            if (!attachment_state || attachment_state.value().access != required_access)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    read_only
                        ? "Vulkan read-only depth-stencil attachment must be transitioned to DepthStencilRead before begin_render_pass."
                        : "Vulkan writable depth-stencil attachment must be transitioned to DepthStencilWrite before begin_render_pass.");
            }
            recording_command_list->track_texture_transition(
                texture,
                attachment.view->desc().subresources,
                attachment_state.value().layout,
                attachment_state.value().access);

            const auto depth_load = to_vk_load_operation(attachment.depth_load);
            const auto depth_store = to_vk_store_operation(attachment.depth_store);
            const auto stencil_load = to_vk_load_operation(attachment.stencil_load);
            const auto stencil_store = to_vk_store_operation(attachment.stencil_store);
            if (!depth_load || !depth_store || !stencil_load || !stencil_store)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan render pass received an invalid depth-stencil load or store operation.");
            }

            const std::uint32_t mip = attachment.view->desc().subresources.first_mip;
            const std::uint32_t attachment_width = std::max(1U, texture->desc().width >> mip);
            const std::uint32_t attachment_height = std::max(1U, texture->desc().height >> mip);
            if (width != 0 && (width != attachment_width || height != attachment_height ||
                sample_count != texture->desc().sample_count))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan depth-stencil attachment extent or sample count does not match the color attachments.");
            }
            width = attachment_width;
            height = attachment_height;
            sample_count = texture->desc().sample_count;

            const VkImageLayout layout = read_only
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            VkAttachmentDescription vk_attachment{};
            vk_attachment.format = vk_format;
            vk_attachment.samples = static_cast<VkSampleCountFlagBits>(texture->desc().sample_count);
            vk_attachment.loadOp = depth_load.value();
            vk_attachment.storeOp = depth_store.value();
            vk_attachment.stencilLoadOp = has_stencil
                ? stencil_load.value()
                : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            vk_attachment.stencilStoreOp = has_stencil
                ? stencil_store.value()
                : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            vk_attachment.initialLayout = layout;
            vk_attachment.finalLayout = layout;
            depth_stencil_reference.attachment = static_cast<std::uint32_t>(attachments.size());
            depth_stencil_reference.layout = layout;
            attachments.push_back(vk_attachment);
            image_views.push_back(view->image_view());

            VkClearValue clear_value{};
            if (attachment.depth_load == RHILoadOperation::Clear ||
                (has_stencil && attachment.stencil_load == RHILoadOperation::Clear))
            {
                float depth = 1.0F;
                std::uint32_t stencil = 0;
                attachment.clear_value.get_clear_depth_stencil(depth, stencil);
                clear_value.depthStencil.depth = depth;
                clear_value.depthStencil.stencil = stencil;
            }
            clear_values.push_back(clear_value);
            depth_stencil_format = attachment.view->desc().format;
            depth_read_only = attachment.view->desc().depth_read_only;
            stencil_read_only = attachment.view->desc().stencil_read_only;
            recording_command_list->retain_resource(texture);
            recording_command_list->retain_texture_view(attachment.view);
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(color_attachment_references.size());
        subpass.pColorAttachments = color_attachment_references.empty()
            ? nullptr
            : color_attachment_references.data();
        subpass.pDepthStencilAttachment = desc.has_depth_stencil_attachment
            ? &depth_stencil_reference
            : nullptr;
        VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        render_pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        render_pass_info.pAttachments = attachments.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        VkRenderPass render_pass = VK_NULL_HANDLE;
        RHIStatus create_status = make_vulkan_status(
            vkCreateRenderPass(vk_device, &render_pass_info, nullptr, &render_pass),
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
            vkCreateFramebuffer(vk_device, &framebuffer_info, nullptr, &framebuffer),
            "vkCreateFramebuffer");
        if (!create_status)
        {
            vkDestroyRenderPass(vk_device, render_pass, nullptr);
            return create_status;
        }
        active_render_pass = std::make_shared<VulkanRenderPassResources>(
            vk_device,
            render_pass,
            framebuffer,
            std::move(color_formats),
            depth_stencil_format,
            depth_read_only,
            stencil_read_only,
            sample_count);
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
        if (!vulkan_pipeline || !vulkan_pipeline->is_owned_by(owner_device))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics command recording requires a Vulkan graphics pipeline.");
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

    RHIStatus VulkanGraphicsCommandContext::set_blend_constants(const vec4& constants)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        if (!std::isfinite(constants.x) || !std::isfinite(constants.y) ||
            !std::isfinite(constants.z) || !std::isfinite(constants.w))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan blend constants must be finite.");
        }
        graphics_state.set_blend_constants(constants);
        return RHIStatus::success();
    }

    RHIStatus VulkanGraphicsCommandContext::set_stencil_reference(std::uint8_t reference)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        graphics_state.set_stencil_reference(reference);
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
            if (!buffer || !buffer->is_owned_by(owner_device) ||
                !EnumHasAnyFlags(buffer->desc().usage, RHIResourceUsage::VertexBuffer))
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
        if (!buffer || !buffer->is_owned_by(owner_device) ||
            !EnumHasAnyFlags(buffer->desc().usage, RHIResourceUsage::IndexBuffer))
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

    RHIStatus VulkanGraphicsCommandContext::bind_graphics_bindings_impl(
        const RHIGraphicsBindings& bindings)
    {
        const RHIStatus status = require_recording();
        if (!status)
        {
            return status;
        }
        const std::array<RHIBindingSetRef, static_cast<std::size_t>(RHIBindingGroup::Max)> sets = {
            bindings.global, bindings.view, bindings.pass, bindings.material, bindings.object};
        if (std::any_of(sets.begin(), sets.end(), [this](const RHIBindingSetRef& binding_set) {
                return binding_set &&
                    (!binding_set->is_owned_by(owner_device) ||
                     !std::dynamic_pointer_cast<VulkanBindingSet>(binding_set));
            }))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan command recording requires Vulkan binding sets.");
        }
        graphics_state.set_graphics_bindings(bindings);
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
                "Vulkan draw requires a compatible Vulkan graphics pipeline.");
        }
        if (!graphics_state.has_viewport() || !graphics_state.has_scissor())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan draw requires viewport and scissor state.");
        }
        if (uses_constant_blend_factor(pipeline->desc()) && !graphics_state.has_blend_constants())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan draw requires blend constants for the active graphics pipeline.");
        }
        if (pipeline->desc().depth_stencil.stencil_test_enable &&
            !graphics_state.has_stencil_reference())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan draw requires a stencil reference for the active graphics pipeline.");
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
            const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(vertex_bindings[index].buffer);
            if (!buffer ||
                recording_command_list->tracked_buffer_access(buffer) != RHIAccess::VertexBuffer)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan draw requires vertex buffers in VertexBuffer state.");
            }
            recording_command_list->track_buffer_transition(buffer, RHIAccess::VertexBuffer);
        }
        if (indexed_draw && !graphics_state.has_index_buffer())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Vulkan indexed draw requires an index buffer.");
        }

        std::array<bool, static_cast<std::size_t>(RHIBindingGroup::Max)> required_groups{};
        for (const RHIBindingLayoutEntry& entry : pipeline->desc().binding_layout->desc().entries)
        {
            if (EnumHasAnyFlags(entry.stages, RHIShaderStageFlags::AllGraphics))
            {
                required_groups[static_cast<std::size_t>(entry.group)] = true;
            }
        }
        const RHIGraphicsBindings& bindings = graphics_state.bindings();
        const std::array<RHIBindingSetRef, static_cast<std::size_t>(RHIBindingGroup::Max)>
            logical_sets = {
                bindings.global,
                bindings.view,
                bindings.pass,
                bindings.material,
                bindings.object
            };
        std::array<std::vector<std::shared_ptr<VulkanBindingSet>>,
            VulkanBindingLayout::physical_set_count> physical_sources;
        for (std::size_t group_index = 0; group_index < logical_sets.size(); ++group_index)
        {
            if (!required_groups[group_index])
            {
                continue;
            }
            const RHIBindingSetRef& binding_set = logical_sets[group_index];
            if (!binding_set)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan draw is missing a required graphics binding group.");
            }
            const auto vulkan_set = std::dynamic_pointer_cast<VulkanBindingSet>(binding_set);
            if (!vulkan_set ||
                !vulkan_set->is_owned_by(owner_device) ||
                !(binding_set->layout()->desc() == pipeline->desc().binding_layout->desc()))
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
                    if (!buffer || recording_command_list->tracked_buffer_access(buffer) != RHIAccess::UniformBuffer)
                    {
                        return RHIStatus::failure(
                            RHIErrorCode::InvalidArgument,
                            "Vulkan uniform buffer must be transitioned to UniformBuffer before draw.");
                    }
                    recording_command_list->track_buffer_transition(buffer, RHIAccess::UniformBuffer);
                }
                if (value.texture_view)
                {
                    const auto texture = std::dynamic_pointer_cast<VulkanTexture>(value.texture_view->texture());
                    const auto texture_state = texture
                        ? recording_command_list->tracked_texture_state(
                              texture, value.texture_view->desc().subresources)
                        : RHIResult<VulkanTextureSubresourceState>::failure(
                              RHIErrorCode::InvalidArgument, "Invalid Vulkan sampled texture.");
                    if (!texture_state || texture_state.value().access != RHIAccess::ShaderResourceGraphics)
                    {
                        return RHIStatus::failure(
                            RHIErrorCode::InvalidArgument,
                            "Vulkan sampled texture must be transitioned to ShaderResourceGraphics before draw.");
                    }
                    recording_command_list->track_texture_transition(
                        texture,
                        value.texture_view->desc().subresources,
                        texture_state.value().layout,
                        texture_state.value().access);
                }
            }
            const RHIBindingGroup group = static_cast<RHIBindingGroup>(group_index);
            physical_sources[VulkanBindingLayout::physical_set(group)].push_back(vulkan_set);
        }

        const VulkanGraphicsStateDirty dirty_flags = graphics_state.dirty_flags();
        const bool pipeline_dirty = EnumHasAnyFlags(
            dirty_flags,
            VulkanGraphicsStateDirty::Pipeline);
        if (pipeline_dirty)
        {
            vkCmdBindPipeline(vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline());
        }
        if (pipeline_dirty || EnumHasAnyFlags(dirty_flags, VulkanGraphicsStateDirty::Bindings))
        {
            active_binding_packets.fill(nullptr);
            const auto layout = std::dynamic_pointer_cast<VulkanBindingLayout>(
                pipeline->desc().binding_layout);
            for (std::size_t physical_set = 0;
                 physical_set < physical_sources.size();
                 ++physical_set)
            {
                if (physical_sources[physical_set].empty())
                {
                    continue;
                }
                auto packet_result = materialize_vulkan_binding_packet(
                    owner_device,
                    vk_device,
                    layout,
                    static_cast<std::uint32_t>(physical_set),
                    physical_sources[physical_set]);
                if (!packet_result)
                {
                    return packet_result.status();
                }
                active_binding_packets[physical_set] = std::move(packet_result).value();
                const VkDescriptorSet descriptor_set =
                    active_binding_packets[physical_set]->descriptor_set();
                vkCmdBindDescriptorSets(
                    vk_command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline->pipeline_layout(),
                    static_cast<std::uint32_t>(physical_set),
                    1,
                    &descriptor_set,
                    0,
                    nullptr);
                recording_command_list->retain_binding_packet(
                    active_binding_packets[physical_set]);
                for (const std::shared_ptr<VulkanBindingSet>& logical_set :
                     physical_sources[physical_set])
                {
                    recording_command_list->retain_binding_set(logical_set);
                }
            }
        }
        if (pipeline_dirty || EnumHasAnyFlags(dirty_flags, VulkanGraphicsStateDirty::VertexBuffers))
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
        if (indexed_draw && (pipeline_dirty || EnumHasAnyFlags(dirty_flags, VulkanGraphicsStateDirty::IndexBuffer)))
        {
            const RHIIndexBufferBinding& binding = graphics_state.index_buffer();
            const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(binding.buffer);
            if (recording_command_list->tracked_buffer_access(buffer) != RHIAccess::IndexBuffer)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan indexed draw requires the index buffer in IndexBuffer state.");
            }
            recording_command_list->track_buffer_transition(buffer, RHIAccess::IndexBuffer);
            const VkIndexType index_type = binding.format == RHIIndexFormat::UInt16
                ? VK_INDEX_TYPE_UINT16
                : VK_INDEX_TYPE_UINT32;
            vkCmdBindIndexBuffer(vk_command_buffer, buffer->buffer(), binding.offset, index_type);
            recording_command_list->retain_resource(binding.buffer);
        }
        if (pipeline_dirty || EnumHasAnyFlags(dirty_flags, VulkanGraphicsStateDirty::Viewport))
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
        if (pipeline_dirty || EnumHasAnyFlags(dirty_flags, VulkanGraphicsStateDirty::Scissor))
        {
            const RHIRect& scissor = graphics_state.scissor();
            VkRect2D vk_scissor{};
            vk_scissor.offset = {scissor.x, scissor.y};
            vk_scissor.extent = {scissor.width, scissor.height};
            vkCmdSetScissor(vk_command_buffer, 0, 1, &vk_scissor);
        }
        if (graphics_state.has_blend_constants() &&
            (pipeline_dirty || EnumHasAnyFlags(dirty_flags, VulkanGraphicsStateDirty::BlendConstants)))
        {
            const vec4& constants = graphics_state.blend_constants();
            const float values[4] = {constants.x, constants.y, constants.z, constants.w};
            vkCmdSetBlendConstants(vk_command_buffer, values);
        }
        if (graphics_state.has_stencil_reference() &&
            (pipeline_dirty || EnumHasAnyFlags(dirty_flags, VulkanGraphicsStateDirty::StencilReference)))
        {
            vkCmdSetStencilReference(
                vk_command_buffer,
                VK_STENCIL_FACE_FRONT_AND_BACK,
                graphics_state.stencil_reference());
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
