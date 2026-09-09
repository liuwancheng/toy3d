#include "drivers/rhi/rhi_command_descriptors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        RHIResourceBindingType resolved_value_type(const RHIBindingValue& value)
        {
            if (value.buffer)
                return RHIResourceBindingType::UniformBuffer;
            if (value.sampler)
                return RHIResourceBindingType::Sampler;
            if (value.texture_view)
                return value.texture_view->desc().type == RHIResourceViewType::UnorderedAccess
                           ? RHIResourceBindingType::StorageTexture
                           : RHIResourceBindingType::SampledTexture;
            return value.buffer_view && value.buffer_view->desc().type == RHIResourceViewType::UnorderedAccess
                       ? RHIResourceBindingType::StorageBuffer
                       : RHIResourceBindingType::ReadOnlyBuffer;
        }
    } // namespace

    RHIStatus validate_resource_transition(const RHIResourceTransition& transition)
    {
        if (!transition.resource)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Resource transition requires a resource.");
        }
        if (transition.before == RHIAccess::Unknown || transition.after == RHIAccess::Unknown)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Resource transition requires explicit before and after access.");
        }
        if (transition.before == transition.after)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Resource transition cannot use identical access states.");
        }
        const auto texture = std::dynamic_pointer_cast<RHITexture>(transition.resource);
        if (texture)
        {
            return validate_texture_subresource_range(texture->desc(), transition.subresources);
        }
        if (transition.subresources.aspect != RHITextureAspect::Color || transition.subresources.first_mip != 0 ||
            transition.subresources.first_layer != 0 || transition.subresources.mip_count != RHI_ALL_MIPS ||
            transition.subresources.layer_count != RHI_ALL_LAYERS)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer transition must use the default full-resource subresource range.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_buffer_copy_desc(const RHIBufferCopyDesc& desc)
    {
        if (!desc.source || !desc.destination || desc.size == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer copy requires source, destination, and non-zero size.");
        }
        if (desc.source_offset > desc.source->desc().size ||
            desc.size > desc.source->desc().size - desc.source_offset ||
            desc.destination_offset > desc.destination->desc().size ||
            desc.size > desc.destination->desc().size - desc.destination_offset)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer copy range is outside a resource.");
        }
        if (!EnumHasAnyFlags(desc.source->desc().usage, RHIResourceUsage::CopySource) ||
            !EnumHasAnyFlags(desc.destination->desc().usage, RHIResourceUsage::CopyDestination))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer copy resources are missing copy usage flags.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_buffer_upload_desc(const RHIBufferUploadDesc& desc)
    {
        if (!desc.destination)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer upload requires a destination buffer.");
        }
        if (desc.source.data == nullptr || desc.source.size == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer upload requires non-empty source data.");
        }
        if (desc.source.row_pitch != 0 || desc.source.slice_pitch != 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer upload source data cannot specify row or slice pitch.");
        }
        const RHIBufferDesc& destination_desc = desc.destination->desc();
        if (desc.destination_offset > destination_desc.size ||
            desc.source.size > destination_desc.size - desc.destination_offset)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer upload range is outside the destination buffer.");
        }
        if (!EnumHasAnyFlags(destination_desc.usage, RHIResourceUsage::CopyDestination))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer upload destination is missing CopyDestination usage.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_copy_desc(const RHITextureCopyDesc& desc)
    {
        if (!desc.source.texture || !desc.destination.texture)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture copy requires source and destination textures.");
        }
        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture copy extent must be non-zero.");
        }
        const RHITextureDesc& source_desc = desc.source.texture->desc();
        const RHITextureDesc& destination_desc = desc.destination.texture->desc();
        if (desc.source.mip >= source_desc.mip_levels || desc.source.layer >= source_desc.array_layers ||
            desc.destination.mip >= destination_desc.mip_levels ||
            desc.destination.layer >= destination_desc.array_layers)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture copy subresource is outside a texture.");
        }
        if (!EnumHasAnyFlags(source_desc.usage, RHIResourceUsage::CopySource) ||
            !EnumHasAnyFlags(destination_desc.usage, RHIResourceUsage::CopyDestination))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture copy resources are missing copy usage flags.");
        }
        const std::uint32_t source_width = std::max(1U, source_desc.width >> desc.source.mip);
        const std::uint32_t source_height = std::max(1U, source_desc.height >> desc.source.mip);
        const std::uint32_t source_depth = std::max(1U, source_desc.depth >> desc.source.mip);
        const std::uint32_t destination_width = std::max(1U, destination_desc.width >> desc.destination.mip);
        const std::uint32_t destination_height = std::max(1U, destination_desc.height >> desc.destination.mip);
        const std::uint32_t destination_depth = std::max(1U, destination_desc.depth >> desc.destination.mip);
        if (desc.source.offset.x > source_width || desc.extent.width > source_width - desc.source.offset.x ||
            desc.source.offset.y > source_height || desc.extent.height > source_height - desc.source.offset.y ||
            desc.source.offset.z > source_depth || desc.extent.depth > source_depth - desc.source.offset.z ||
            desc.destination.offset.x > destination_width ||
            desc.extent.width > destination_width - desc.destination.offset.x ||
            desc.destination.offset.y > destination_height ||
            desc.extent.height > destination_height - desc.destination.offset.y ||
            desc.destination.offset.z > destination_depth ||
            desc.extent.depth > destination_depth - desc.destination.offset.z)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture copy region is outside a mip extent.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_upload_desc(const RHITextureUploadDesc& desc)
    {
        if (!desc.destination.texture)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture upload requires a destination texture.");
        }
        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture upload extent must be non-zero.");
        }
        if (desc.source.data == nullptr || desc.source.size == 0 || desc.source.row_pitch == 0 ||
            desc.source.slice_pitch == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture upload requires data, row pitch, and slice pitch.");
        }
        const RHITextureDesc& destination_desc = desc.destination.texture->desc();
        if (desc.destination.mip >= destination_desc.mip_levels ||
            desc.destination.layer >= destination_desc.array_layers)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture upload destination subresource is outside the texture.");
        }
        if (!EnumHasAnyFlags(destination_desc.usage, RHIResourceUsage::CopyDestination))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture upload destination is missing CopyDestination usage.");
        }
        const std::uint32_t mip_width = std::max(1U, destination_desc.width >> desc.destination.mip);
        const std::uint32_t mip_height = std::max(1U, destination_desc.height >> desc.destination.mip);
        const std::uint32_t mip_depth = std::max(1U, destination_desc.depth >> desc.destination.mip);
        if (desc.destination.offset.x > mip_width || desc.extent.width > mip_width - desc.destination.offset.x ||
            desc.destination.offset.y > mip_height || desc.extent.height > mip_height - desc.destination.offset.y ||
            desc.destination.offset.z > mip_depth || desc.extent.depth > mip_depth - desc.destination.offset.z)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture upload region is outside the destination mip extent.");
        }
        std::uint64_t minimum_row_pitch = 0;
        std::uint64_t minimum_slice_pitch = 0;
        if (!pixel_format_calculate_minimum_row_pitch(destination_desc.format, desc.extent.width, minimum_row_pitch) ||
            !pixel_format_calculate_minimum_slice_pitch(destination_desc.format, desc.extent.width, desc.extent.height,
                                                        minimum_slice_pitch))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture upload format or block-pitch calculation is invalid.");
        }
        const std::uint32_t bytes_per_block = pixel_format_bytes_per_block(destination_desc.format);
        const std::uint64_t block_row_count = minimum_slice_pitch / minimum_row_pitch;
        if (desc.source.row_pitch > std::numeric_limits<std::uint64_t>::max() / block_row_count)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture upload slice-pitch calculation overflows.");
        }
        const std::uint64_t required_slice_pitch = static_cast<std::uint64_t>(desc.source.row_pitch) * block_row_count;
        if (desc.source.row_pitch < minimum_row_pitch || desc.source.row_pitch % bytes_per_block != 0 ||
            desc.source.slice_pitch < required_slice_pitch || desc.source.slice_pitch % desc.source.row_pitch != 0 ||
            desc.extent.depth > std::numeric_limits<std::size_t>::max() / desc.source.slice_pitch ||
            desc.source.size < desc.source.slice_pitch * static_cast<std::size_t>(desc.extent.depth))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Texture upload source pitches or data size do not cover complete format blocks.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_render_pass_desc(const RHIRenderPassDesc& desc)
    {
        if (desc.color_attachments.empty() && !desc.has_depth_stencil_attachment)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Render pass requires at least one attachment.");
        }
        std::uint32_t pass_width = 0;
        std::uint32_t pass_height = 0;
        std::uint32_t pass_samples = 0;
        const auto validate_extent = [&pass_width, &pass_height, &pass_samples](const RHITextureViewRef& view) -> bool
        {
            const RHITextureDesc& texture_desc = view->texture()->desc();
            const std::uint32_t mip = view->desc().subresources.first_mip;
            const std::uint32_t width = std::max(1U, texture_desc.width >> mip);
            const std::uint32_t height = std::max(1U, texture_desc.height >> mip);
            if (pass_width == 0)
            {
                pass_width = width;
                pass_height = height;
                pass_samples = texture_desc.sample_count;
                return true;
            }
            return pass_width == width && pass_height == height && pass_samples == texture_desc.sample_count;
        };
        const auto is_single_attachment_subresource = [](const RHITextureViewRef& view) -> bool
        {
            const RHISubresourceRange& range = view->desc().subresources;
            const RHITextureDesc& texture_desc = view->texture()->desc();
            const std::uint32_t mip_count =
                range.mip_count == RHI_ALL_MIPS ? texture_desc.mip_levels - range.first_mip : range.mip_count;
            const std::uint32_t layer_count =
                range.layer_count == RHI_ALL_LAYERS ? texture_desc.array_layers - range.first_layer : range.layer_count;
            return mip_count == 1 && layer_count == 1;
        };

        for (const RHIColorAttachmentDesc& attachment : desc.color_attachments)
        {
            if (!attachment.view || attachment.view->desc().type != RHIResourceViewType::RenderTarget)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Color attachment requires a render-target view.");
            }
            if (!is_single_attachment_subresource(attachment.view))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Render-pass attachment views must select exactly one mip and one array layer.");
            }
            if (!validate_extent(attachment.view))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Render pass attachments require matching extent and sample count.");
            }
            if (attachment.resolve_view && attachment.resolve_view->desc().type != RHIResourceViewType::RenderTarget)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Resolve attachment requires a render-target view.");
            }
            if (attachment.resolve_view)
            {
                if (!is_single_attachment_subresource(attachment.resolve_view))
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Resolve attachment views must select exactly one mip and one array layer.");
                }
                const RHITextureDesc& source = attachment.view->texture()->desc();
                const RHITextureDesc& destination = attachment.resolve_view->texture()->desc();
                const std::uint32_t source_mip = attachment.view->desc().subresources.first_mip;
                const std::uint32_t destination_mip = attachment.resolve_view->desc().subresources.first_mip;
                if (source.sample_count <= 1 || destination.sample_count != 1 || source.format != destination.format ||
                    std::max(1U, source.width >> source_mip) != std::max(1U, destination.width >> destination_mip) ||
                    std::max(1U, source.height >> source_mip) != std::max(1U, destination.height >> destination_mip))
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                              "Resolve requires compatible source and destination attachments.");
                }
            }
            if (attachment.load == RHILoadOperation::Clear &&
                attachment.clear_value.type() != RHIClearValue::Type::Color)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Cleared color attachment requires a color clear value.");
            }
        }
        if (desc.has_depth_stencil_attachment)
        {
            const RHIDepthStencilAttachmentDesc& attachment = desc.depth_stencil_attachment;
            if (!attachment.view || attachment.view->desc().type != RHIResourceViewType::DepthStencil)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Depth attachment requires a depth-stencil view.");
            }
            if (!is_single_attachment_subresource(attachment.view))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Depth-stencil attachment views must select exactly one mip and one array layer.");
            }
            if (!validate_extent(attachment.view))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Depth attachment extent and sample count must match color attachments.");
            }
            if ((attachment.view->desc().depth_read_only && attachment.depth_load == RHILoadOperation::Clear) ||
                (attachment.view->desc().stencil_read_only && attachment.stencil_load == RHILoadOperation::Clear))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Read-only depth/stencil aspects cannot use a clear load operation.");
            }
            if ((attachment.depth_load == RHILoadOperation::Clear ||
                 attachment.stencil_load == RHILoadOperation::Clear) &&
                attachment.clear_value.type() != RHIClearValue::Type::DepthStencil)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Cleared depth-stencil attachment requires a depth-stencil clear value.");
            }
            if (attachment.clear_value.type() == RHIClearValue::Type::DepthStencil)
            {
                float depth = 1.0F;
                std::uint32_t stencil = 0;
                attachment.clear_value.get_clear_depth_stencil(depth, stencil);
                if (!std::isfinite(depth) || depth < 0.0F || depth > 1.0F ||
                    stencil > std::numeric_limits<std::uint8_t>::max())
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Depth-stencil clear values require depth in [0, 1] and an 8-bit stencil value.");
                }
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_graphics_bindings(const RHIGraphicsBindings& bindings)
    {
        const std::array<std::pair<RHIBindingGroup, RHIBindingSetRef>, static_cast<std::size_t>(RHIBindingGroup::Max)>
            sets = {{{RHIBindingGroup::Global, bindings.global},
                     {RHIBindingGroup::View, bindings.view},
                     {RHIBindingGroup::Pass, bindings.pass},
                     {RHIBindingGroup::Material, bindings.material},
                     {RHIBindingGroup::Object, bindings.object}}};

        for (const auto& entry : sets)
        {
            if (!entry.second)
            {
                continue;
            }
            if (entry.second->group() != entry.first)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Graphics bindings contain a binding set in the wrong logical group field.");
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_transient_uniform_data_desc(const RHITransientUniformDataDesc& desc)
    {
        const bool hash_is_zero = std::all_of(desc.data_layout_hash.begin(), desc.data_layout_hash.end(),
                                              [](std::uint8_t byte) { return byte == 0u; });
        if (desc.source.data == nullptr || desc.source.size == 0 || desc.source.row_pitch != 0 ||
            desc.source.slice_pitch != 0 || hash_is_zero || desc.shader_abi_version == 0)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Transient uniform data requires bytes, a data layout hash, and a Shader ABI version.");
        }
        return RHIStatus::success();
    }

    namespace rhi_detail
    {
        RHIResult<std::vector<ResolvedBinding>> resolve_graphics_bindings(
            const RHIGraphicsPipelineRef& pipeline, const RHIGraphicsBindings& bindings)
        {
            if (!pipeline || !pipeline->desc().binding_layout)
            {
                return RHIResult<std::vector<ResolvedBinding>>::failure(
                    RHIErrorCode::InvalidArgument, "Resolving graphics bindings requires a pipeline layout.");
            }
            const auto set_for_group = [&](RHIBindingGroup group) -> RHIBindingSetRef
            {
                switch (group)
                {
                case RHIBindingGroup::Global:
                    return bindings.global;
                case RHIBindingGroup::View:
                    return bindings.view;
                case RHIBindingGroup::Pass:
                    return bindings.pass;
                case RHIBindingGroup::Material:
                    return bindings.material;
                case RHIBindingGroup::Object:
                    return bindings.object;
                case RHIBindingGroup::Max:
                    return {};
                }
                return {};
            };

            std::vector<ResolvedBinding> resolved;
            for (const RHIBindingLayoutEntry& entry : pipeline->desc().binding_layout->desc().entries)
            {
                const RHIBindingSetRef set = set_for_group(entry.group);
                if (!set)
                {
                    return RHIResult<std::vector<ResolvedBinding>>::failure(
                        RHIErrorCode::InvalidArgument, "A required logical binding group is missing for the draw.");
                }
                if (set->owner_device() != pipeline->owner_device())
                {
                    return RHIResult<std::vector<ResolvedBinding>>::failure(
                        RHIErrorCode::InvalidArgument, "A logical binding set belongs to a different RHI device.");
                }
                for (std::uint32_t array_index = 0; array_index < entry.array_count; ++array_index)
                {
                    const auto found = std::lower_bound(
                        set->desc().bindings.begin(), set->desc().bindings.end(),
                        std::make_pair(entry.binding_id, array_index),
                        [](const RHIBindingValue& value, const std::pair<ShaderParameterId, std::uint32_t>& key)
                        {
                            return value.binding_id != key.first ? value.binding_id < key.first
                                                                 : value.array_index < key.second;
                        });
                    if (found == set->desc().bindings.end() || found->binding_id != entry.binding_id ||
                        found->array_index != array_index)
                    {
                        return RHIResult<std::vector<ResolvedBinding>>::failure(
                            RHIErrorCode::InvalidArgument, "A required active logical binding value is missing.");
                    }
                    if (resolved_value_type(*found) != entry.type)
                    {
                        return RHIResult<std::vector<ResolvedBinding>>::failure(
                            RHIErrorCode::InvalidArgument, "An active logical binding has an incompatible resource type.");
                    }
                    if (entry.type == RHIResourceBindingType::UniformBuffer)
                    {
                        const std::uint64_t range = found->buffer_size == 0
                                                        ? found->buffer->desc().size - found->buffer_offset
                                                        : found->buffer_size;
                        if (range < entry.data_size || found->data_layout_hash != entry.data_layout_hash ||
                            found->shader_abi_version != entry.shader_abi_version)
                        {
                            return RHIResult<std::vector<ResolvedBinding>>::failure(
                                RHIErrorCode::InvalidArgument,
                                "An active uniform binding is incompatible with the pipeline data ABI.");
                        }
                    }
                    resolved.push_back({entry, *found, set});
                }
            }
            return RHIResult<std::vector<ResolvedBinding>>::success(std::move(resolved));
        }
    } // namespace rhi_detail

    RHIStatus validate_draw_args(const RHIDrawArgs& args)
    {
        if (args.vertex_count == 0 || args.instance_count == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Draw counts must be non-zero.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_draw_indexed_args(const RHIDrawIndexedArgs& args)
    {
        if (args.index_count == 0 || args.instance_count == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Indexed draw counts must be non-zero.");
        }
        return RHIStatus::success();
    }
} // namespace toy3d
