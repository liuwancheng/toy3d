#include "drivers/rhi/rhi_descriptors.h"
#include "drivers/rhi/rhi_resource.h"

#include <cmath>
#include <set>
#include <tuple>

namespace toy3d
{
    namespace
    {
        enum class BindingRegisterClass : std::uint8_t
        {
            ConstantBuffer,
            ShaderResource,
            Sampler,
            UnorderedAccess
        };

        BindingRegisterClass binding_register_class(RHIResourceBindingType type)
        {
            switch (type)
            {
            case RHIResourceBindingType::UniformBuffer:
                return BindingRegisterClass::ConstantBuffer;
            case RHIResourceBindingType::SampledTexture:
            case RHIResourceBindingType::ReadOnlyBuffer:
                return BindingRegisterClass::ShaderResource;
            case RHIResourceBindingType::Sampler:
                return BindingRegisterClass::Sampler;
            case RHIResourceBindingType::StorageTexture:
            case RHIResourceBindingType::StorageBuffer:
                return BindingRegisterClass::UnorderedAccess;
            }
            return BindingRegisterClass::ShaderResource;
        }

        bool binding_ranges_overlap(
            std::uint32_t first_slot,
            std::uint32_t first_count,
            std::uint32_t second_slot,
            std::uint32_t second_count)
        {
            const std::uint64_t first_end = static_cast<std::uint64_t>(first_slot) + first_count;
            const std::uint64_t second_end = static_cast<std::uint64_t>(second_slot) + second_count;
            return first_slot < second_end && second_slot < first_end;
        }

        RHIResult<RHIResourceBindingType> binding_value_type(const RHIBindingValue& value)
        {
            const std::uint32_t populated_fields =
                (value.buffer ? 1U : 0U) +
                (value.buffer_view ? 1U : 0U) +
                (value.texture_view ? 1U : 0U) +
                (value.sampler ? 1U : 0U);
            if (populated_fields != 1)
            {
                return RHIResult<RHIResourceBindingType>::failure(
                    RHIErrorCode::InvalidArgument,
                    "A binding value must contain exactly one resource.");
            }
            if (value.buffer)
            {
                return RHIResult<RHIResourceBindingType>::success(RHIResourceBindingType::UniformBuffer);
            }
            if (value.sampler)
            {
                return RHIResult<RHIResourceBindingType>::success(RHIResourceBindingType::Sampler);
            }
            if (value.texture_view)
            {
                return RHIResult<RHIResourceBindingType>::success(
                    value.texture_view->desc().type == RHIResourceViewType::UnorderedAccess
                        ? RHIResourceBindingType::StorageTexture
                        : RHIResourceBindingType::SampledTexture);
            }
            return RHIResult<RHIResourceBindingType>::success(
                value.buffer_view->desc().type == RHIResourceViewType::UnorderedAccess
                    ? RHIResourceBindingType::StorageBuffer
                    : RHIResourceBindingType::ReadOnlyBuffer);
        }
    }

    const RHIClearValue RHIClearValue::None = RHIClearValue::none();
    const RHIClearValue RHIClearValue::Black = RHIClearValue::color_value(vec4(0.0F));
    const RHIClearValue RHIClearValue::White = RHIClearValue::color_value(vec4(1.0F));
    const RHIClearValue RHIClearValue::DepthOne = RHIClearValue::depth_stencil_value(1.0F, 0);
    const RHIClearValue RHIClearValue::DepthZero = RHIClearValue::depth_stencil_value(0.0F, 0);

    bool RHIBindingLayoutEntry::operator==(const RHIBindingLayoutEntry& other) const
    {
        return group == other.group &&
            slot == other.slot &&
            type == other.type &&
            stages == other.stages &&
            array_count == other.array_count;
    }

    bool RHIBindingLayoutDesc::operator==(const RHIBindingLayoutDesc& other) const
    {
        return entries == other.entries;
    }

