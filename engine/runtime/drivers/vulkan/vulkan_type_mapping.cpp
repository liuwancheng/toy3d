#include "drivers/vulkan/vulkan_type_mapping.h"

#include <string>

namespace toy3d
{
    RHIStatus vulkan_status_from_result(VkResult result, const char* operation)
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
        else if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            code = RHIErrorCode::OutOfDate;
        }
        return RHIStatus::failure(code, std::string(operation) + " failed with VkResult " +
                                            std::to_string(static_cast<int>(result)) + ".");
    }

    VkFormat vulkan_format_from_pixel_format(PixelFormat format)
    {
        switch (format)
        {
        case PixelFormat::R8UNorm:
            return VK_FORMAT_R8_UNORM;
        case PixelFormat::R8G8B8A8UNorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::R8G8B8A8UNormSRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case PixelFormat::B8G8R8A8UNorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::B8G8R8A8UNormSRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case PixelFormat::R16Float:
            return VK_FORMAT_R16_SFLOAT;
        case PixelFormat::R16G16Float:
            return VK_FORMAT_R16G16_SFLOAT;
        case PixelFormat::R16G16B16A16Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case PixelFormat::R32Float:
            return VK_FORMAT_R32_SFLOAT;
        case PixelFormat::R32G32Float:
            return VK_FORMAT_R32G32_SFLOAT;
        case PixelFormat::R32G32B32Float:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case PixelFormat::R32G32B32A32Float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case PixelFormat::R16UInt:
            return VK_FORMAT_R16_UINT;
        case PixelFormat::R32UInt:
            return VK_FORMAT_R32_UINT;
        case PixelFormat::R8SNorm:
            return VK_FORMAT_R8_SNORM;
        case PixelFormat::R8G8B8A8SNorm:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case PixelFormat::R10G10B10A2UNorm:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case PixelFormat::R11G11B10Float:
            return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case PixelFormat::BC1UNorm:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case PixelFormat::BC2UNorm:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case PixelFormat::BC3UNorm:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case PixelFormat::ASTC4x4:
            return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case PixelFormat::ASTC6x6:
            return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case PixelFormat::ASTC8x8:
            return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        case PixelFormat::ASTC12x12:
            return VK_FORMAT_ASTC_12x12_UNORM_BLOCK;
        case PixelFormat::D16UNorm:
            return VK_FORMAT_D16_UNORM;
        case PixelFormat::D24UNormS8UInt:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::D32Float:
            return VK_FORMAT_D32_SFLOAT;
        case PixelFormat::D32FloatS8UInt:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    bool is_vk_depth_format(VkFormat format)
    {
        return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D24_UNORM_S8_UINT ||
               format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    bool is_vk_stencil_format(VkFormat format)
    {
        return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    RHIResult<VkPrimitiveTopology> to_vk_primitive_topology(RHIPrimitiveTopology topology)
    {
        switch (topology)
        {
        case RHIPrimitiveTopology::PointList:
            return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
        case RHIPrimitiveTopology::LineList:
            return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        case RHIPrimitiveTopology::LineStrip:
            return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
        case RHIPrimitiveTopology::TriangleList:
            return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        case RHIPrimitiveTopology::TriangleStrip:
            return RHIResult<VkPrimitiveTopology>::success(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        }
        return RHIResult<VkPrimitiveTopology>::failure(RHIErrorCode::InvalidArgument,
                                                       "Unknown RHI primitive topology.");
    }

    RHIResult<VkVertexInputRate> to_vk_vertex_input_rate(RHIVertexInputRate input_rate)
    {
        switch (input_rate)
        {
        case RHIVertexInputRate::PerVertex:
            return RHIResult<VkVertexInputRate>::success(VK_VERTEX_INPUT_RATE_VERTEX);
        case RHIVertexInputRate::PerInstance:
            return RHIResult<VkVertexInputRate>::success(VK_VERTEX_INPUT_RATE_INSTANCE);
        }
        return RHIResult<VkVertexInputRate>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI vertex input rate.");
    }

    RHIResult<VkCullModeFlags> to_vk_cull_mode(RHICullMode cull_mode)
    {
        switch (cull_mode)
        {
        case RHICullMode::None:
            return RHIResult<VkCullModeFlags>::success(VK_CULL_MODE_NONE);
        case RHICullMode::Front:
            return RHIResult<VkCullModeFlags>::success(VK_CULL_MODE_FRONT_BIT);
        case RHICullMode::Back:
            return RHIResult<VkCullModeFlags>::success(VK_CULL_MODE_BACK_BIT);
        }
        return RHIResult<VkCullModeFlags>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI cull mode.");
    }

    RHIResult<VkFrontFace> to_vk_front_face(RHIFrontFace front_face)
    {
        // The Vulkan backend uses a negative viewport height to preserve the
        // public D3D-style clip-space Y convention. That viewport transform
        // reverses native framebuffer winding, so front-face mapping must be
        // reversed here as part of the same backend-only coordinate policy.
        switch (front_face)
        {
        case RHIFrontFace::CounterClockwise:
            return RHIResult<VkFrontFace>::success(VK_FRONT_FACE_CLOCKWISE);
        case RHIFrontFace::Clockwise:
            return RHIResult<VkFrontFace>::success(VK_FRONT_FACE_COUNTER_CLOCKWISE);
        }
        return RHIResult<VkFrontFace>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI front-face winding.");
    }

    VkViewport to_vk_viewport(const RHIViewport& viewport)
    {
        VkViewport result{};
        result.x = viewport.x;
        result.y = viewport.y + viewport.height;
        result.width = viewport.width;
        result.height = -viewport.height;
        result.minDepth = viewport.min_depth;
        result.maxDepth = viewport.max_depth;
        return result;
    }

    RHIResult<VkBlendFactor> to_vk_blend_factor(RHIBlendFactor factor)
    {
        switch (factor)
        {
        case RHIBlendFactor::Zero:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ZERO);
        case RHIBlendFactor::One:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE);
        case RHIBlendFactor::SourceColor:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_SRC_COLOR);
        case RHIBlendFactor::OneMinusSourceColor:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR);
        case RHIBlendFactor::DestinationColor:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_DST_COLOR);
        case RHIBlendFactor::OneMinusDestinationColor:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR);
        case RHIBlendFactor::SourceAlpha:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_SRC_ALPHA);
        case RHIBlendFactor::OneMinusSourceAlpha:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
        case RHIBlendFactor::DestinationAlpha:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_DST_ALPHA);
        case RHIBlendFactor::OneMinusDestinationAlpha:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA);
        case RHIBlendFactor::ConstantColor:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_CONSTANT_COLOR);
        case RHIBlendFactor::OneMinusConstantColor:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR);
        case RHIBlendFactor::SourceAlphaSaturate:
            return RHIResult<VkBlendFactor>::success(VK_BLEND_FACTOR_SRC_ALPHA_SATURATE);
        }
        return RHIResult<VkBlendFactor>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI blend factor.");
    }

    RHIResult<VkBlendOp> to_vk_blend_operation(RHIBlendOperation operation)
    {
        switch (operation)
        {
        case RHIBlendOperation::Add:
            return RHIResult<VkBlendOp>::success(VK_BLEND_OP_ADD);
        case RHIBlendOperation::Subtract:
            return RHIResult<VkBlendOp>::success(VK_BLEND_OP_SUBTRACT);
        case RHIBlendOperation::ReverseSubtract:
            return RHIResult<VkBlendOp>::success(VK_BLEND_OP_REVERSE_SUBTRACT);
        case RHIBlendOperation::Min:
            return RHIResult<VkBlendOp>::success(VK_BLEND_OP_MIN);
        case RHIBlendOperation::Max:
            return RHIResult<VkBlendOp>::success(VK_BLEND_OP_MAX);
        }
        return RHIResult<VkBlendOp>::failure(RHIErrorCode::InvalidArgument, "Unknown RHI blend operation.");
    }

    VkColorComponentFlags to_vk_color_write_mask(RHIColorWriteMask mask)
    {
        VkColorComponentFlags result = 0;
        if (EnumHasAnyFlags(mask, RHIColorWriteMask::Red))
            result |= VK_COLOR_COMPONENT_R_BIT;
        if (EnumHasAnyFlags(mask, RHIColorWriteMask::Green))
            result |= VK_COLOR_COMPONENT_G_BIT;
        if (EnumHasAnyFlags(mask, RHIColorWriteMask::Blue))
            result |= VK_COLOR_COMPONENT_B_BIT;
        if (EnumHasAnyFlags(mask, RHIColorWriteMask::Alpha))
            result |= VK_COLOR_COMPONENT_A_BIT;
        return result;
    }

    VkShaderStageFlags to_vk_shader_stage_flags(RHIShaderStageFlags stages)
    {
        VkShaderStageFlags result = 0;
        if (EnumHasAnyFlags(stages, RHIShaderStageFlags::Vertex))
            result |= VK_SHADER_STAGE_VERTEX_BIT;
        if (EnumHasAnyFlags(stages, RHIShaderStageFlags::Pixel))
            result |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (EnumHasAnyFlags(stages, RHIShaderStageFlags::Geometry))
            result |= VK_SHADER_STAGE_GEOMETRY_BIT;
        if (EnumHasAnyFlags(stages, RHIShaderStageFlags::Hull))
            result |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        if (EnumHasAnyFlags(stages, RHIShaderStageFlags::Domain))
            result |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        if (EnumHasAnyFlags(stages, RHIShaderStageFlags::Compute))
            result |= VK_SHADER_STAGE_COMPUTE_BIT;
        return result;
    }

    VkDescriptorType to_vk_descriptor_type(RHIResourceBindingType type)
    {
        switch (type)
        {
        case RHIResourceBindingType::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case RHIResourceBindingType::SampledTexture:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case RHIResourceBindingType::StorageTexture:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case RHIResourceBindingType::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case RHIResourceBindingType::ReadOnlyBuffer:
        case RHIResourceBindingType::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    VkFilter to_vk_filter(RHIFilter filter)
    {
        return filter == RHIFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    }

    VkSamplerMipmapMode to_vk_mipmap_mode(RHIFilter filter)
    {
        return filter == RHIFilter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    VkSamplerAddressMode to_vk_address_mode(RHIAddressMode mode)
    {
        switch (mode)
        {
        case RHIAddressMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case RHIAddressMode::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case RHIAddressMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case RHIAddressMode::ClampToBorder:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        }
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    VkCompareOp to_vk_compare_operation(RHICompareOperation operation)
    {
        switch (operation)
        {
        case RHICompareOperation::Never:
            return VK_COMPARE_OP_NEVER;
        case RHICompareOperation::Less:
            return VK_COMPARE_OP_LESS;
        case RHICompareOperation::Equal:
            return VK_COMPARE_OP_EQUAL;
        case RHICompareOperation::LessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case RHICompareOperation::Greater:
            return VK_COMPARE_OP_GREATER;
        case RHICompareOperation::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case RHICompareOperation::GreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case RHICompareOperation::Always:
            return VK_COMPARE_OP_ALWAYS;
        }
        return VK_COMPARE_OP_ALWAYS;
    }

    VkStencilOp to_vk_stencil_operation(RHIStencilOperation operation)
    {
        switch (operation)
        {
        case RHIStencilOperation::Keep:
            return VK_STENCIL_OP_KEEP;
        case RHIStencilOperation::Zero:
            return VK_STENCIL_OP_ZERO;
        case RHIStencilOperation::Replace:
            return VK_STENCIL_OP_REPLACE;
        case RHIStencilOperation::IncrementClamp:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case RHIStencilOperation::DecrementClamp:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case RHIStencilOperation::Invert:
            return VK_STENCIL_OP_INVERT;
        case RHIStencilOperation::IncrementWrap:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case RHIStencilOperation::DecrementWrap:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        }
        return VK_STENCIL_OP_KEEP;
    }

    VkStencilOpState to_vk_stencil_face(const RHIGraphicsPipelineDesc::StencilFaceState& face, std::uint8_t read_mask,
                                        std::uint8_t write_mask)
    {
        VkStencilOpState result{};
        result.failOp = to_vk_stencil_operation(face.fail_operation);
        result.passOp = to_vk_stencil_operation(face.pass_operation);
        result.depthFailOp = to_vk_stencil_operation(face.depth_fail_operation);
        result.compareOp = to_vk_compare_operation(face.compare_operation);
        result.compareMask = read_mask;
        result.writeMask = write_mask;
        result.reference = 0;
        return result;
    }

    VkBorderColor to_vk_border_color(RHIBorderColor color)
    {
        switch (color)
        {
        case RHIBorderColor::TransparentBlack:
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case RHIBorderColor::OpaqueBlack:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case RHIBorderColor::OpaqueWhite:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        }
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }

    VkBufferUsageFlags to_vk_buffer_usage(RHIResourceUsage usage)
    {
        VkBufferUsageFlags result = 0;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::VertexBuffer))
            result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::IndexBuffer))
            result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::UniformBuffer))
            result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::ShaderResource) ||
            EnumHasAnyFlags(usage, RHIResourceUsage::UnorderedAccess))
            result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::IndirectArguments))
            result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::CopySource))
            result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::CopyDestination))
            result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        return result;
    }

    VkImageUsageFlags to_vk_image_usage(RHIResourceUsage usage)
    {
        VkImageUsageFlags result = 0;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::ShaderResource))
            result |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::UnorderedAccess))
            result |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::RenderTarget))
            result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::DepthStencil))
            result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::CopySource))
            result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (EnumHasAnyFlags(usage, RHIResourceUsage::CopyDestination))
            result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        return result;
    }

    RHIResult<VkImageType> to_vk_image_type(RHIResourceDimension dimension)
    {
        switch (dimension)
        {
        case RHIResourceDimension::Texture1D:
            return RHIResult<VkImageType>::success(VK_IMAGE_TYPE_1D);
        case RHIResourceDimension::Texture2D:
            return RHIResult<VkImageType>::success(VK_IMAGE_TYPE_2D);
        case RHIResourceDimension::Texture3D:
            return RHIResult<VkImageType>::success(VK_IMAGE_TYPE_3D);
        default:
            return RHIResult<VkImageType>::failure(RHIErrorCode::InvalidArgument,
                                                   "Vulkan texture creation requires a texture resource dimension.");
        }
    }

    RHIResult<VkSampleCountFlagBits> to_vk_sample_count(std::uint32_t sample_count)
    {
        switch (sample_count)
        {
        case 1:
            return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_1_BIT);
        case 2:
            return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_2_BIT);
        case 4:
            return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_4_BIT);
        case 8:
            return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_8_BIT);
        case 16:
            return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_16_BIT);
        case 32:
            return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_32_BIT);
        case 64:
            return RHIResult<VkSampleCountFlagBits>::success(VK_SAMPLE_COUNT_64_BIT);
        default:
            return RHIResult<VkSampleCountFlagBits>::failure(
                RHIErrorCode::Unsupported, "Vulkan does not support the requested texture sample count.");
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
                return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_COLOR_BIT);
            break;
        case RHITextureAspect::Depth:
            if (has_depth)
                return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_DEPTH_BIT);
            break;
        case RHITextureAspect::Stencil:
            if (has_stencil)
                return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_STENCIL_BIT);
            break;
        case RHITextureAspect::DepthStencil:
            if (has_depth && has_stencil)
                return RHIResult<VkImageAspectFlags>::success(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
            break;
        }
        return RHIResult<VkImageAspectFlags>::failure(RHIErrorCode::InvalidArgument,
                                                      "Texture aspect is incompatible with its Vulkan format.");
    }

    RHIResult<VkImageViewType> to_vk_image_view_type(const RHITextureViewDesc& desc)
    {
        switch (desc.dimension)
        {
        case RHITextureViewDimension::Texture1D:
            return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_1D);
        case RHITextureViewDimension::Texture1DArray:
            return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_1D_ARRAY);
        case RHITextureViewDimension::Texture2D:
        case RHITextureViewDimension::Texture2DMS:
            return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_2D);
        case RHITextureViewDimension::Texture2DArray:
        case RHITextureViewDimension::Texture2DMSArray:
            return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_2D_ARRAY);
        case RHITextureViewDimension::Texture3D:
            return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_3D);
        case RHITextureViewDimension::TextureCube:
            return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_CUBE);
        case RHITextureViewDimension::TextureCubeArray:
            return RHIResult<VkImageViewType>::success(VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);
        }
        return RHIResult<VkImageViewType>::failure(RHIErrorCode::Unsupported,
                                                   "The requested RHI texture-view dimension has no Vulkan mapping.");
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
            state = {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     true, false};
            return RHIStatus::success();
        case RHIAccess::IndexBuffer:
            state = {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, true,
                     false};
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
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                     false, true};
            return RHIStatus::success();
        case RHIAccess::DepthStencilWrite:
            state = {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false, true};
            return RHIStatus::success();
        case RHIAccess::CopySource:
        case RHIAccess::ResolveSource:
            state = {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     true, true};
            return RHIStatus::success();
        case RHIAccess::CopyDestination:
        case RHIAccess::ResolveDestination:
            state = {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     true, true};
            return RHIStatus::success();
        default:
            return RHIStatus::failure(RHIErrorCode::Unsupported,
                                      "The requested RHI access mask has no Vulkan transition mapping.");
        }
    }
} // namespace toy3d
