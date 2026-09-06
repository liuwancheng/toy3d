#include "drivers/vulkan/vulkan_resource_creation.h"

#include "drivers/vulkan/vulkan_deferred_deletion.h"
#include "drivers/vulkan/vulkan_memory_manager.h"
#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_type_mapping.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>
#include <vector>

namespace toy3d
{
    RHIResult<RHIBufferRef> create_vulkan_buffer(const RHIDevice& owner, VkDevice device,
                                                 VulkanMemoryManager& memory_manager,
                                                 VulkanDeferredDeletionQueue& deletion_queue, const RHIBufferDesc& desc,
                                                 const RHIInitialData* initial_data)
    {
        if (device == VK_NULL_HANDLE)
        {
            return RHIResult<RHIBufferRef>::failure(RHIErrorCode::NotReady,
                                                    "Vulkan buffer creation requires a valid logical device.");
        }
        (void)initial_data;
        if (desc.cpu_access != RHICPUAccess::None)
        {
            return RHIResult<RHIBufferRef>::failure(RHIErrorCode::Unsupported,
                                                    "Vulkan CPU-accessible buffers are not implemented yet.");
        }
        if (desc.initial_access != RHIAccess::Unknown && desc.initial_access != RHIAccess::Common)
        {
            return RHIResult<RHIBufferRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan buffer creation currently supports only Unknown or Common initial access.");
        }

        const VkBufferUsageFlags usage = to_vk_buffer_usage(desc.usage);
        if (usage == 0)
        {
            return RHIResult<RHIBufferRef>::failure(
                RHIErrorCode::InvalidArgument, "Vulkan buffer creation requires at least one supported usage flag.");
        }
        VkBufferCreateInfo create_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        create_info.size = desc.size;
        create_info.usage = usage;
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        auto allocated_buffer =
            memory_manager.create_buffer(create_info, VulkanAllocationUsage::GpuOnly, desc.debug_name.c_str());
        if (!allocated_buffer)
        {
            return RHIResult<RHIBufferRef>::failure(allocated_buffer.status().code(),
                                                    allocated_buffer.status().message());
        }
        return RHIResult<RHIBufferRef>::success(std::make_shared<VulkanBuffer>(
            owner, desc, memory_manager, deletion_queue, std::move(allocated_buffer.value()), desc.initial_access));
    }

