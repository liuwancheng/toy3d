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


    VkFormat cast_format(const EPixelFormat &format);
    VkSampleCountFlagBits cast_msaa(const uint32_t &nums);
    VkAttachmentLoadOp cast_loadop(const ERenderTargetLoadAction &load_action);
    VkAttachmentStoreOp cast_storeop(const ERenderTargetStoreAction &store_action);

}