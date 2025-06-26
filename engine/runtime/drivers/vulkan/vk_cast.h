#pragma once
#include "vk_com.h"

namespace toy3d
{

    // ========================================================================
    // Utility Functions
    // ========================================================================
    
    /// Convert VkResult to string for error reporting
    const std::string cast_vk_error(VkResult result);

    /// Initialize a Vulkan struct with proper sType and zero other fields
    template<typename T>
    void zero_vulkan_struct(T& vulkan_struct, VkStructureType vk_sType)
    {
        static_assert(!std::is_pointer_v<T>, "Don't use a pointer!");
        static_assert(std::is_standard_layout_v<T>, "T must be standard layout!");
        static_assert(offsetof(T, sType) == 0, "sType must be the first member!");
        
        std::memset(&vulkan_struct, 0, sizeof(T));
        vulkan_struct.sType = vk_sType;
    }

    // ========================================================================
    // Format Conversion Functions
    // ========================================================================
    
    /// Convert engine pixel format to Vulkan format
    VkFormat cast_format(const EPixelFormat &format);
    
    /// Convert MSAA sample count to Vulkan sample count flag bits
    VkSampleCountFlagBits cast_msaa(const uint32_t &nums);

    // ========================================================================
    // Render Target Functions
    // ========================================================================
    
    /// Convert render target load action to Vulkan attachment load operation
    VkAttachmentLoadOp cast_loadop(const ERenderTargetLoadAction &load_action);
    
    /// Convert render target store action to Vulkan attachment store operation
    VkAttachmentStoreOp cast_storeop(const ERenderTargetStoreAction &store_action);

    // ========================================================================
    // Rasterizer State Functions
    // ========================================================================
    
    /// Convert fill mode to Vulkan polygon mode
    VkPolygonMode cast_fill_mode(ERasterizerFillMode fill_mode);
    
    /// Convert cull mode to Vulkan cull mode flags
    VkCullModeFlags cast_cull_mode(ERasterizerCullMode cull_mode);

    // ========================================================================
    // Sampler State Functions
    // ========================================================================
    
    /// Convert sampler filter to Vulkan mipmap mode
    VkSamplerMipmapMode cast_mipmap_mode(ESamplerFilter mip_filter);
    
    /// Convert sampler filter to Vulkan filter mode
    VkFilter cast_filter_mode(ESamplerFilter in_filter);
    
    /// Convert sampler address mode to Vulkan sampler address mode
    VkSamplerAddressMode cast_wrap_mode(ESamplerAddressMode address_mode);
    
    /// Convert sampler compare function to Vulkan compare operation
    VkCompareOp cast_sampler_compare_function(ESamplerCompareFunction sampler_cf);

    // ========================================================================
    // Blend State Functions
    // ========================================================================
    
    /// Convert blend operation to Vulkan blend operation
    VkBlendOp cast_blend_operation(EBlendOperation blend_op);
    
    /// Convert blend factor to Vulkan blend factor
    VkBlendFactor cast_blend_factor(EBlendFactor blend_factor);

    // ========================================================================
    // Depth Stencil State Functions
    // ========================================================================
    
    /// Convert compare function to Vulkan compare operation
    VkCompareOp cast_depth_stencil_compare_function(ECompareFunction in_op);
    
    /// Convert stencil operation to Vulkan stencil operation
    VkStencilOp cast_stencil_op(EStencilOp in_op);

    // ========================================================================
    // Render Pass Functions
    // ========================================================================
    
    /// Convert render pass info to Vulkan attachment description
    VkAttachmentDescription cast_attachment_desc(const RHIRenderPassInfo &info);

} // namespace toy3d