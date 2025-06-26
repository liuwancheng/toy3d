#include "vk_cast.h"

namespace toy3d
{
    // ========================================================================
    // Utility Functions Implementation
    // ========================================================================
    
    const std::string cast_vk_error(VkResult result)
    {
        switch (result)
        {
    #define STR(r)   \
        case VK_##r: \
            return #r
            STR(NOT_READY);
            STR(TIMEOUT);
            STR(EVENT_SET);
            STR(EVENT_RESET);
            STR(INCOMPLETE);
            STR(ERROR_OUT_OF_HOST_MEMORY);
            STR(ERROR_OUT_OF_DEVICE_MEMORY);
            STR(ERROR_INITIALIZATION_FAILED);
            STR(ERROR_DEVICE_LOST);
            STR(ERROR_MEMORY_MAP_FAILED);
            STR(ERROR_LAYER_NOT_PRESENT);
            STR(ERROR_EXTENSION_NOT_PRESENT);
            STR(ERROR_FEATURE_NOT_PRESENT);
            STR(ERROR_INCOMPATIBLE_DRIVER);
            STR(ERROR_TOO_MANY_OBJECTS);
            STR(ERROR_FORMAT_NOT_SUPPORTED);
            STR(ERROR_SURFACE_LOST_KHR);
            STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
            STR(SUBOPTIMAL_KHR);
            STR(ERROR_OUT_OF_DATE_KHR);
            STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
            STR(ERROR_VALIDATION_FAILED_EXT);
            STR(ERROR_INVALID_SHADER_NV);
    #undef STR
            default:
                return "UNKNOWN_ERROR";
        }
    }

    // ========================================================================
    // Format Conversion Functions Implementation
    // ========================================================================
    
    VkFormat cast_format(const EPixelFormat &format)
    {
        // 以后再慢慢加吧！！！
        VkFormat vk_format{VK_FORMAT_R8G8B8A8_UNORM};
        switch (format)
        {
        case EPixelFormat::B8G8R8A8:
            vk_format = VkFormat::VK_FORMAT_B8G8R8A8_UNORM;
            break;
        case EPixelFormat::A16G16B16R16:
            vk_format = VkFormat::VK_FORMAT_R16G16B16A16_UNORM;
            break;
        default:
            break;
        }
        return vk_format;
    }

    VkSampleCountFlagBits cast_msaa(const uint32_t &nums)
    {
        VkSampleCountFlagBits flag{VK_SAMPLE_COUNT_1_BIT};
        switch (nums)
        {
        case 2:
            flag = VkSampleCountFlagBits::VK_SAMPLE_COUNT_2_BIT;
            break;
        case 4:
            flag = VkSampleCountFlagBits::VK_SAMPLE_COUNT_4_BIT;
            break;
        case 8:
            flag = VkSampleCountFlagBits::VK_SAMPLE_COUNT_8_BIT;
            break;
        case 16:
            flag = VkSampleCountFlagBits::VK_SAMPLE_COUNT_16_BIT;
            break;
        case 32:
            flag = VkSampleCountFlagBits::VK_SAMPLE_COUNT_32_BIT;
            break;
        default:
            break;
        }
        return flag;
    }

    // ========================================================================
    // Render Target Functions Implementation
    // ========================================================================
    
    VkAttachmentLoadOp cast_loadop(const ERenderTargetLoadAction &load_action)
    {
        VkAttachmentLoadOp load_op{VK_ATTACHMENT_LOAD_OP_NONE_EXT};
        switch (load_action)
        {
        case ERenderTargetLoadAction::EClear:
            load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            break;
        case ERenderTargetLoadAction::ELoad:
            load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
            break;
        case ERenderTargetLoadAction::EDontCare:
            load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            break;
        default:
            break;
        }
        return load_op;
    }

    VkAttachmentStoreOp cast_storeop(const ERenderTargetStoreAction &store_action)
    {
        VkAttachmentStoreOp store_op{VK_ATTACHMENT_STORE_OP_NONE_EXT};
        switch (store_action)
        {
        case ERenderTargetStoreAction::EMultisampleResolve:
            store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            break;
        case ERenderTargetStoreAction::EStore:
            store_op = VK_ATTACHMENT_STORE_OP_STORE;
            break;
        case ERenderTargetStoreAction::EDontCare:
            store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            break;
        default:
            break;
        }
        return store_op;
    }