    RHIResult<RHITextureRef> create_vulkan_texture(const RHIDevice& owner, VkPhysicalDevice physical_device,
                                                   VkDevice device, VulkanMemoryManager& memory_manager,
                                                   VulkanDeferredDeletionQueue& deletion_queue,
                                                   const RHITextureDesc& desc, const RHIInitialData* initial_data)
    {
        if (physical_device == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::NotReady, "Vulkan texture creation requires valid physical and logical devices.");
        }
        (void)initial_data;
        if (desc.cpu_access != RHICPUAccess::None)
        {
            return RHIResult<RHITextureRef>::failure(RHIErrorCode::Unsupported,
                                                     "Vulkan CPU-accessible textures are not implemented yet.");
        }
        if (desc.initial_access != RHIAccess::Unknown && desc.initial_access != RHIAccess::Common)
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan texture creation currently supports only Unknown or Common initial access.");
        }

        const VkFormat format = vulkan_format_from_pixel_format(desc.format);
        if (format == VK_FORMAT_UNDEFINED)
        {
            return RHIResult<RHITextureRef>::failure(RHIErrorCode::Unsupported,
                                                     "The requested RHI texture format has no Vulkan mapping.");
        }
        const VkImageUsageFlags usage = to_vk_image_usage(desc.usage);
        if (usage == 0)
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::InvalidArgument, "Vulkan texture creation requires at least one supported usage flag.");
        }
        const auto image_type = to_vk_image_type(desc.dimension);
        if (!image_type)
        {
            return RHIResult<RHITextureRef>::failure(image_type.status().code(), image_type.status().message());
        }
        const auto sample_count = to_vk_sample_count(desc.sample_count);
        if (!sample_count)
        {
            return RHIResult<RHITextureRef>::failure(sample_count.status().code(), sample_count.status().message());
        }

        VkImageFormatProperties image_format_properties{};
        const VkResult format_properties_result = vkGetPhysicalDeviceImageFormatProperties(
            physical_device, format, image_type.value(), VK_IMAGE_TILING_OPTIMAL, usage, 0, &image_format_properties);
        if (format_properties_result == VK_ERROR_FORMAT_NOT_SUPPORTED ||
            (format_properties_result == VK_SUCCESS &&
             (image_format_properties.sampleCounts & sample_count.value()) == 0))
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan does not support the requested texture format, combined usage, and sample count.");
        }
        if (format_properties_result != VK_SUCCESS)
        {
            return RHIResult<RHITextureRef>::failure(
                vulkan_status_from_result(format_properties_result, "vkGetPhysicalDeviceImageFormatProperties").code(),
                "vkGetPhysicalDeviceImageFormatProperties failed while validating texture support.");
        }

        VkImageCreateInfo create_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        create_info.imageType = image_type.value();
        create_info.format = format;
        create_info.extent = {desc.width, desc.height, desc.depth};
        create_info.mipLevels = desc.mip_levels;
        create_info.arrayLayers = desc.array_layers;
        create_info.samples = sample_count.value();
        create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        create_info.usage = usage;
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        auto allocated_image =
            memory_manager.create_image(create_info, VulkanAllocationUsage::GpuOnly, desc.debug_name.c_str());
        if (!allocated_image)
        {
            return RHIResult<RHITextureRef>::failure(allocated_image.status().code(),
                                                     allocated_image.status().message());
        }
        return RHIResult<RHITextureRef>::success(std::make_shared<VulkanTexture>(
            owner, desc, memory_manager, deletion_queue, std::move(allocated_image.value()), VK_IMAGE_LAYOUT_UNDEFINED,
            desc.initial_access));
    }

    RHIResult<RHIBufferViewRef> create_vulkan_buffer_view(const RHIBufferRef& buffer, const RHIBufferViewDesc& desc)
    {
        (void)buffer;
        (void)desc;
        return RHIResult<RHIBufferViewRef>::failure(
            RHIErrorCode::Unsupported,
            "Vulkan buffer views require descriptor binding support, which is not implemented yet.");
    }

    RHIResult<RHITextureViewRef> create_vulkan_texture_view(VkDevice device, const RHITextureRef& texture,
                                                            const RHITextureViewDesc& desc)
    {
        if (device == VK_NULL_HANDLE)
        {
            return RHIResult<RHITextureViewRef>::failure(
                RHIErrorCode::NotReady, "Vulkan texture-view creation requires a valid logical device.");
        }
        const auto vulkan_texture = std::dynamic_pointer_cast<VulkanTexture>(texture);
        if (!vulkan_texture)
        {
            return RHIResult<RHITextureViewRef>::failure(
                RHIErrorCode::InvalidArgument, "Vulkan texture views require a texture created by the Vulkan device.");
        }
        const VkFormat view_format = vulkan_format_from_pixel_format(desc.format);
        if (view_format == VK_FORMAT_UNDEFINED ||
            view_format != vulkan_format_from_pixel_format(texture->desc().format))
        {
            return RHIResult<RHITextureViewRef>::failure(
                RHIErrorCode::Unsupported, "Vulkan texture views currently require the texture's original format.");
        }
        const auto view_type = to_vk_image_view_type(desc);
        if (!view_type)
        {
            return RHIResult<RHITextureViewRef>::failure(view_type.status().code(), view_type.status().message());
        }
        const auto aspect = to_vk_image_aspect(desc.subresources.aspect, view_format);
        if (!aspect)
        {
            return RHIResult<RHITextureViewRef>::failure(aspect.status().code(), aspect.status().message());
        }
        if (desc.type == RHIResourceViewType::DepthStencil)
        {
            const RHITextureAspect required_aspect =
                is_vk_stencil_format(view_format) ? RHITextureAspect::DepthStencil : RHITextureAspect::Depth;
            if (desc.subresources.aspect != required_aspect)
            {
                return RHIResult<RHITextureViewRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan depth-stencil views must select every aspect present in the attachment format.");
            }
            if (is_vk_stencil_format(view_format) && desc.depth_read_only != desc.stencil_read_only)
            {
                return RHIResult<RHITextureViewRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan ES3.1 profile does not require separate depth and stencil layouts; "
                    "mixed read-only and writable aspects are unsupported by this backend path.");
            }
        }
        const std::uint32_t mip_count = desc.subresources.mip_count == RHI_ALL_MIPS
                                            ? texture->desc().mip_levels - desc.subresources.first_mip
                                            : desc.subresources.mip_count;
        const std::uint32_t layer_count = desc.subresources.layer_count == RHI_ALL_LAYERS
                                              ? texture->desc().array_layers - desc.subresources.first_layer
                                              : desc.subresources.layer_count;
        VkImageViewCreateInfo create_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        create_info.image = vulkan_texture->image();
        create_info.viewType = view_type.value();
        create_info.format = view_format;
        create_info.subresourceRange.aspectMask = aspect.value();
        create_info.subresourceRange.baseMipLevel = desc.subresources.first_mip;
        create_info.subresourceRange.levelCount = mip_count;
        create_info.subresourceRange.baseArrayLayer = desc.subresources.first_layer;
        create_info.subresourceRange.layerCount = layer_count;
        VkImageView image_view = VK_NULL_HANDLE;
        const RHIStatus status = vulkan_status_from_result(
            vkCreateImageView(device, &create_info, nullptr, &image_view), "vkCreateImageView");
        if (!status)
        {
            return RHIResult<RHITextureViewRef>::failure(status.code(), status.message());
        }
        return RHIResult<RHITextureViewRef>::success(
            std::make_shared<VulkanTextureView>(texture, desc, device, image_view, true));
    }

    RHIResult<RHIShaderRef> create_vulkan_shader(const RHIDevice& owner, VkDevice device, const RHIShaderDesc& desc)
    {
        if (device == VK_NULL_HANDLE)
        {
            return RHIResult<RHIShaderRef>::failure(RHIErrorCode::NotReady,
                                                    "Vulkan shader creation requires a valid logical device.");
        }
        std::string target = desc.bytecode.target;
        std::transform(target.begin(), target.end(), target.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (target != "spirv" && target != "spir-v")
        {
            return RHIResult<RHIShaderRef>::failure(RHIErrorCode::Unsupported,
                                                    "Vulkan shaders require SPIR-V bytecode.");
        }
        if (desc.bytecode.bytes.size() % sizeof(std::uint32_t) != 0)
        {
            return RHIResult<RHIShaderRef>::failure(RHIErrorCode::InvalidArgument,
                                                    "SPIR-V bytecode size must be a multiple of four bytes.");
        }
        std::vector<std::uint32_t> words(desc.bytecode.bytes.size() / sizeof(std::uint32_t));
        std::memcpy(words.data(), desc.bytecode.bytes.data(), desc.bytecode.bytes.size());
        VkShaderModuleCreateInfo create_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        create_info.codeSize = desc.bytecode.bytes.size();
        create_info.pCode = words.data();
        VkShaderModule shader_module = VK_NULL_HANDLE;
        const RHIStatus status = vulkan_status_from_result(
            vkCreateShaderModule(device, &create_info, nullptr, &shader_module), "vkCreateShaderModule");
        if (!status)
        {
            return RHIResult<RHIShaderRef>::failure(status.code(), status.message());
        }
        return RHIResult<RHIShaderRef>::success(std::make_shared<VulkanShader>(owner, desc, device, shader_module));
    }

    RHIResult<RHISamplerRef> create_vulkan_sampler(const RHIDevice& owner, VkDevice device, const RHISamplerDesc& desc)
    {
        if (device == VK_NULL_HANDLE)
        {
            return RHIResult<RHISamplerRef>::failure(RHIErrorCode::NotReady,
                                                     "Vulkan sampler creation requires a valid logical device.");
        }
        VkSamplerCreateInfo create_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        create_info.magFilter = to_vk_filter(desc.mag_filter);
        create_info.minFilter = to_vk_filter(desc.min_filter);
        create_info.mipmapMode = to_vk_mipmap_mode(desc.mip_filter);
        create_info.addressModeU = to_vk_address_mode(desc.address_u);
        create_info.addressModeV = to_vk_address_mode(desc.address_v);
        create_info.addressModeW = to_vk_address_mode(desc.address_w);
        create_info.mipLodBias = desc.mip_lod_bias;
        create_info.anisotropyEnable = desc.max_anisotropy > 1 ? VK_TRUE : VK_FALSE;
        create_info.maxAnisotropy = static_cast<float>(desc.max_anisotropy);
        create_info.compareEnable = desc.compare_enable ? VK_TRUE : VK_FALSE;
        create_info.compareOp = to_vk_compare_operation(desc.compare_operation);
        create_info.minLod = desc.min_lod;
        create_info.maxLod = desc.max_lod;
        create_info.borderColor = to_vk_border_color(desc.border_color);
        VkSampler sampler = VK_NULL_HANDLE;
        const RHIStatus create_status =
            vulkan_status_from_result(vkCreateSampler(device, &create_info, nullptr, &sampler), "vkCreateSampler");
        if (!create_status)
        {
            return RHIResult<RHISamplerRef>::failure(create_status.code(), create_status.message());
        }
        return RHIResult<RHISamplerRef>::success(std::make_shared<VulkanSampler>(owner, desc, device, sampler));
    }

    RHIResult<RHIGPUFenceRef> create_vulkan_gpu_fence(const std::string&)
    {
        return RHIResult<RHIGPUFenceRef>::failure(RHIErrorCode::Unsupported,
                                                  "Vulkan GPU fences are not implemented yet.");
    }
} // namespace toy3d
