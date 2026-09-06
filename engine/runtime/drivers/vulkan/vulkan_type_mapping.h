#pragma once

#include "drivers/rhi/rhi_command_descriptors.h"
#include "drivers/rhi/rhi_descriptors.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

namespace toy3d
{
    RHIStatus vulkan_status_from_result(VkResult result, const char* operation);

    struct VulkanAccessState
    {
        VkPipelineStageFlags pipeline_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkAccessFlags access_mask = 0;
        VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool supports_buffer = false;
        bool supports_image = false;
    };

    VkFormat vulkan_format_from_pixel_format(PixelFormat format);
    bool is_vk_depth_format(VkFormat format);
    bool is_vk_stencil_format(VkFormat format);

    RHIResult<VkPrimitiveTopology> to_vk_primitive_topology(RHIPrimitiveTopology topology);
    RHIResult<VkVertexInputRate> to_vk_vertex_input_rate(RHIVertexInputRate input_rate);
    RHIResult<VkCullModeFlags> to_vk_cull_mode(RHICullMode cull_mode);
    RHIResult<VkFrontFace> to_vk_front_face(RHIFrontFace front_face);
    VkViewport to_vk_viewport(const RHIViewport& viewport);
    RHIResult<VkBlendFactor> to_vk_blend_factor(RHIBlendFactor factor);
    RHIResult<VkBlendOp> to_vk_blend_operation(RHIBlendOperation operation);
    VkColorComponentFlags to_vk_color_write_mask(RHIColorWriteMask mask);
    VkShaderStageFlags to_vk_shader_stage_flags(RHIShaderStageFlags stages);
    VkDescriptorType to_vk_descriptor_type(RHIResourceBindingType type);
    VkFilter to_vk_filter(RHIFilter filter);
    VkSamplerMipmapMode to_vk_mipmap_mode(RHIFilter filter);
    VkSamplerAddressMode to_vk_address_mode(RHIAddressMode mode);
    VkCompareOp to_vk_compare_operation(RHICompareOperation operation);
    VkStencilOp to_vk_stencil_operation(RHIStencilOperation operation);
    VkStencilOpState to_vk_stencil_face(const RHIGraphicsPipelineDesc::StencilFaceState& face, std::uint8_t read_mask,
                                        std::uint8_t write_mask);
    VkBorderColor to_vk_border_color(RHIBorderColor color);
    VkBufferUsageFlags to_vk_buffer_usage(RHIResourceUsage usage);
    VkImageUsageFlags to_vk_image_usage(RHIResourceUsage usage);
    RHIResult<VkImageType> to_vk_image_type(RHIResourceDimension dimension);
    RHIResult<VkSampleCountFlagBits> to_vk_sample_count(std::uint32_t sample_count);
    RHIResult<VkImageAspectFlags> to_vk_image_aspect(RHITextureAspect aspect, VkFormat format);
    RHIResult<VkImageViewType> to_vk_image_view_type(const RHITextureViewDesc& desc);
    RHIResult<VkAttachmentLoadOp> to_vk_load_operation(RHILoadOperation operation);
    RHIResult<VkAttachmentStoreOp> to_vk_store_operation(RHIStoreOperation operation);
    RHIStatus get_vulkan_access_state(RHIAccess access, VulkanAccessState& state);
} // namespace toy3d