    // ========================================================================
    // Rasterizer State Functions Implementation
    // ========================================================================
    
    VkPolygonMode cast_fill_mode(ERasterizerFillMode fill_mode)
    {
        switch (fill_mode)
        {
            case FM_Point:          return VK_POLYGON_MODE_POINT;
            case FM_Wireframe:      return VK_POLYGON_MODE_LINE;
            case FM_Solid:          return VK_POLYGON_MODE_FILL;
            default:
                break;
        }
        return VK_POLYGON_MODE_MAX_ENUM;
    }

    VkCullModeFlags cast_cull_mode(ERasterizerCullMode cull_mode)
    {
        switch (cull_mode)
        {
            case CM_None:   return VK_CULL_MODE_NONE;
            case CM_CW:     return VK_CULL_MODE_FRONT_BIT;
            case CM_CCW:    return VK_CULL_MODE_BACK_BIT;
            default:        break;
        }
        return VK_CULL_MODE_NONE;
    }

    // ========================================================================
    // Sampler State Functions Implementation
    // ========================================================================
    
    VkSamplerMipmapMode cast_mipmap_mode(ESamplerFilter mip_filter)
    {
        switch (mip_filter)
        {
            case SF_Point:              return VK_SAMPLER_MIPMAP_MODE_NEAREST;
            case SF_Bilinear:           return VK_SAMPLER_MIPMAP_MODE_NEAREST;
            case SF_Trilinear:          return VK_SAMPLER_MIPMAP_MODE_LINEAR;
            case SF_AnisotropicPoint:   return VK_SAMPLER_MIPMAP_MODE_NEAREST;
            case SF_AnisotropicLinear:  return VK_SAMPLER_MIPMAP_MODE_LINEAR;
            default:
                break;
        }
        return VK_SAMPLER_MIPMAP_MODE_MAX_ENUM;
    }

    VkFilter cast_filter_mode(ESamplerFilter in_filter)
    {
        switch (in_filter)
        {
            case SF_Point:              return VK_FILTER_NEAREST;
            case SF_Bilinear:           return VK_FILTER_LINEAR;
            case SF_Trilinear:          return VK_FILTER_LINEAR;
            case SF_AnisotropicPoint:
            case SF_AnisotropicLinear:  return VK_FILTER_LINEAR;
            default:
                break;
        }
        return VK_FILTER_MAX_ENUM;
    }

    VkSamplerAddressMode cast_wrap_mode(ESamplerAddressMode address_mode)
    {
        switch (address_mode)
        {
            case AM_Wrap:       return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case AM_Clamp:      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case AM_Mirror:     return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case AM_Border:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            default:
                break;
        }
        return VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
    }

    VkCompareOp cast_sampler_compare_function(ESamplerCompareFunction sampler_cf)
    {
        switch (sampler_cf)
        {
            case SCF_Less:  return VK_COMPARE_OP_LESS;
            case SCF_Never: return VK_COMPARE_OP_NEVER;
            default:
                break;
        };
        return VK_COMPARE_OP_MAX_ENUM;
    }

    // ========================================================================
    // Blend State Functions Implementation
    // ========================================================================
    
    VkBlendOp cast_blend_operation(EBlendOperation blend_op)
    {
        switch (blend_op)
        {
            case BO_Add:                return VK_BLEND_OP_ADD;
            case BO_Subtract:           return VK_BLEND_OP_SUBTRACT;
            case BO_Min:                return VK_BLEND_OP_MIN;
            case BO_Max:                return VK_BLEND_OP_MAX;
            case BO_ReverseSubtract:    return VK_BLEND_OP_REVERSE_SUBTRACT;
            default:
                break;
        }
        return VK_BLEND_OP_MAX_ENUM;
    }

