#include "vulkan_resource.h"

namespace toy3d
{
  
    SwapFrameData::SwapFrameData(VulkanContext & context, VkImage swap_image)
    :m_context(context)
    {
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(context.device, &fence_info, nullptr, &fence));

        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(context.device, &semaphore_info, nullptr, &semaphore));

        // command pool
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = context.graphics_family_index;
        VK_CHECK(vkCreateCommandPool(context.device, &pool_info, nullptr, &cmd_pool));

        // command buffer  
        VkCommandBufferAllocateInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_info.commandPool = cmd_pool;
        cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_info.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(context.device, &cmd_info, &cmd_buffer));

        // default color buffer
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format                      = context.get_swapchain().get_format();
        view_info.image                       = swap_image;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.components.r                = VK_COMPONENT_SWIZZLE_R;
        view_info.components.g                = VK_COMPONENT_SWIZZLE_G;
        view_info.components.b                = VK_COMPONENT_SWIZZLE_B;
        view_info.components.a                = VK_COMPONENT_SWIZZLE_A;
        VK_CHECK(vkCreateImageView(context.device, &view_info, nullptr, &default_color));
    }

    SwapFrameData::~SwapFrameData()
    {
        if(fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_context.device, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
        if(semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_context.device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
        if(cmd_buffer != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(m_context.device, cmd_pool, 1, &cmd_buffer);
            cmd_buffer = VK_NULL_HANDLE;
        }
        if(cmd_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_context.device, cmd_pool, nullptr);
            cmd_pool = VK_NULL_HANDLE;
        }
        if(default_color != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_context.device, default_color, nullptr);
            default_color = VK_NULL_HANDLE;
        }
    }

    void SwapFrameData::wait_prev_frame(uint32_t timeout) const
    {
        vkWaitForFences(m_context.device, 1, &fence, VK_TRUE, timeout);

        // 重制fence状态
        VK_CHECK(vkResetFences(m_context.device, 1, &fence));
    }

    ////////////////////////////////////////////////////////////////////////
    VkFormat cast_format(const EPixelFormat &format)
    {
        // 以后再慢慢加吧！！！
        VkFormat vk_format{VK_FORMAT_R8G8B8A8_UNORM};
        switch (format)
        {
        case EPixelFormat::B8G8R8A8 :
            vk_format = VkFormat::VK_FORMAT_B8G8R8A8_UNORM;
            break;
        case EPixelFormat::A16G16B16R16 :
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

    VkAttachmentLoadOp cast_loadop(const ERenderTargetLoadAction &load_action)
    {
        VkAttachmentLoadOp load_op{VK_ATTACHMENT_LOAD_OP_NONE_EXT};
        switch (load_action)
        {
        case ERenderTargetLoadAction::EClear :
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
        case ERenderTargetStoreAction::EMultisampleResolve :
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

}// namespace toy3d