#pragma once
#include "vulkan_context.h"

namespace toy3d
{
    class VulkanSwapChain
    {
    public:
        VulkanSwapChain(VulkanContext& context);
        ~VulkanSwapChain();

        VkResult acquire_next_image(uint32_t& image_index, VkSemaphore semaphore, VkFence fence=VK_NULL_HANDLE);

        const std::vector<VkImage>& get_images(){return m_images;}

        const VkExtent2D get_extent(){return m_swapchain_extent;}

        VkFormat get_format(){return m_swapchain_format;}

        VkSwapchainKHR get_handle(){return m_swapchain;}
    private:
        void init_swapchain();
    private:
        VulkanContext& m_context;
        VkFormat m_swapchain_format;
        VkExtent2D m_swapchain_extent;
        std::vector<VkImage> m_images;
        VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    };
}// namespace toy3d