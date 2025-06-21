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
    }

    VkAttachmentDescription VulkanDynamicRHI::cast_vk_attachment_desc(const RHIRenderPassInfo &info)
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

    void VulkanDynamicRHI::end_render_pass()
    {
    }
}