    VkBlendFactor cast_blend_factor(EBlendFactor blend_factor)
    {
        switch (blend_factor)
        {
            case BF_Zero:                       return VK_BLEND_FACTOR_ZERO;
            case BF_One:                        return VK_BLEND_FACTOR_ONE;
            case BF_SourceColor:                return VK_BLEND_FACTOR_SRC_COLOR;
            case BF_InverseSourceColor:         return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case BF_SourceAlpha:                return VK_BLEND_FACTOR_SRC_ALPHA;
            case BF_InverseSourceAlpha:         return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BF_DestAlpha:                  return VK_BLEND_FACTOR_DST_ALPHA;
            case BF_InverseDestAlpha:           return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case BF_DestColor:                  return VK_BLEND_FACTOR_DST_COLOR;
            case BF_InverseDestColor:           return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case BF_ConstantBlendFactor:        return VK_BLEND_FACTOR_CONSTANT_COLOR;
            case BF_InverseConstantBlendFactor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            case BF_Source1Color:               return VK_BLEND_FACTOR_SRC1_COLOR;
            case BF_InverseSource1Color:        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
            case BF_Source1Alpha:               return VK_BLEND_FACTOR_SRC1_ALPHA;
            case BF_InverseSource1Alpha:        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
            default:
                break;
        }
        return VK_BLEND_FACTOR_MAX_ENUM;
    }

    // ========================================================================
    // Depth Stencil State Functions Implementation
    // ========================================================================
    
    VkCompareOp cast_depth_stencil_compare_function(ECompareFunction in_op)
    {
        switch (in_op)
        {
            case CF_Less:           return VK_COMPARE_OP_LESS;
            case CF_LessEqual:      return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CF_Greater:        return VK_COMPARE_OP_GREATER;
            case CF_GreaterEqual:   return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CF_Equal:          return VK_COMPARE_OP_EQUAL;
            case CF_NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
            case CF_Never:          return VK_COMPARE_OP_NEVER;
            case CF_Always:         return VK_COMPARE_OP_ALWAYS;
            default:
                break;
        }
        return VK_COMPARE_OP_MAX_ENUM;
    }

    VkStencilOp cast_stencil_op(EStencilOp in_op)
    {
        switch (in_op)
        {
            case SO_Keep:                   return VK_STENCIL_OP_KEEP;
            case SO_Zero:                   return VK_STENCIL_OP_ZERO;
            case SO_Replace:                return VK_STENCIL_OP_REPLACE;
            case SO_SaturatedIncrement:     return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            case SO_SaturatedDecrement:     return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            case SO_Invert:                 return VK_STENCIL_OP_INVERT;
            case SO_Increment:              return VK_STENCIL_OP_INCREMENT_AND_WRAP;
            case SO_Decrement:              return VK_STENCIL_OP_DECREMENT_AND_WRAP;
            default:
                break;
        }
        return VK_STENCIL_OP_MAX_ENUM;
    }

    // ========================================================================
    // Render Pass Functions Implementation
    // ========================================================================
    
    VkAttachmentDescription cast_attachment_desc(const RHIRenderPassInfo &info)
    {
        uint8_t color_rt_nums = info.get_color_rt_num();
        bool b_depth_test = info.enable_depth_test();
        std::vector<VkAttachmentDescription> color_attachments;
        std::vector<VkAttachmentReference> color_refs;
        color_attachments.resize(color_rt_nums);
        color_refs.resize(color_rt_nums);
        
        for (size_t i = 0; i < color_rt_nums; i++)
        {
            const RHIRenderPassInfo::ColorEntry &entry = info.color_render_targets[i];
            VkAttachmentDescription& desc = color_attachments[i];
            desc.flags = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
            desc.format = cast_format(entry.render_target->get_format());
            desc.samples = cast_msaa(entry.render_target->get_mips_num());
            desc.loadOp = cast_loadop(entry.load_action);
            desc.storeOp = cast_storeop(entry.store_action);
            desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        
        VkAttachmentReference color_attachment_ref{};
        VkSubpassDescription subpass{};

        if(b_depth_test)
        {
            const RHIRenderPassInfo::DepthStencilEntry &entry = info.depth_stencil_render_target;
            VkAttachmentDescription desc;
            desc.format = cast_format(entry.depth_stencil_target->get_format());
            desc.samples = cast_msaa(entry.depth_stencil_target->get_mips_num());
            desc.stencilLoadOp = cast_loadop(entry.load_action);
            desc.stencilStoreOp = cast_storeop(entry.store_action);
            desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            //attachments[nums-1] = desc;
        }

        // subpass 暂时不处理了，等后续需要再加

        return VkAttachmentDescription();
    }

} // namespace toy3d