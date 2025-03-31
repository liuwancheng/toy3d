#include "vulkan_framebuffer.h"

namespace toy3d
{
    VulkanFrameBuffer::VulkanFrameBuffer(toy3d::VulkanContext &in_context, const std::vector<VkImageView> &attachments, VkRenderPass render_pass)
    :context(in_context)
    {
        VkFramebufferCreateInfo create_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

        create_info.renderPass      = render_pass;
        create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        create_info.pAttachments    = attachments.data();
        create_info.width           = extent.width;
        create_info.height          = extent.height;
        create_info.layers          = 1;

        VK_CHECK(vkCreateFramebuffer(context.device, &create_info, nullptr, &handle));
    }

    VulkanFrameBuffer::~VulkanFrameBuffer()
    {
        vkDestroyFramebuffer(context.device, handle, nullptr);
    }
}