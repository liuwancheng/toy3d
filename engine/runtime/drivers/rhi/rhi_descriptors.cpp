#include "drivers/rhi/rhi_descriptors.h"
#include "drivers/rhi/rhi_resource.h"

#include <set>
#include <tuple>

namespace toy3d
{
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
        if (desc.stride > desc.size)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer stride cannot exceed buffer size.");
        }
        if (desc.cpu_access == RHICPUAccess::Read &&
            rhi_has_any_flag(desc.usage, RHIResourceUsage::RenderTarget))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "CPU-readable buffers cannot be render targets.");
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
        if (view_desc.subresources.mip_count == 0 || view_desc.subresources.layer_count == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture view subresource counts must be non-zero.");
        }
        if (view_desc.subresources.first_mip >= texture_desc.mip_levels ||
            view_desc.subresources.first_layer >= texture_desc.array_layers)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture view starts outside the texture subresources.");
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

        using BindingKey = std::tuple<RHIBindingGroup, std::uint32_t>;
        std::set<BindingKey> reflected_bindings;
        for (const RHIShaderBindingReflection& binding : desc.reflection)
        {
            if (binding.name.empty() || binding.array_count == 0)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shader reflection bindings require a name and non-zero array count.");
            }
            if (!reflected_bindings.emplace(binding.group, binding.slot).second)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shader reflection group and slot pair must be unique.");
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_binding_layout_desc(const RHIBindingLayoutDesc& desc)
    {
        using BindingKey = std::tuple<RHIBindingGroup, std::uint32_t>;
        std::set<BindingKey> bindings;
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
            if (!bindings.emplace(entry.group, entry.slot).second)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Binding group and slot pair must be unique.");
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
        if (desc.sample_count == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Pipeline sample count must be non-zero.");
        }
        for (std::uint32_t index = 0; index < desc.color_attachment_count; ++index)
        {
            if (desc.color_formats[index] == RHIFormat::Unknown)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Enabled color attachment format must be specified.");
            }
        }
        return RHIStatus::success();
    }
}
