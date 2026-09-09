#include "drivers/rhi/rhi_descriptors.h"
#include "drivers/rhi/rhi_resource.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

        bool binding_ranges_overlap(std::uint32_t first_slot, std::uint32_t first_count, std::uint32_t second_slot,
                                    std::uint32_t second_count)
        {
            const std::uint64_t first_end = static_cast<std::uint64_t>(first_slot) + first_count;
            const std::uint64_t second_end = static_cast<std::uint64_t>(second_slot) + second_count;
            return first_slot < second_end && second_slot < first_end;
        }

        bool is_depth_format(PixelFormat format)
        {
            return format == PixelFormat::D16UNorm || format == PixelFormat::D24UNormS8UInt ||
                   format == PixelFormat::D32Float || format == PixelFormat::D32FloatS8UInt;
        }

        bool has_stencil(PixelFormat format)
        {
            return format == PixelFormat::D24UNormS8UInt || format == PixelFormat::D32FloatS8UInt;
        }

        bool vertex_format_shape(PixelFormat format, RHIShaderVertexInputReflection::ScalarType& scalar_type,
                                 std::uint32_t& component_count, std::uint32_t& byte_size)
        {
            scalar_type = RHIShaderVertexInputReflection::ScalarType::Float32;
            switch (format)
            {
            case PixelFormat::R8UNorm:
            case PixelFormat::R8SNorm:
                component_count = 1u;
                byte_size = 1u;
                return true;
            case PixelFormat::R8G8B8A8UNorm:
            case PixelFormat::R8G8B8A8SNorm:
            case PixelFormat::R10G10B10A2UNorm:
                component_count = 4u;
                byte_size = 4u;
                return true;
            case PixelFormat::R11G11B10Float:
                component_count = 3u;
                byte_size = 4u;
                return true;
            case PixelFormat::R16Float:
                component_count = 1u;
                byte_size = 2u;
                return true;
            case PixelFormat::R16G16Float:
                component_count = 2u;
                byte_size = 4u;
                return true;
            case PixelFormat::R16G16B16A16Float:
                component_count = 4u;
                byte_size = 8u;
                return true;
            case PixelFormat::R32Float:
                component_count = 1u;
                byte_size = 4u;
                return true;
            case PixelFormat::R32G32Float:
                component_count = 2u;
                byte_size = 8u;
                return true;
            case PixelFormat::R32G32B32Float:
                component_count = 3u;
                byte_size = 12u;
                return true;
            case PixelFormat::R32G32B32A32Float:
                component_count = 4u;
                byte_size = 16u;
                return true;
            case PixelFormat::R16UInt:
                scalar_type = RHIShaderVertexInputReflection::ScalarType::UInt32;
                component_count = 1u;
                byte_size = 2u;
                return true;
            case PixelFormat::R32UInt:
                scalar_type = RHIShaderVertexInputReflection::ScalarType::UInt32;
                component_count = 1u;
                byte_size = 4u;
                return true;
            default:
                return false;
            }
        }

        std::string canonical_semantic_name(const std::string& semantic_name)
        {
            std::string result = semantic_name;
            for (char& character : result)
            {
                if (character >= 'a' && character <= 'z')
                {
                    character = static_cast<char>(character - 'a' + 'A');
                }
            }
            return result;
        }

        RHIFormatUsage required_format_usage(RHIResourceUsage usage)
        {
            RHIFormatUsage result = RHIFormatUsage::None;
            if (EnumHasAnyFlags(usage, RHIResourceUsage::ShaderResource))
            {
                result |= RHIFormatUsage::Sampled;
            }
            if (EnumHasAnyFlags(usage, RHIResourceUsage::UnorderedAccess))
            {
                result |= RHIFormatUsage::Storage;
            }
            if (EnumHasAnyFlags(usage, RHIResourceUsage::RenderTarget))
            {
                result |= RHIFormatUsage::RenderTarget;
            }
            if (EnumHasAnyFlags(usage, RHIResourceUsage::DepthStencil))
            {
                result |= RHIFormatUsage::DepthStencil;
            }
            if (EnumHasAnyFlags(usage, RHIResourceUsage::CopySource))
            {
                result |= RHIFormatUsage::CopySource;
            }
            if (EnumHasAnyFlags(usage, RHIResourceUsage::CopyDestination))
            {
                result |= RHIFormatUsage::CopyDestination;
            }
            return result;
        }

        RHIResult<RHIResourceBindingType> binding_value_type(const RHIBindingValue& value)
        {
            const std::uint32_t populated_fields = (value.buffer ? 1U : 0U) + (value.buffer_view ? 1U : 0U) +
                                                   (value.texture_view ? 1U : 0U) + (value.sampler ? 1U : 0U);
            if (populated_fields != 1)
            {
                return RHIResult<RHIResourceBindingType>::failure(RHIErrorCode::InvalidArgument,
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
                return RHIResult<RHIResourceBindingType>::success(value.texture_view->desc().type ==
                                                                          RHIResourceViewType::UnorderedAccess
                                                                      ? RHIResourceBindingType::StorageTexture
                                                                      : RHIResourceBindingType::SampledTexture);
            }
            return RHIResult<RHIResourceBindingType>::success(value.buffer_view->desc().type ==
                                                                      RHIResourceViewType::UnorderedAccess
                                                                  ? RHIResourceBindingType::StorageBuffer
                                                                  : RHIResourceBindingType::ReadOnlyBuffer);
        }
    } // namespace

    const RHIClearValue RHIClearValue::None = RHIClearValue::none();
    const RHIClearValue RHIClearValue::Black = RHIClearValue::color_value(vec4(0.0F));
    const RHIClearValue RHIClearValue::White = RHIClearValue::color_value(vec4(1.0F));
    const RHIClearValue RHIClearValue::DepthOne = RHIClearValue::depth_stencil_value(1.0F, 0);
    const RHIClearValue RHIClearValue::DepthZero = RHIClearValue::depth_stencil_value(0.0F, 0);

    bool RHIBindingLayoutEntry::operator==(const RHIBindingLayoutEntry& other) const
    {
        return binding_id == other.binding_id && group == other.group && target_binding == other.target_binding &&
               type == other.type && stages == other.stages && array_count == other.array_count &&
               data_size == other.data_size && data_layout_hash == other.data_layout_hash &&
               shader_abi_version == other.shader_abi_version;
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
        if (EnumHasAnyFlags(desc.usage, RHIResourceUsage::RenderTarget | RHIResourceUsage::DepthStencil))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffers cannot use render-target or depth-stencil usage.");
        }
        if (desc.structure_stride != 0)
        {
            if (desc.structure_stride > desc.size || desc.size % desc.structure_stride != 0)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Structured-buffer size must be a multiple of its structure stride.");
            }
            if (!EnumHasAnyFlags(desc.usage, RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Structured buffers require shader-resource or unordered-access usage.");
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_buffer_initial_data(const RHIBufferDesc& desc, const RHIInitialData& initial_data)
    {
        const RHIStatus desc_status = validate_buffer_desc(desc);
        if (!desc_status)
        {
            return desc_status;
        }
        if (initial_data.data == nullptr || initial_data.size == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer initial data and size must be specified together.");
        }
        if (initial_data.size > desc.size)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer initial data exceeds the destination buffer size.");
        }
        if (initial_data.row_pitch != 0 || initial_data.slice_pitch != 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer initial data cannot specify row or slice pitch.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_desc(const RHITextureDesc& desc)
    {
        if (desc.dimension == RHIResourceDimension::Buffer)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture descriptor cannot use Buffer dimension.");
        }
        if (desc.width == 0 || desc.height == 0 || desc.depth == 0 || desc.array_layers == 0 || desc.mip_levels == 0 ||
            desc.sample_count == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture dimensions, layers, mips, and samples must be non-zero.");
        }
        if (desc.format == PixelFormat::Unknown)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture format must be specified.");
        }
        if (desc.usage == RHIResourceUsage::None)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture usage must not be None.");
        }
        if (EnumHasAnyFlags(desc.usage, RHIResourceUsage::VertexBuffer | RHIResourceUsage::IndexBuffer |
                                            RHIResourceUsage::UniformBuffer | RHIResourceUsage::IndirectArguments))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Textures cannot use vertex, index, uniform-buffer, or indirect-argument usage.");
        }
        if (EnumHasAnyFlags(desc.usage, RHIResourceUsage::RenderTarget) && is_depth_format(desc.format))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Depth/stencil formats cannot use render-target usage.");
        }
        if (EnumHasAnyFlags(desc.usage, RHIResourceUsage::DepthStencil) && !is_depth_format(desc.format))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Depth-stencil usage requires a depth/stencil format.");
        }
        if (desc.sample_count > 1 && desc.mip_levels > 1)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Multisampled textures cannot have multiple mip levels.");
        }
        if (desc.dimension == RHIResourceDimension::Texture3D && desc.array_layers != 1)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "3D textures cannot have array layers.");
        }
        return RHIStatus::success();
    }

    bool RHIShaderVertexInputReflection::operator==(const RHIShaderVertexInputReflection& other) const
    {
        return semantic_name == other.semantic_name && semantic_index == other.semantic_index &&
               location == other.location && scalar_type == other.scalar_type &&
               component_count == other.component_count;
    }

    RHIStatus validate_texture_format_capabilities(const RHITextureDesc& desc,
                                                   const RHIFormatCapabilities& capabilities)
    {
        const RHIStatus desc_status = validate_texture_desc(desc);
        if (!desc_status)
        {
            return desc_status;
        }
        const RHIFormatUsage required_usage = required_format_usage(desc.usage);
        if (!EnumHasAllFlags(capabilities.usage, required_usage))
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported,
                                      "Texture format does not support every requested usage.");
        }
        if ((capabilities.supported_sample_counts & desc.sample_count) == 0)
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported,
                                      "Texture format does not support the requested sample count.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_subresource_range(const RHITextureDesc& texture_desc, const RHISubresourceRange& range)
    {
        if (range.first_mip >= texture_desc.mip_levels || range.first_layer >= texture_desc.array_layers)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture subresource range begins outside the texture.");
        }
        const std::uint32_t mip_count =
            range.mip_count == RHI_ALL_MIPS ? texture_desc.mip_levels - range.first_mip : range.mip_count;
        const std::uint32_t layer_count =
            range.layer_count == RHI_ALL_LAYERS ? texture_desc.array_layers - range.first_layer : range.layer_count;
        if (mip_count == 0 || layer_count == 0 || mip_count > texture_desc.mip_levels - range.first_mip ||
            layer_count > texture_desc.array_layers - range.first_layer)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture subresource range extends outside the texture.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_initial_data(const RHITextureDesc& desc, const RHIInitialData& initial_data)
    {
        const RHIStatus desc_status = validate_texture_desc(desc);
        if (!desc_status)
        {
            return desc_status;
        }
        if (initial_data.data == nullptr || initial_data.size == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture initial data and size must be specified together.");
        }
        if (initial_data.row_pitch == 0 || initial_data.slice_pitch == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture initial data requires row and slice pitch.");
        }
        if (initial_data.slice_pitch > initial_data.size)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture slice pitch exceeds the provided data size.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_texture_view_desc(const RHITextureDesc& texture_desc, const RHITextureViewDesc& view_desc)
    {
        const RHIStatus texture_status = validate_texture_desc(texture_desc);
        if (!texture_status)
        {
            return texture_status;
        }
        if (view_desc.format == PixelFormat::Unknown)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture view format must be specified.");
        }
        if (view_desc.format != texture_desc.format)
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported, "Texture views currently require the texture's public "
                                                                 "format; backend typeless storage remains internal.");
        }
        const RHIStatus range_status = validate_texture_subresource_range(texture_desc, view_desc.subresources);
        if (!range_status)
        {
            return range_status;
        }
        const RHIResourceUsage required_usage =
            view_desc.type == RHIResourceViewType::ShaderResource    ? RHIResourceUsage::ShaderResource
            : view_desc.type == RHIResourceViewType::UnorderedAccess ? RHIResourceUsage::UnorderedAccess
            : view_desc.type == RHIResourceViewType::RenderTarget    ? RHIResourceUsage::RenderTarget
                                                                     : RHIResourceUsage::DepthStencil;
        if (!EnumHasAnyFlags(texture_desc.usage, required_usage))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Texture was not created for the requested view type.");
        }
        if (view_desc.type != RHIResourceViewType::DepthStencil &&
            (view_desc.depth_read_only || view_desc.stencil_read_only))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Depth/stencil read-only flags are valid only for depth-stencil views.");
        }
        const bool depth_format = is_depth_format(view_desc.format);
        if (view_desc.type == RHIResourceViewType::RenderTarget ||
            view_desc.type == RHIResourceViewType::UnorderedAccess)
        {
            if (depth_format || view_desc.subresources.aspect != RHITextureAspect::Color)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Render-target and unordered-access views require a color format and color aspect.");
            }
        }
        else if (view_desc.type == RHIResourceViewType::DepthStencil)
        {
            const RHITextureAspect required_aspect =
                has_stencil(view_desc.format) ? RHITextureAspect::DepthStencil : RHITextureAspect::Depth;
            if (!depth_format || view_desc.subresources.aspect != required_aspect)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Depth-stencil views must select every aspect present in the depth/stencil format.");
            }
        }
        else if (depth_format)
        {
            if (view_desc.subresources.aspect != RHITextureAspect::Depth)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Sampled depth/stencil textures expose a depth-only shader-resource view.");
            }
        }
        else if (view_desc.subresources.aspect != RHITextureAspect::Color)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Color texture views require the color aspect.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_buffer_view_desc(const RHIBufferDesc& buffer_desc, const RHIBufferViewDesc& view_desc)
    {
        const RHIStatus buffer_status = validate_buffer_desc(buffer_desc);
        if (!buffer_status)
        {
            return buffer_status;
        }
        if (view_desc.type != RHIResourceViewType::ShaderResource &&
            view_desc.type != RHIResourceViewType::UnorderedAccess)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffers only support shader-resource and unordered-access views.");
        }
        if (view_desc.size == 0 || view_desc.offset > buffer_desc.size ||
            view_desc.size > buffer_desc.size - view_desc.offset)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Buffer view range is outside the buffer.");
        }
        const RHIResourceUsage required_usage = view_desc.type == RHIResourceViewType::ShaderResource
                                                    ? RHIResourceUsage::ShaderResource
                                                    : RHIResourceUsage::UnorderedAccess;
        if (!EnumHasAnyFlags(buffer_desc.usage, required_usage))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Buffer was not created for the requested view type.");
        }
        if (buffer_desc.structure_stride != 0)
        {
            if (view_desc.format != PixelFormat::Unknown || view_desc.offset % buffer_desc.structure_stride != 0 ||
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
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Shader content hash must be stable and non-zero.");
        }

        using BindingKey = std::tuple<RHIBindingGroup, BindingRegisterClass, std::uint32_t>;
        std::set<BindingKey> reflected_bindings;
        for (const RHIShaderBindingReflection& binding : desc.reflection)
        {
            if (binding.binding_id == 0 || binding.name.empty() || binding.array_count == 0)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Shader reflection bindings require a name and non-zero array count.");
            }
            const bool data_hash_is_zero = std::all_of(binding.data_layout_hash.begin(), binding.data_layout_hash.end(),
                                                       [](std::uint8_t byte) { return byte == 0u; });
            if ((binding.type == RHIResourceBindingType::UniformBuffer &&
                 (binding.data_size == 0 || data_hash_is_zero || binding.shader_abi_version == 0)) ||
                (binding.type != RHIResourceBindingType::UniformBuffer &&
                 (binding.data_size != 0 || !data_hash_is_zero || binding.shader_abi_version != 0)))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Shader reflection binding data ABI metadata is invalid.");
            }
            if (!reflected_bindings
                     .emplace(binding.group, binding_register_class(binding.type), binding.target_binding)
                     .second)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Shader reflection register must be unique within its group and resource class.");
            }
        }

        if (desc.stage != RHIShaderStage::Vertex && !desc.vertex_inputs.empty())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Only vertex shaders may contain vertex-input reflection.");
        }
        using SemanticKey = std::pair<std::string, std::uint32_t>;
        std::set<SemanticKey> reflected_semantics;
        std::set<std::uint32_t> reflected_locations;
        for (const RHIShaderVertexInputReflection& input : desc.vertex_inputs)
        {
            if (input.semantic_name.empty() || input.semantic_index == std::numeric_limits<std::uint32_t>::max() ||
                input.location == std::numeric_limits<std::uint32_t>::max())
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Shader vertex inputs require a semantic and valid semantic index and location.");
            }
            if (static_cast<std::uint32_t>(input.scalar_type) >
                    static_cast<std::uint32_t>(RHIShaderVertexInputReflection::ScalarType::UInt32) ||
                input.component_count == 0 || input.component_count > 4)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Shader vertex input scalar and component shape is unsupported.");
            }
            if (!reflected_semantics.emplace(canonical_semantic_name(input.semantic_name), input.semantic_index).second)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Shader vertex-input semantic and index must be unique.");
            }
            if (!reflected_locations.insert(input.location).second)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Shader vertex-input location must be unique.");
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_binding_layout_desc(const RHIBindingLayoutDesc& desc)
    {
        std::vector<RHIBindingLayoutEntry> bindings;
        for (const RHIBindingLayoutEntry& entry : desc.entries)
        {
            if (entry.binding_id == 0 || entry.group == RHIBindingGroup::Max || entry.array_count == 0)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Binding layout entries require a stable ID, logical group, and non-zero array count.");
            }
            const bool data_hash_is_zero = std::all_of(entry.data_layout_hash.begin(), entry.data_layout_hash.end(),
                                                       [](std::uint8_t byte) { return byte == 0u; });
            if ((entry.type == RHIResourceBindingType::UniformBuffer &&
                 (entry.data_size == 0 || data_hash_is_zero || entry.shader_abi_version == 0)) ||
                (entry.type != RHIResourceBindingType::UniformBuffer &&
                 (entry.data_size != 0 || !data_hash_is_zero || entry.shader_abi_version != 0)))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Binding layout entry data ABI metadata is invalid.");
            }
            if (entry.stages == RHIShaderStageFlags::None)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Binding must be visible to at least one shader stage.");
            }
            for (const RHIBindingLayoutEntry& existing : bindings)
            {
                if (existing.group == entry.group &&
                    binding_register_class(existing.type) == binding_register_class(entry.type) &&
                    EnumHasAnyFlags(existing.stages, entry.stages) &&
                    binding_ranges_overlap(existing.target_binding, existing.array_count, entry.target_binding,
                                           entry.array_count))
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Binding register ranges must not overlap within one logical group, shader "
                        "stages, and resource class.");
                }
            }
            bindings.push_back(entry);
        }
        return RHIStatus::success();
    }

    RHIStatus validate_sampler_desc(const RHISamplerDesc& desc)
    {
        if (!std::isfinite(desc.mip_lod_bias) || !std::isfinite(desc.min_lod) || !std::isfinite(desc.max_lod) ||
            desc.min_lod > desc.max_lod)
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
        if (desc.group == RHIBindingGroup::Max || desc.bindings.empty())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Binding set requires one logical group and at least one value.");
        }

        std::set<std::pair<ShaderParameterId, std::uint32_t>> supplied_bindings;
        for (const RHIBindingValue& value : desc.bindings)
        {
            if (value.binding_id == 0 || !supplied_bindings.emplace(value.binding_id, value.array_index).second)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Binding set contains an invalid or duplicate logical binding value.");
            }
            const auto value_type = binding_value_type(value);
            if (!value_type)
            {
                return value_type.status();
            }
            if (value.buffer)
            {
                if (!EnumHasAnyFlags(value.buffer->desc().usage, RHIResourceUsage::UniformBuffer) ||
                    value.buffer_offset >= value.buffer->desc().size)
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Uniform-buffer binding requires UniformBuffer usage and a valid offset.");
                }
                const std::uint64_t range =
                    value.buffer_size == 0 ? value.buffer->desc().size - value.buffer_offset : value.buffer_size;
                if (range == 0 || range > value.buffer->desc().size - value.buffer_offset)
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                              "Uniform-buffer binding range is invalid.");
                }
                const bool hash_is_zero = std::all_of(value.data_layout_hash.begin(), value.data_layout_hash.end(),
                                                      [](std::uint8_t byte) { return byte == 0u; });
                if (hash_is_zero || value.shader_abi_version == 0)
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Uniform-buffer binding requires a data layout hash and Shader ABI version.");
                }
            }
            else if (value.buffer_offset != 0 || value.buffer_size != 0 || value.shader_abi_version != 0 ||
                     std::any_of(value.data_layout_hash.begin(), value.data_layout_hash.end(),
                                 [](std::uint8_t byte) { return byte != 0u; }))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Buffer range and data ABI are valid only for uniform-buffer bindings.");
            }
        }
        return RHIStatus::success();
    }

    RHIStatus validate_graphics_pipeline_desc(const RHIGraphicsPipelineDesc& desc)
    {
        if (!desc.vertex_shader || !desc.pixel_shader || !desc.binding_layout)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Graphics pipeline requires vertex shader, pixel shader, and binding layout.");
        }
        if (desc.vertex_shader->desc().stage != RHIShaderStage::Vertex ||
            desc.pixel_shader->desc().stage != RHIShaderStage::Pixel)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Graphics pipeline shader stages do not match their roles.");
        }
        const RHIStatus vertex_shader_status = validate_shader_desc(desc.vertex_shader->desc());
        if (!vertex_shader_status)
        {
            return vertex_shader_status;
        }
        const RHIStatus pixel_shader_status = validate_shader_desc(desc.pixel_shader->desc());
        if (!pixel_shader_status)
        {
            return pixel_shader_status;
        }
        if (desc.color_attachment_count > desc.color_formats.size())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Pipeline has too many color attachments.");
        }
        if (desc.sample_count == 0 || (desc.sample_count & (desc.sample_count - 1U)) != 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Pipeline sample count must be a non-zero power of two.");
        }
        std::set<std::uint32_t> vertex_bindings;
        for (const RHIGraphicsPipelineDesc::VertexBufferLayout& layout : desc.vertex_buffers)
        {
            if (layout.stride == 0 || !vertex_bindings.emplace(layout.binding).second)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Vertex-buffer layouts require a non-zero stride and unique binding index.");
            }
        }
        std::set<std::uint32_t> attribute_locations;
        for (const RHIGraphicsPipelineDesc::VertexAttribute& attribute : desc.vertex_attributes)
        {
            if (attribute.format == PixelFormat::Unknown ||
                vertex_bindings.find(attribute.binding) == vertex_bindings.end() ||
                !attribute_locations.emplace(attribute.location).second)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vertex attributes require a format, an existing binding, and a unique location.");
            }

            RHIShaderVertexInputReflection::ScalarType scalar_type =
                RHIShaderVertexInputReflection::ScalarType::Float32;
            std::uint32_t component_count = 0u;
            std::uint32_t byte_size = 0u;
            if (!vertex_format_shape(attribute.format, scalar_type, component_count, byte_size))
            {
                return RHIStatus::failure(RHIErrorCode::Unsupported,
                                          "Vertex attribute format has no common RHI input shape.");
            }
            const auto layout = std::find_if(desc.vertex_buffers.begin(), desc.vertex_buffers.end(),
                                             [&](const RHIGraphicsPipelineDesc::VertexBufferLayout& candidate)
                                             { return candidate.binding == attribute.binding; });
            if (attribute.offset > layout->stride || byte_size > layout->stride - attribute.offset)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Vertex attribute byte range exceeds its buffer stride.");
            }
            const auto reflected = std::find_if(
                desc.vertex_shader->desc().vertex_inputs.begin(), desc.vertex_shader->desc().vertex_inputs.end(),
                [&](const RHIShaderVertexInputReflection& input) { return input.location == attribute.location; });
            if (reflected == desc.vertex_shader->desc().vertex_inputs.end() || reflected->scalar_type != scalar_type ||
                reflected->component_count != component_count)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Vertex attribute location and format must match shader input reflection.");
            }
        }
        if (desc.vertex_attributes.size() != desc.vertex_shader->desc().vertex_inputs.size())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Graphics pipeline vertex layout must provide every shader vertex input exactly once.");
        }
        for (std::uint32_t index = 0; index < desc.color_attachment_count; ++index)
        {
            if (desc.color_formats[index] == PixelFormat::Unknown)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Enabled color attachment format must be specified.");
            }
        }
        const bool depth_state_enabled = desc.depth_stencil.depth_test_enable ||
                                         desc.depth_stencil.depth_write_enable ||
                                         desc.depth_stencil.stencil_test_enable;
        if (desc.depth_stencil.depth_write_enable && !desc.depth_stencil.depth_test_enable)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Graphics pipeline depth writes require depth testing to be enabled.");
        }
        if (depth_state_enabled && desc.depth_stencil_format == PixelFormat::Unknown)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Enabled depth/stencil state requires a depth-stencil attachment format.");
        }
        return RHIStatus::success();
    }
} // namespace toy3d
