#pragma once

#include "rhi/rhi_resource.h"
#include "vk_com.h"

namespace toy3d
{
    class VulkanContext;

    /**
     * vulkan的back buffer是需要自己控制的，抽象一个数据层存放切换状态
    */
    class SwapFrameData
    {
    public:
        SwapFrameData(toy3d::VulkanContext *context, VkImage swap_image);
        ~SwapFrameData();

        SwapFrameData(const SwapFrameData &) = delete;
        SwapFrameData(SwapFrameData &&) = delete;
        SwapFrameData &operator=(const SwapFrameData &) = delete;
        SwapFrameData &operator=(SwapFrameData &&) = delete;
    public:
        void wait_prev_frame(uint32_t timeout = std::numeric_limits<uint32_t>::max()) const;
    public:
        VkSemaphore         semaphore{VK_NULL_HANDLE};
        VkFence             fence{VK_NULL_HANDLE};
        VkCommandPool       cmd_pool{VK_NULL_HANDLE};
        VkCommandBuffer     cmd_buffer{VK_NULL_HANDLE};
        VkImageView         default_color{VK_NULL_HANDLE};
    private:
        VulkanContext* m_context;
    };

    class VulkanRasterizerState : public RHIRasterizerState
    {
    public:
        VulkanRasterizerState(const RasterizerStateInitializerRHI& in_desc);
        virtual bool get_initializer(struct RasterizerStateInitializerRHI& out) override
        { 
            out = rhi_desc;
            return true;
        }
    public:
        VkPipelineRasterizationStateCreateInfo rasterizer_state;
        RasterizerStateInitializerRHI rhi_desc;
    };

    class VulkanDepthStencilState : public RHIDepthStencilState
    {
    public:
        VulkanDepthStencilState(const DepthStencilStateInitializerRHI& in_desc);
        virtual bool get_initializer(struct DepthStencilStateInitializerRHI& out) override
        { 
            out = rhi_desc;
            return true; 
        }
    public:
        VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
        DepthStencilStateInitializerRHI rhi_desc;
    };

    class VulkanBlendState : public RHIBlendState
    {
    public:
        VulkanBlendState(const BlendStateInitializerRHI& in_desc);
    
        virtual bool get_initializer(class BlendStateInitializerRHI& out) override
        { 
            out = rhi_desc;
            return true; 
        }
    public:
        VkPipelineColorBlendAttachmentState blend_states[MaxSimultaneousRenderTargets];
        BlendStateInitializerRHI rhi_desc;
    };

    class VulkanSamplerState : public RHISamplerState 
    {
    public:
	    VulkanSamplerState(const VkSamplerCreateInfo& info, VkDevice& device, const bool is_immutable = false);

	    static void setup_sampler_createinfo(const SamplerStateInitializerRHI& in_desc, VkSamplerCreateInfo& create_info, uint32 device_max_anisotropy);

        virtual bool is_immutable() const override{ return b_immutable; }
    public:
    	VkSampler vk_sampler;
        uint32 sampler_id;
    private:
        bool b_immutable;
    };

}