    RHIStatus validate_buffer_desc(const RHIBufferDesc& desc)
    {
        if (desc.size == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer size must be greater than zero.");
        }
        if (desc.usage == RHIResourceUsage::None)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer usage must not be None.");
        }
        if (rhi_has_any_flag(
                desc.usage,
                rhi_enum_or(RHIResourceUsage::RenderTarget, RHIResourceUsage::DepthStencil)))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Buffers cannot use render-target or depth-stencil usage.");
        }
        if (desc.structure_stride != 0)
        {
            if (desc.structure_stride > desc.size || desc.size % desc.structure_stride != 0)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Structured-buffer size must be a multiple of its structure stride.");
            }
            if (!rhi_has_any_flag(
                    desc.usage,
                    rhi_enum_or(RHIResourceUsage::ShaderResource, RHIResourceUsage::UnorderedAccess)))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Structured buffers require shader-resource or unordered-access usage.");
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_buffer_initial_data(
        const RHIBufferDesc& desc,
        const RHIInitialData& initial_data)
    {
        const RHIStatus desc_status = validate_buffer_desc(desc);
        if (!desc_status)
        {
            return desc_status;
        }
        if (initial_data.data == nullptr || initial_data.size == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer initial data and size must be specified together.");
        }
        if (initial_data.size > desc.size)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer initial data exceeds the destination buffer size.");
        }
        if (initial_data.row_pitch != 0 || initial_data.slice_pitch != 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer initial data cannot specify row or slice pitch.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_desc(const RHITextureDesc& desc)
    {
        if (desc.dimension == RHIResourceDimension::Buffer)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture descriptor cannot use Buffer dimension.");
        }
        if (desc.width == 0 || desc.height == 0 || desc.depth == 0 ||
            desc.array_layers == 0 || desc.mip_levels == 0 || desc.sample_count == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture dimensions, layers, mips, and samples must be non-zero.");
        }
        if (desc.format == RHIFormat::Unknown)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture format must be specified.");
        }
        if (desc.sample_count > 1 && desc.mip_levels > 1)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Multisampled textures cannot have multiple mip levels.");
        }
        if (desc.dimension == RHIResourceDimension::Texture3D && desc.array_layers != 1)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "3D textures cannot have array layers.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_subresource_range(
        const RHITextureDesc& texture_desc,
        const RHISubresourceRange& range)
    {
        if (range.first_mip >= texture_desc.mip_levels ||
            range.first_layer >= texture_desc.array_layers)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Texture subresource range begins outside the texture.");
        }
        const std::uint32_t mip_count = range.mip_count == RHI_ALL_MIPS
            ? texture_desc.mip_levels - range.first_mip
            : range.mip_count;
        const std::uint32_t layer_count = range.layer_count == RHI_ALL_LAYERS
            ? texture_desc.array_layers - range.first_layer
            : range.layer_count;
        if (mip_count == 0 || layer_count == 0 ||
            mip_count > texture_desc.mip_levels - range.first_mip ||
            layer_count > texture_desc.array_layers - range.first_layer)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Texture subresource range extends outside the texture.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_initial_data(
        const RHITextureDesc& desc,
        const RHIInitialData& initial_data)
    {
        const RHIStatus desc_status = validate_texture_desc(desc);
        if (!desc_status)
        {
            return desc_status;
        }
        if (initial_data.data == nullptr || initial_data.size == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture initial data and size must be specified together.");
        }
        if (initial_data.row_pitch == 0 || initial_data.slice_pitch == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture initial data requires row and slice pitch.");
        }
        if (initial_data.slice_pitch > initial_data.size)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture slice pitch exceeds the provided data size.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_view_desc(
        const RHITextureDesc& texture_desc,
        const RHITextureViewDesc& view_desc)
    {
        const RHIStatus texture_status = validate_texture_desc(texture_desc);
        if (!texture_status)
        {
            return texture_status;
        }
        if (view_desc.format == RHIFormat::Unknown)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture view format must be specified.");
        }
        const RHIStatus range_status = validate_texture_subresource_range(
            texture_desc, view_desc.subresources);
        if (!range_status)
        {
            return range_status;
        }
        const RHIResourceUsage required_usage =
            view_desc.type == RHIResourceViewType::ShaderResource ? RHIResourceUsage::ShaderResource :
            view_desc.type == RHIResourceViewType::UnorderedAccess ? RHIResourceUsage::UnorderedAccess :
            view_desc.type == RHIResourceViewType::RenderTarget ? RHIResourceUsage::RenderTarget :
            RHIResourceUsage::DepthStencil;
        if (!rhi_has_any_flag(texture_desc.usage, required_usage))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture was not created for the requested view type.");
        }
        if (view_desc.type != RHIResourceViewType::DepthStencil &&
            (view_desc.depth_read_only || view_desc.stencil_read_only))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Depth/stencil read-only flags are valid only for depth-stencil views.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_buffer_view_desc(
        const RHIBufferDesc& buffer_desc,
        const RHIBufferViewDesc& view_desc)
    {
        const RHIStatus buffer_status = validate_buffer_desc(buffer_desc);
        if (!buffer_status)
        {
            return buffer_status;
        }
        if (view_desc.type != RHIResourceViewType::ShaderResource &&
            view_desc.type != RHIResourceViewType::UnorderedAccess)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffers only support shader-resource and unordered-access views.");
        }
        if (view_desc.size == 0 || view_desc.offset > buffer_desc.size ||
            view_desc.size > buffer_desc.size - view_desc.offset)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer view range is outside the buffer.");
        }
        const RHIResourceUsage required_usage =
            view_desc.type == RHIResourceViewType::ShaderResource
                ? RHIResourceUsage::ShaderResource
                : RHIResourceUsage::UnorderedAccess;
        if (!rhi_has_any_flag(buffer_desc.usage, required_usage))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer was not created for the requested view type.");
        }
        if (buffer_desc.structure_stride != 0)
        {
            if (view_desc.format != RHIFormat::Unknown ||
                view_desc.offset % buffer_desc.structure_stride != 0 ||
                view_desc.size % buffer_desc.structure_stride != 0)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Structured-buffer views require an unknown format and structure-aligned range.");
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_shader_desc(const RHIShaderDesc& desc)
    {
        if (desc.bytecode.bytes.empty())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shader bytecode cannot be empty.");
        }
        if (desc.bytecode.target.empty())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shader bytecode target must be specified.");
        }
        if (desc.entry_point.empty())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shader entry point must be specified.");
        }
        if (desc.content_hash[0] == 0 && desc.content_hash[1] == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shader content hash must be stable and non-zero.");
        }

        using BindingKey = std::tuple<RHIBindingGroup, BindingRegisterClass, std::uint32_t>;
        std::set<BindingKey> reflected_bindings;
        for (const RHIShaderBindingReflection& binding : desc.reflection)
        {
            if (binding.name.empty() || binding.array_count == 0)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shader reflection bindings require a name and non-zero array count.");
            }
            if (!reflected_bindings.emplace(
                    binding.group, binding_register_class(binding.type), binding.slot).second)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shader reflection register must be unique within its group and resource class.");
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_binding_layout_desc(const RHIBindingLayoutDesc& desc)
    {
        std::vector<RHIBindingLayoutEntry> bindings;
        for (const RHIBindingLayoutEntry& entry : desc.entries)
        {
            if (entry.array_count == 0)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Binding array count must be non-zero.");
            }
            if (entry.stages == RHIShaderStageFlags::None)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Binding must be visible to at least one shader stage.");
            }
            for (const RHIBindingLayoutEntry& existing : bindings)
            {
                if (binding_register_class(existing.type) == binding_register_class(entry.type) &&
                    rhi_has_any_flag(existing.stages, entry.stages) &&
                    binding_ranges_overlap(existing.slot, existing.array_count, entry.slot, entry.array_count))
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Binding register ranges must not overlap within the same shader stages and resource class.");
                }
            }
            bindings.push_back(entry);
        }
        return RHIStatus::success();
    }

    RHIStatus validate_sampler_desc(const RHISamplerDesc& desc)
    {
        if (!std::isfinite(desc.mip_lod_bias) || !std::isfinite(desc.min_lod) ||
            !std::isfinite(desc.max_lod) || desc.min_lod > desc.max_lod)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Sampler LOD values are invalid.");
        }
        if (desc.max_anisotropy == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Sampler anisotropy must be at least one.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_binding_set_desc(const RHIBindingSetDesc& desc)
    {
        if (!desc.layout)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Binding set requires a binding layout.");
        }

        std::set<std::tuple<BindingRegisterClass, std::uint32_t, std::uint32_t>> supplied_bindings;
        for (const RHIBindingValue& value : desc.bindings)
        {
            const auto value_type = binding_value_type(value);
            if (!value_type)
            {
                return value_type.status();
            }
            const RHIBindingLayoutEntry* matching_entry = nullptr;
            for (const RHIBindingLayoutEntry& entry : desc.layout->desc().entries)
            {
                if (entry.group == desc.group && entry.slot == value.slot && entry.type == value_type.value())
                {
                    matching_entry = &entry;
                    break;
                }
            }
            if (!matching_entry || value.array_index >= matching_entry->array_count)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Binding value does not match its layout group, slot, type, or array range.");
            }
            if (!supplied_bindings.emplace(
                    binding_register_class(value_type.value()), value.slot, value.array_index).second)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Binding set contains a duplicate value.");
            }
            if (value.buffer)
            {
                if (!rhi_has_any_flag(value.buffer->desc().usage, RHIResourceUsage::UniformBuffer) ||
                    value.buffer_offset >= value.buffer->desc().size)
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Uniform-buffer binding requires UniformBuffer usage and a valid offset.");
                }
                const std::uint64_t range = value.buffer_size == 0
                    ? value.buffer->desc().size - value.buffer_offset
                    : value.buffer_size;
                if (range == 0 || range > value.buffer->desc().size - value.buffer_offset)
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Uniform-buffer binding range is invalid.");
                }
            }
            else if ((value.buffer_offset != 0 || value.buffer_size != 0))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Buffer offset and size are valid only for direct uniform-buffer bindings.");
            }
        }

        for (const RHIBindingLayoutEntry& entry : desc.layout->desc().entries)
        {
            if (entry.group != desc.group)
            {
                continue;
            }
            for (std::uint32_t array_index = 0; array_index < entry.array_count; ++array_index)
            {
                if (supplied_bindings.find(std::make_tuple(
                        binding_register_class(entry.type), entry.slot, array_index)) == supplied_bindings.end())
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Binding set must provide every binding declared for its group.");
                }
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_graphics_pipeline_desc(const RHIGraphicsPipelineDesc& desc)
    {
        if (!desc.vertex_shader || !desc.pixel_shader || !desc.binding_layout)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Graphics pipeline requires vertex shader, pixel shader, and binding layout.");
        }
        if (desc.vertex_shader->desc().stage != RHIShaderStage::Vertex ||
            desc.pixel_shader->desc().stage != RHIShaderStage::Pixel)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Graphics pipeline shader stages do not match their roles.");
        }
        if (desc.color_attachment_count > desc.color_formats.size())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Pipeline has too many color attachments.");
        }
        if (desc.sample_count == 0 ||
            (desc.sample_count & (desc.sample_count - 1U)) != 0)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Pipeline sample count must be a non-zero power of two.");
        }
        std::set<std::uint32_t> vertex_bindings;
        for (const RHIGraphicsPipelineDesc::VertexBufferLayout& layout : desc.vertex_buffers)
        {
            if (layout.stride == 0 || !vertex_bindings.emplace(layout.binding).second)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vertex-buffer layouts require a non-zero stride and unique binding index.");
            }
        }
        std::set<std::uint32_t> attribute_locations;
        for (const RHIGraphicsPipelineDesc::VertexAttribute& attribute : desc.vertex_attributes)
        {
            if (attribute.format == RHIFormat::Unknown ||
                vertex_bindings.find(attribute.binding) == vertex_bindings.end() ||
                !attribute_locations.emplace(attribute.location).second)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vertex attributes require a format, an existing binding, and a unique location.");
            }
        }
        for (std::uint32_t index = 0; index < desc.color_attachment_count; ++index)
        {
            if (desc.color_formats[index] == RHIFormat::Unknown)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Enabled color attachment format must be specified.");
            }
        }
        const bool depth_state_enabled = desc.depth_stencil.depth_test_enable ||
            desc.depth_stencil.depth_write_enable || desc.depth_stencil.stencil_test_enable;
        if (desc.depth_stencil.depth_write_enable && !desc.depth_stencil.depth_test_enable)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Graphics pipeline depth writes require depth testing to be enabled.");
        }
        if (depth_state_enabled && desc.depth_stencil_format == RHIFormat::Unknown)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Enabled depth/stencil state requires a depth-stencil attachment format.");
        }
        return RHIStatus::success();
    }
}
