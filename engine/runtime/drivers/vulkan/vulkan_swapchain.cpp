#include "drivers/vulkan/vulkan_swapchain.h"

#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_type_mapping.h"

#include <algorithm>
#include <string>
#include <utility>

namespace toy3d
{
    namespace
    {
        RHIStatus make_swapchain_status(VkResult result, const char* operation)
        {
            if (result == VK_SUCCESS)
            {
                return RHIStatus::success();
            }
            RHIErrorCode code = RHIErrorCode::BackendFailure;
            if (result == VK_ERROR_DEVICE_LOST)
            {
                code = RHIErrorCode::DeviceLost;
            }
            else if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                code = RHIErrorCode::OutOfDate;
            }
            else if (result == VK_SUBOPTIMAL_KHR)
            {
                code = RHIErrorCode::Suboptimal;
            }
            else if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            {
                code = RHIErrorCode::OutOfMemory;
            }
            return RHIStatus::failure(code, std::string(operation) + " failed with VkResult " +
                                                std::to_string(static_cast<int>(result)) + ".");
        }

        VkPresentModeKHR to_vk_present_mode(RHIPresentMode mode)
        {
            switch (mode)
            {
            case RHIPresentMode::Immediate:
                return VK_PRESENT_MODE_IMMEDIATE_KHR;
            case RHIPresentMode::Mailbox:
                return VK_PRESENT_MODE_MAILBOX_KHR;
            case RHIPresentMode::Fifo:
            default:
                return VK_PRESENT_MODE_FIFO_KHR;
            }
        }
    } // namespace

    RHIStatus map_vulkan_acquire_result(VkResult result)
    {
        return make_swapchain_status(result, "vkAcquireNextImageKHR");
    }

    RHIStatus map_vulkan_present_result(VkResult result)
    {
        return make_swapchain_status(result, "vkQueuePresentKHR");
    }

    std::uint32_t vulkan_frame_slot_count(std::uint32_t actual_image_count)
    {
        constexpr std::uint32_t max_frames_in_flight = 2;
        return std::min(max_frames_in_flight, actual_image_count);
    }

    VulkanSwapchain::VulkanSwapchain(const RHIDevice& owner, VkPhysicalDevice physical_device, VkDevice device,
                                     VkSurfaceKHR surface)
        : owner_device(owner), vk_physical_device(physical_device), vk_device(device), vk_surface(surface)
    {
    }

    VulkanSwapchain::~VulkanSwapchain()
    {
        const VkDevice device = vk_device;
        for (VulkanSwapchainImage& swapchain_image : swapchain_images)
        {
            swapchain_image.view.reset();
            swapchain_image.texture.reset();
            if (device != VK_NULL_HANDLE && swapchain_image.image_view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device, swapchain_image.image_view, nullptr);
            }
            if (device != VK_NULL_HANDLE && swapchain_image.rendering_done != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, swapchain_image.rendering_done, nullptr);
            }
        }
        swapchain_images.clear();
        if (device != VK_NULL_HANDLE && vk_swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device, vk_swapchain, nullptr);
        }
    }

    RHIResult<std::unique_ptr<VulkanSwapchain>> VulkanSwapchain::create(const RHIDevice& owner,
                                                                        VkPhysicalDevice physical_device,
                                                                        VkDevice device, VkSurfaceKHR surface,
                                                                        const RHIViewportContextDesc& desc,
                                                                        VkSwapchainKHR old_swapchain)
    {
        auto swapchain = std::make_unique<VulkanSwapchain>(owner, physical_device, device, surface);
        const RHIStatus status = swapchain->initialize(desc, old_swapchain);
        if (!status)
        {
            return RHIResult<std::unique_ptr<VulkanSwapchain>>::failure(status.code(), status.message());
        }
        return RHIResult<std::unique_ptr<VulkanSwapchain>>::success(std::move(swapchain));
    }

    RHIStatus VulkanSwapchain::initialize(const RHIViewportContextDesc& desc, VkSwapchainKHR old_swapchain)
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        RHIStatus status = make_swapchain_status(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device, vk_surface, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if (!status)
        {
            return status;
        }
        if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0)
        {
            return RHIStatus::failure(
                RHIErrorCode::NotReady,
                "The Vulkan presentation surface has a zero extent and cannot create a swapchain yet.");
        }

        const VkFormat requested_format = vulkan_format_from_pixel_format(desc.format);
        if (requested_format == VK_FORMAT_UNDEFINED)
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported,
                                      "The requested RHI viewport format has no Vulkan mapping.");
        }

        std::uint32_t format_count = 0;
        status = make_swapchain_status(
            vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, vk_surface, &format_count, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (!status)
        {
            return status;
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        status = make_swapchain_status(
            vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, vk_surface, &format_count, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (!status)
        {
            return status;
        }
        const auto format_it =
            std::find_if(formats.begin(), formats.end(), [requested_format](const VkSurfaceFormatKHR& surface_format)
                         { return surface_format.format == requested_format; });
        if (format_it == formats.end())
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported,
                                      "The primary surface does not support the requested viewport format.");
        }

        std::uint32_t present_mode_count = 0;
        status = make_swapchain_status(
            vkGetPhysicalDeviceSurfacePresentModesKHR(vk_physical_device, vk_surface, &present_mode_count, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
        if (!status)
        {
            return status;
        }
        std::vector<VkPresentModeKHR> present_modes(present_mode_count);
        status = make_swapchain_status(vkGetPhysicalDeviceSurfacePresentModesKHR(
                                           vk_physical_device, vk_surface, &present_mode_count, present_modes.data()),
                                       "vkGetPhysicalDeviceSurfacePresentModesKHR");
        if (!status)
        {
            return status;
        }
        const VkPresentModeKHR present_mode = to_vk_present_mode(desc.present_mode);
        if (std::find(present_modes.begin(), present_modes.end(), present_mode) == present_modes.end())
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported,
                                      "The primary surface does not support the requested present mode.");
        }

        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            // std::clamp applies Vulkan's inclusive surface limits directly and
            // keeps both dimensions on the same readable C++17 path.
            extent.width = std::clamp(desc.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height =
                std::clamp(desc.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0)
        {
            return RHIStatus::failure(
                RHIErrorCode::NotReady,
                "The Vulkan presentation surface has a zero extent and cannot create a swapchain yet.");
        }

        std::uint32_t requested_image_count = std::max(desc.image_count, capabilities.minImageCount);
        if (capabilities.maxImageCount != 0)
        {
            requested_image_count = std::min(requested_image_count, capabilities.maxImageCount);
        }
        constexpr VkImageUsageFlags required_usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if ((capabilities.supportedUsageFlags & required_usage) != required_usage)
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported,
                                      "The primary surface does not support the required presentation image usage.");
        }

        VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        constexpr VkCompositeAlphaFlagBitsKHR alpha_candidates[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
        for (VkCompositeAlphaFlagBitsKHR candidate : alpha_candidates)
        {
            if ((capabilities.supportedCompositeAlpha & candidate) != 0)
            {
                composite_alpha = candidate;
                break;
            }
        }

        VkSwapchainCreateInfoKHR create_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        create_info.surface = vk_surface;
        create_info.minImageCount = requested_image_count;
        create_info.imageFormat = format_it->format;
        create_info.imageColorSpace = format_it->colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = required_usage;
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.preTransform = capabilities.currentTransform;
        create_info.compositeAlpha = composite_alpha;
        create_info.presentMode = present_mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = old_swapchain;
        status = make_swapchain_status(vkCreateSwapchainKHR(vk_device, &create_info, nullptr, &vk_swapchain),
                                       "vkCreateSwapchainKHR");
        if (!status)
        {
            vk_swapchain = VK_NULL_HANDLE;
            return status;
        }

        std::uint32_t actual_image_count = 0;
        status = make_swapchain_status(vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &actual_image_count, nullptr),
                                       "vkGetSwapchainImagesKHR");
        if (!status)
        {
            return status;
        }
        std::vector<VkImage> images(actual_image_count);
        status =
            make_swapchain_status(vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &actual_image_count, images.data()),
                                  "vkGetSwapchainImagesKHR");
        if (!status)
        {
            return status;
        }

        swapchain_images.resize(actual_image_count);
        swapchain_extent = extent;
        swapchain_format = format_it->format;
        for (std::uint32_t index = 0; index < actual_image_count; ++index)
        {
            VulkanSwapchainImage& swapchain_image = swapchain_images[index];
            swapchain_image.image = images[index];

            VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view_info.image = swapchain_image.image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = swapchain_format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.layerCount = 1;
            status = make_swapchain_status(
                vkCreateImageView(vk_device, &view_info, nullptr, &swapchain_image.image_view), "vkCreateImageView");
            if (!status)
            {
                return status;
            }

            RHITextureDesc texture_desc;
            texture_desc.width = extent.width;
            texture_desc.height = extent.height;
            texture_desc.format = desc.format;
            texture_desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::CopyDestination;
            texture_desc.initial_access = RHIAccess::Present;
            texture_desc.debug_name = desc.debug_name + ".Image" + std::to_string(index);
            swapchain_image.texture =
                std::make_shared<VulkanTexture>(owner_device, std::move(texture_desc), swapchain_image.image,
                                                VK_IMAGE_LAYOUT_UNDEFINED, RHIAccess::Present);

            RHITextureViewDesc view_desc;
            view_desc.type = RHIResourceViewType::RenderTarget;
            view_desc.dimension = RHITextureViewDimension::Texture2D;
            view_desc.format = desc.format;
            view_desc.debug_name = desc.debug_name + ".View" + std::to_string(index);
            swapchain_image.view = std::make_shared<VulkanTextureView>(swapchain_image.texture, std::move(view_desc),
                                                                       vk_device, swapchain_image.image_view, false);

            VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            status = make_swapchain_status(
                vkCreateSemaphore(vk_device, &semaphore_info, nullptr, &swapchain_image.rendering_done),
                "vkCreateSemaphore");
            if (!status)
            {
                return status;
            }
        }
        return RHIStatus::success();
    }

    RHIResult<VulkanAcquireResult> VulkanSwapchain::acquire_image(VkSemaphore image_acquired)
    {
        std::uint32_t image_index = 0;
        const VkResult result =
            vkAcquireNextImageKHR(vk_device, vk_swapchain, UINT64_MAX, image_acquired, VK_NULL_HANDLE, &image_index);
        const RHIStatus status = map_vulkan_acquire_result(result);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            return RHIResult<VulkanAcquireResult>::failure(status.code(), status.message());
        }
        if (image_index >= swapchain_images.size())
        {
            return RHIResult<VulkanAcquireResult>::failure(
                RHIErrorCode::BackendFailure,
                "vkAcquireNextImageKHR returned an image index outside the active swapchain.");
        }
        return RHIResult<VulkanAcquireResult>::success({image_index, status});
    }

    RHIStatus VulkanSwapchain::present(VkQueue queue, std::uint32_t image_index, VkSemaphore rendering_done)
    {
        if (image_index >= swapchain_images.size() || rendering_done == VK_NULL_HANDLE)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Vulkan swapchain present requires a valid image and rendering semaphore.");
        }
        VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &rendering_done;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &vk_swapchain;
        present_info.pImageIndices = &image_index;
        return map_vulkan_present_result(vkQueuePresentKHR(queue, &present_info));
    }

    VkSwapchainKHR VulkanSwapchain::native_handle() const
    {
        return vk_swapchain;
    }

    VkExtent2D VulkanSwapchain::extent() const
    {
        return swapchain_extent;
    }

    VkFormat VulkanSwapchain::format() const
    {
        return swapchain_format;
    }

    std::uint32_t VulkanSwapchain::image_count() const
    {
        return static_cast<std::uint32_t>(swapchain_images.size());
    }

    VulkanSwapchainImage& VulkanSwapchain::image(std::uint32_t image_index)
    {
        return swapchain_images.at(image_index);
    }

    const VulkanSwapchainImage& VulkanSwapchain::image(std::uint32_t image_index) const
    {
        return swapchain_images.at(image_index);
    }
} // namespace toy3d
