#include "vulkan_rhi.h"

namespace toy3d
{
    void VulkanDynamicRHI::init()
    {
        vulkan_context = std::make_unique<VulkanContext>();
    }

    void VulkanDynamicRHI::clear()
    {
        vulkan_context.reset();
    }

    void VulkanDynamicRHI::begin_render_pass(const RHIRenderPassInfo &info, std::string pass_name)
    {
        cache_pass_info = info;
        vulkan_context.get()->get_active_frame();
        // TODO: 1、这里需要创建Vk的RenderPass
        // 2、创建Vk的FrameBuffer
        // 3、调用Vk的CmdBeginRenderPass

    }

    void VulkanDynamicRHI::end_render_pass()
    {
    }
}