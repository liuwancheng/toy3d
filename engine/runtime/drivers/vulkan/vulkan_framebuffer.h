#pragma once
#include "vulkan_context.h"

namespace toy3d
{
    class VulkanFrameBuffer
    {
    public:
        VulkanFrameBuffer(VulkanContext* in_context, const std::vector<VkImageView> &attachments, VkRenderPass render_pass);

        ~VulkanFrameBuffer();

        VkFramebuffer get_handle() const{return handle;}
  private:
        VulkanContext* context;

        VkFramebuffer handle{VK_NULL_HANDLE};

        VkExtent2D extent{};       
    };
}