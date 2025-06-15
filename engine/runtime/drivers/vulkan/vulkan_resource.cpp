#include "vulkan_resource.h"
#include "core/math/math.h"
#include "vulkan/vulkan_context.h"

namespace toy3d
{
  
    SwapFrameData::SwapFrameData(VulkanContext* context, VkImage swap_image)
    :m_context(context)
    {
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(context->device, &fence_info, nullptr, &fence));

        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(context->device, &semaphore_info, nullptr, &semaphore));

        // command pool
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = context->graphics_family_index;
        VK_CHECK(vkCreateCommandPool(context->device, &pool_info, nullptr, &cmd_pool));

        // command buffer  
        VkCommandBufferAllocateInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_info.commandPool = cmd_pool;
        cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_info.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(context->device, &cmd_info, &cmd_buffer));

        // default color buffer
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format                      = context->get_swapchain().get_format();
        view_info.image                       = swap_image;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.components.r                = VK_COMPONENT_SWIZZLE_R;
        view_info.components.g                = VK_COMPONENT_SWIZZLE_G;
        view_info.components.b                = VK_COMPONENT_SWIZZLE_B;
        view_info.components.a                = VK_COMPONENT_SWIZZLE_A;
        VK_CHECK(vkCreateImageView(context->device, &view_info, nullptr, &default_color));
    }

    SwapFrameData::~SwapFrameData()
    {
        if(fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_context->device, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
        if(semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_context->device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
        if(cmd_buffer != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(m_context->device, cmd_pool, 1, &cmd_buffer);
            cmd_buffer = VK_NULL_HANDLE;
        }
        if(cmd_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_context->device, cmd_pool, nullptr);
            cmd_pool = VK_NULL_HANDLE;
        }
        if(default_color != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_context->device, default_color, nullptr);
            default_color = VK_NULL_HANDLE;
        }
    }

    void SwapFrameData::wait_prev_frame(uint32_t timeout) const
    {
        vkWaitForFences(m_context->device, 1, &fence, VK_TRUE, timeout);

        // 重制fence状态
        VK_CHECK(vkResetFences(m_context->device, 1, &fence));
    }

    ///////////////////////////////// vulkan state ////////////////////////////////////
    VulkanRasterizerState::VulkanRasterizerState(const RasterizerStateInitializerRHI& in_desc)
    {
        rhi_desc = in_desc;
        zero_vulkan_struct(rasterizer_state, VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
		rasterizer_state.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterizer_state.lineWidth = 1.0f;

        rasterizer_state.polygonMode = cast_fill_mode(in_desc.fill_mode);
        rasterizer_state.cullMode = cast_cull_mode(in_desc.cull_mode);

        //rasterizer_state.depthClampEnable = VK_FALSE;
        rasterizer_state.depthBiasEnable = in_desc.depth_bias != 0.0f ? VK_TRUE : VK_FALSE;
        //RasterizerState.rasterizerDiscardEnable = VK_FALSE;

        rasterizer_state.depthBiasSlopeFactor = in_desc.slope_scale_depth_bias;
        rasterizer_state.depthBiasConstantFactor = in_desc.depth_bias;
    }

    VulkanDepthStencilState::VulkanDepthStencilState(const DepthStencilStateInitializerRHI& in_desc)
    {
        rhi_desc = in_desc;
        zero_vulkan_struct(depth_stencil_state, VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);

        depth_stencil_state.depthTestEnable = (in_desc.depth_test != CF_Always || in_desc.enable_depth_write) ? VK_TRUE : VK_FALSE;
        depth_stencil_state.depthCompareOp = cast_depth_stencil_compare_function(in_desc.depth_test);
        depth_stencil_state.depthWriteEnable = in_desc.enable_depth_write ? VK_TRUE : VK_FALSE;

        {
            // 深度整体范围测试
            depth_stencil_state.depthBoundsTestEnable = in_desc.enable_depth_bounds;
            depth_stencil_state.minDepthBounds = 0.0f;
            depth_stencil_state.maxDepthBounds = 1.0f;
        }

        depth_stencil_state.stencilTestEnable = (in_desc.enable_front_face_stencil || in_desc.enable_back_face_stencil) ? VK_TRUE : VK_FALSE;

        // Front
        depth_stencil_state.back.failOp = cast_stencil_op(in_desc.front_face_depth_fail_stencil_op);
        depth_stencil_state.back.passOp = cast_stencil_op(in_desc.front_face_pass_stencil_op);
        depth_stencil_state.back.depthFailOp = cast_stencil_op(in_desc.front_face_depth_fail_stencil_op);
        depth_stencil_state.back.compareOp = cast_depth_stencil_compare_function(in_desc.front_face_stencil_test);
        depth_stencil_state.back.compareMask = in_desc.stencil_read_mask;
        depth_stencil_state.back.writeMask = in_desc.stencil_write_mask;
        depth_stencil_state.back.reference = 0;

        if (in_desc.enable_back_face_stencil)
        {
            // Back
            depth_stencil_state.front.failOp = cast_stencil_op(in_desc.back_face_depth_fail_stencil_op);
            depth_stencil_state.front.passOp = cast_stencil_op(in_desc.back_face_pass_stencil_op);
            depth_stencil_state.front.depthFailOp = cast_stencil_op(in_desc.back_face_depth_fail_stencil_op);
            depth_stencil_state.front.compareOp = cast_depth_stencil_compare_function(in_desc.back_face_stencil_test);
            depth_stencil_state.front.compareMask = in_desc.stencil_read_mask;
            depth_stencil_state.front.writeMask = in_desc.stencil_write_mask;
            depth_stencil_state.front.reference = 0;
        }
        else
        {
            depth_stencil_state.front = depth_stencil_state.back;
        }
    }

    VulkanBlendState::VulkanBlendState(const BlendStateInitializerRHI& in_desc)
    {
        rhi_desc = in_desc;
        for (uint32 index = 0; index < MaxSimultaneousRenderTargets; ++index)
        {
            const BlendStateInitializerRHI::PerRenderTargetBlendState& color_target = in_desc.render_targets[index];
            VkPipelineColorBlendAttachmentState& blend_state = blend_states[index];
            std::memset(&blend_state, 0, sizeof(VkPipelineColorBlendAttachmentState));

            blend_state.colorBlendOp = cast_blend_operation(color_target.color_blend_op);
            blend_state.alphaBlendOp = cast_blend_operation(color_target.alpha_blend_op);

            blend_state.dstColorBlendFactor = cast_blend_factor(color_target.color_dest_blend);
            blend_state.dstAlphaBlendFactor = cast_blend_factor(color_target.alpha_dest_blend);

            blend_state.srcColorBlendFactor = cast_blend_factor(color_target.color_src_blend);
            blend_state.srcAlphaBlendFactor = cast_blend_factor(color_target.alpha_src_blend);

            blend_state.blendEnable =
                (color_target.color_blend_op != BO_Add || color_target.color_dest_blend != BF_Zero || color_target.color_src_blend != BF_One ||
                color_target.alpha_blend_op != BO_Add || color_target.alpha_dest_blend != BF_Zero || color_target.alpha_src_blend != BF_One) ? VK_TRUE : VK_FALSE;

            blend_state.colorWriteMask = (color_target.color_write_mask & CW_RED) ? VK_COLOR_COMPONENT_R_BIT : 0;
            blend_state.colorWriteMask |= (color_target.color_write_mask & CW_GREEN) ? VK_COLOR_COMPONENT_G_BIT : 0;
            blend_state.colorWriteMask |= (color_target.color_write_mask & CW_BLUE) ? VK_COLOR_COMPONENT_B_BIT : 0;
            blend_state.colorWriteMask |= (color_target.color_write_mask & CW_ALPHA) ? VK_COLOR_COMPONENT_A_BIT : 0;
        }
    }

    uint32 g_vk_sampler_handle_counter = 0;

    VulkanSamplerState::VulkanSamplerState(const VkSamplerCreateInfo& info, VkDevice& device, const bool is_immutable)
    : vk_sampler(VK_NULL_HANDLE)
	, sampler_id(0)
	, b_immutable(is_immutable)
    {
        vkCreateSampler(device, &info, nullptr, &vk_sampler);

		sampler_id = ++g_vk_sampler_handle_counter;
    }

    void VulkanSamplerState::setup_sampler_createinfo(const SamplerStateInitializerRHI& in_desc, VkSamplerCreateInfo& create_info, uint32 device_max_anisotropy)
    {
        zero_vulkan_struct(create_info, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

        create_info.magFilter = cast_filter_mode(in_desc.filter);
        create_info.minFilter = cast_filter_mode(in_desc.filter);
        create_info.mipmapMode = cast_mipmap_mode(in_desc.filter);
        create_info.addressModeU = cast_sampler_mode(in_desc.address_u);
        create_info.addressModeV = cast_sampler_mode(in_desc.address_v);
        create_info.addressModeW = cast_sampler_mode(in_desc.address_w);

        create_info.mipLodBias = in_desc.mip_bias;
        
        create_info.maxAnisotropy = 1.0f;
        if (in_desc.filter == SF_AnisotropicLinear || in_desc.filter == SF_AnisotropicPoint)
        {
            create_info.maxAnisotropy = Math::clamp((float)in_desc.max_anisotropy, 1.0f, device_max_anisotropy);
        }
        create_info.anisotropyEnable = create_info.maxAnisotropy > 1.0f;

        create_info.compareEnable = in_desc.sampler_comparison_function != SCF_Never ? VK_TRUE : VK_FALSE;
        create_info.compareOp = cast_sampler_compare_function(in_desc.sampler_comparison_function);
        create_info.minLod = in_desc.min_mip_level;
        create_info.maxLod = in_desc.max_mip_level;
        create_info.borderColor = in_desc.border_color == 0 ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK : VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }
}// namespace toy3d