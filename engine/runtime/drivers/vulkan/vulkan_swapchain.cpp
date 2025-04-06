#include "vulkan_swapchain.h"
#include "vulkan/vulkan_context.h"

namespace toy3d
{

    VulkanSwapChain::VulkanSwapChain(VulkanContext* context)
    :m_context(context)
    {
        init_swapchain();
    }

    VulkanSwapChain::~VulkanSwapChain()
    {
        if(m_swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_context->device, m_swapchain, nullptr);
        }
    }

        
    void VulkanSwapChain::init_swapchain()
    {
        VkSurfaceCapabilitiesKHR cap;
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_context->gpu_device, m_context->surface, &cap));

        uint32_t count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_context->gpu_device, m_context->surface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_context->gpu_device, m_context->surface, &count, formats.data());

        VkSurfaceFormatKHR surface_format;
        surface_format.format = VK_FORMAT_UNDEFINED;
        for (VkSurfaceFormatKHR& available : formats)
        {
            if( available.format == VK_FORMAT_R8G8B8A8_UNORM ||
                available.format == VK_FORMAT_B8G8R8A8_UNORM ||
                available.format == VK_FORMAT_A8B8G8R8_UNORM_PACK32)
            {
                surface_format = available;
                break;
            }
        }
        if(surface_format.format == VK_FORMAT_UNDEFINED) 
        {
            std::cout << " surface format check failed! formats:" << count << std::endl;
            abort();
        }
        
        uint32_t desired_image_count = cap.minImageCount + 1;
        if(cap.maxImageCount && desired_image_count > cap.maxImageCount)
        {
            desired_image_count = cap.maxImageCount;
        }

        const VkCompositeAlphaFlagBitsKHR compositeAlpha = (cap.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) ?
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR : VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        VkSwapchainCreateInfoKHR create_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        create_info.surface             = m_context->surface;
        create_info.minImageCount       = desired_image_count;
        create_info.imageFormat         = surface_format.format;
        create_info.imageColorSpace     = surface_format.colorSpace;
        create_info.imageExtent         = cap.currentExtent;
        create_info.imageArrayLayers    = 1;
        create_info.imageUsage          = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        create_info.imageSharingMode    = VK_SHARING_MODE_EXCLUSIVE;
        create_info.preTransform        = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        create_info.compositeAlpha      = compositeAlpha; 
        create_info.presentMode         = VK_PRESENT_MODE_FIFO_KHR;
        create_info.clipped             = VK_TRUE;
        create_info.oldSwapchain        = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSwapchainKHR(m_context->device, &create_info, nullptr, &m_swapchain));

        m_swapchain_format = surface_format.format;
        m_swapchain_extent = cap.currentExtent;

        // 获取images
        uint32_t image_count;
        vkGetSwapchainImagesKHR(m_context->device, m_swapchain, &image_count, nullptr);
        m_images.resize(image_count);
        vkGetSwapchainImagesKHR(m_context->device, m_swapchain, &image_count, m_images.data());
    }

    VkResult VulkanSwapChain::acquire_next_image(uint32_t& image_index, VkSemaphore semaphore, VkFence fence)
    {
        return vkAcquireNextImageKHR(m_context->device, m_swapchain, std::numeric_limits<uint64_t>::max(), semaphore, fence, &image_index);
    }
}// namespace toy3d