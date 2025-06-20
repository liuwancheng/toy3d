#include "vulkan_texture.h"
#include "vulkan_context.h"
#include "core/misc/log.h"
#include <algorithm>

namespace toy3d
{
    bool g_vulkan_submit_on_texture_update = false; // 在纹理更新时是否触发当前comandbuffer的提交

    //=============================================================================
    // VulkanTexture Base Class Implementation
    //=============================================================================

    VulkanTexture::VulkanTexture(uint32_t mips, uint32_t samples, EPixelFormat format, ETextureCreateFlags flags, const ClearValueBinding& clear_value)
        : RHITexture(mips, samples, format, flags, clear_value)
    {
        vk_format = convert_pixel_format_to_vk(format);
    }

    VulkanTexture::~VulkanTexture()
    {
        // Resources should be destroyed explicitly via destroy() before destruction
    }

    void VulkanTexture::create_texture(VulkanContext* context, const VkImageCreateInfo& image_info, VmaMemoryUsage memory_usage)
    {
        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = memory_usage;
        alloc_info.flags = 0;

        // 如果是RenderTarget或DepthStencil，优先使用GPU内存
        if ((flags & ETextureCreateFlags::Tex_RenderTarget) != ETextureCreateFlags::Tex_None ||
            (flags & ETextureCreateFlags::Tex_DepthStencilTarget) != ETextureCreateFlags::Tex_None)
        {
            alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        }

        VkResult result = vmaCreateImage(context->get_vma_allocator(), &image_info, &alloc_info,
                                        &vk_image, &vma_allocation, &allocation_info);
        
        if (result != VK_SUCCESS)
        {
            TOY_LOG_ERROR("Failed to create Vulkan image: {}", cast_vk_error(result));
            return;
        }

        current_layout = image_info.initialLayout;
    }

    void VulkanTexture::create_texture_view(VulkanContext* context, VkImageViewType view_type, VkFormat format,
                                          VkImageAspectFlags aspect_flags, uint32_t base_mip, uint32_t mip_levels,
                                          uint32_t base_layer, uint32_t layer_count)
    {
        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = vk_image;
        view_info.viewType = view_type;
        view_info.format = format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = aspect_flags;
        view_info.subresourceRange.baseMipLevel = base_mip;
        view_info.subresourceRange.levelCount = mip_levels;
        view_info.subresourceRange.baseArrayLayer = base_layer;
        view_info.subresourceRange.layerCount = layer_count;

        VkResult result = vkCreateImageView(context->get_device(), &view_info, nullptr, &vk_image_view);
        if (result != VK_SUCCESS)
        {
            TOY_LOG_ERROR("Failed to create image view: {}", cast_vk_error(result));
        }
    }

    void VulkanTexture::destroy(VulkanContext* context)
    {
        if (vk_image_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(context->get_device(), vk_image_view, nullptr);
            vk_image_view = VK_NULL_HANDLE;
        }

        if (staging_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(context->get_vma_allocator(), staging_buffer, staging_allocation);
            staging_buffer = VK_NULL_HANDLE;
            staging_allocation = VK_NULL_HANDLE;
            has_staging_buffer = false;
        }

        if (vk_image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(context->get_vma_allocator(), vk_image, vma_allocation);
            vk_image = VK_NULL_HANDLE;
            vma_allocation = VK_NULL_HANDLE;
        }
    }

    void VulkanTexture::transition_image_layout(VkCommandBuffer command_buffer, VkImageLayout old_layout, VkImageLayout new_layout,
                                              VkImageAspectFlags aspect_flags)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = vk_image;
        barrier.subresourceRange.aspectMask = aspect_flags;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mips_num;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

        VkPipelineStageFlags source_stage;
        VkPipelineStageFlags destination_stage;

        if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            source_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            // 通用转换
            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            source_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            destination_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        // 直接在提供的命令缓冲区中插入屏障
        vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        current_layout = new_layout;
    }

    void VulkanTexture::create_staging_buffer(VulkanContext* context, uint32_t size)
    {
        if (has_staging_buffer)
            return;

        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        VkResult result = vmaCreateBuffer(context->get_vma_allocator(), &buffer_info, &alloc_info,
                                         &staging_buffer, &staging_allocation, nullptr);
        
        if (result == VK_SUCCESS)
        {
            has_staging_buffer = true;
        }
        else
        {
            TOY_LOG_ERROR("Failed to create staging buffer: {}", cast_vk_error(result));
        }
    }

    void VulkanTexture::copy_buffer_to_image(VulkanContext* context, VkBuffer buffer, uint32_t width, uint32_t height,
                                           uint32_t depth, uint32_t mip_level, uint32_t array_layer)
    {
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mip_level;
        region.imageSubresource.baseArrayLayer = array_layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, depth};

        vkCmdCopyBufferToImage(command_buffer, buffer, vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    VkFormat VulkanTexture::convert_pixel_format_to_vk(EPixelFormat format)
    {
        switch (format)
        {
        case EPixelFormat::A32B32G32R32F:   return VK_FORMAT_R32G32B32A32_SFLOAT;
        case EPixelFormat::B8G8R8A8:        return VK_FORMAT_B8G8R8A8_UNORM;
        case EPixelFormat::R8G8B8A8:        return VK_FORMAT_R8G8B8A8_UNORM;
        case EPixelFormat::A8R8G8B8:        return VK_FORMAT_B8G8R8A8_UNORM; // 需要swizzle
        case EPixelFormat::G8:              return VK_FORMAT_R8_UNORM;
        case EPixelFormat::G16:             return VK_FORMAT_R16_UNORM;
        case EPixelFormat::R32_Float:       return VK_FORMAT_R32_SFLOAT;
        case EPixelFormat::G16R16:          return VK_FORMAT_R16G16_UNORM;
        case EPixelFormat::G16R16F:         return VK_FORMAT_R16G16_SFLOAT;
        case EPixelFormat::G32R32F:         return VK_FORMAT_R32G32_SFLOAT;
        case EPixelFormat::A16G16B16R16:    return VK_FORMAT_R16G16B16A16_UNORM;
        case EPixelFormat::R16G16B16A16:    return VK_FORMAT_R16G16B16A16_UNORM;
        case EPixelFormat::A8:              return VK_FORMAT_R8_UNORM;
        case EPixelFormat::R32_UINT:        return VK_FORMAT_R32_UINT;
        case EPixelFormat::FloatRGB:        return VK_FORMAT_R32G32B32_SFLOAT;
        case EPixelFormat::FloatRGBA:       return VK_FORMAT_R32G32B32A32_SFLOAT;
        case EPixelFormat::DepthStencil:    return VK_FORMAT_D24_UNORM_S8_UINT;
        case EPixelFormat::ShadowDepth:     return VK_FORMAT_D32_SFLOAT;
        case EPixelFormat::Depth24:         return VK_FORMAT_D24_UNORM_S8_UINT;
        case EPixelFormat::FloatR11G11B10:  return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case EPixelFormat::A2B10G10R10:     return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case EPixelFormat::R8G8B8A8_SNORM:  return VK_FORMAT_R8G8B8A8_SNORM;
        case EPixelFormat::DXT1:            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case EPixelFormat::DXT3:            return VK_FORMAT_BC2_UNORM_BLOCK;
        case EPixelFormat::DXT5:            return VK_FORMAT_BC3_UNORM_BLOCK;
        default:
            LOG_WARNING("Unsupported pixel format: {}", static_cast<int>(format));
            return VK_FORMAT_R8G8B8A8_UNORM;
        }
    }

    VkImageUsageFlags VulkanTexture::convert_texture_flags_to_vk_usage(ETextureCreateFlags flags)
    {
        VkImageUsageFlags usage = 0;
        
        if ((flags & ETextureCreateFlags::Tex_ShaderResource) != ETextureCreateFlags::Tex_None)
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            
        if ((flags & ETextureCreateFlags::Tex_RenderTarget) != ETextureCreateFlags::Tex_None)
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            
        if ((flags & ETextureCreateFlags::Tex_DepthStencilTarget) != ETextureCreateFlags::Tex_None)
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            
        if ((flags & ETextureCreateFlags::Tex_UAV) != ETextureCreateFlags::Tex_None)
            usage |= VK_IMAGE_USAGE_STORAGE_BIT;
            
        if ((flags & ETextureCreateFlags::Tex_Dynamic) != ETextureCreateFlags::Tex_None)
            usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            
        // 默认添加transfer用途以支持数据拷贝
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        
        return usage;
    }

    VkImageAspectFlags VulkanTexture::get_aspect_flags_from_format(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
            
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    uint32_t VulkanTexture::get_pixel_size(EPixelFormat format)
    {
        switch (format)
        {
        case EPixelFormat::A32B32G32R32F:   return 16;
        case EPixelFormat::B8G8R8A8:        return 4;
        case EPixelFormat::R8G8B8A8:        return 4;
        case EPixelFormat::A8R8G8B8:        return 4;
        case EPixelFormat::G8:              return 1;
        case EPixelFormat::G16:             return 2;
        case EPixelFormat::R32_Float:       return 4;
        case EPixelFormat::G16R16:          return 4;
        case EPixelFormat::G16R16F:         return 4;
        case EPixelFormat::G32R32F:         return 8;
        case EPixelFormat::A16G16B16R16:    return 8;
        case EPixelFormat::R16G16B16A16:    return 8;
        case EPixelFormat::A8:              return 1;
        case EPixelFormat::R32_UINT:        return 4;
        case EPixelFormat::FloatRGB:        return 12;
        case EPixelFormat::FloatRGBA:       return 16;
        case EPixelFormat::DepthStencil:    return 4;
        case EPixelFormat::ShadowDepth:     return 4;
        case EPixelFormat::Depth24:         return 4;
        case EPixelFormat::FloatR11G11B10:  return 4;
        case EPixelFormat::A2B10G10R10:     return 4;
        case EPixelFormat::R8G8B8A8_SNORM:  return 4;
        default:                            return 4;
        }
    }

    //=============================================================================
    // VulkanTexture2D Implementation
    //=============================================================================

    VulkanTexture2D::VulkanTexture2D(uint32_t x, uint32_t y, uint32_t mips, uint32_t samples, EPixelFormat format,
                                   ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info)
        : VulkanTexture(mips, samples, format, flags, create_info.clear_value)
        , size_x(x)
        , size_y(y)
    {
        // 构造函数中不直接创建Vulkan资源，留给外部调用
    }

    VulkanTexture2D::~VulkanTexture2D()
    {
    }

    void VulkanTexture2D::create_vk_texture(VulkanContext* context, const RHIResourceCreateInfo& create_info)
    {
        VkImageCreateInfo image_info = {};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = size_x;
        image_info.extent.height = size_y;
        image_info.extent.depth = 1;
        image_info.mipLevels = mips_num;
        image_info.arrayLayers = 1;
        image_info.format = vk_format;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = convert_texture_flags_to_vk_usage(flags);
        image_info.samples = samples_num > 1 ? static_cast<VkSampleCountFlagBits>(samples_num) : VK_SAMPLE_COUNT_1_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if ((flags & ETextureCreateFlags::Tex_Dynamic) != ETextureCreateFlags::Tex_None)
        {
            memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        }

        create_texture(context, image_info, memory_usage);

        // 创建纹理视图
        VkImageAspectFlags aspect = get_aspect_flags_from_format(vk_format);
        create_texture_view(context, VK_IMAGE_VIEW_TYPE_2D, vk_format, aspect, 0, mips_num, 0, 1);

        // 如果有初始数据，更新纹理
        if (create_info.bulk_data != nullptr)
        {
            uint32_t texture_size = get_texture_size();
            update_texture_data(context, create_info.bulk_data, texture_size, 0);
        }
    }

    uint32_t VulkanTexture2D::get_texture_size() const
    {
        uint32_t pixel_size = get_pixel_size(format);
        return size_x * size_y * pixel_size;
    }

    void VulkanTexture2D::update_texture_data(VulkanContext* context, const void* data, uint32_t size, uint32_t mip_level)
    {
        if (!data || size == 0)
            return;

        // 创建staging buffer
        create_staging_buffer(context, size);
        if (!has_staging_buffer)
            return;

        // 拷贝数据到staging buffer
        void* mapped_data;
        vmaMapMemory(context->get_vma_allocator(), staging_allocation, &mapped_data);
        memcpy(mapped_data, data, size);
        vmaUnmapMemory(context->get_vma_allocator(), staging_allocation);

        // 转换布局为传输目标
        VkImageLayout old_layout = current_layout;
        if (current_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            transition_image_layout(context, current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        // 计算mip level的尺寸
        uint32_t mip_width = std::max(1u, size_x >> mip_level);
        uint32_t mip_height = std::max(1u, size_y >> mip_level);

        // 拷贝staging buffer到纹理
        copy_buffer_to_image(context, staging_buffer, mip_width, mip_height, 1, mip_level, 0);

        // 转换回shader读取布局
        if ((flags & ETextureCreateFlags::Tex_ShaderResource) != ETextureCreateFlags::Tex_None)
        {
            transition_image_layout(context, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if(g_vulkan_submit_on_texture_update)
        {
            context->submit_current_command_buffer();
        }
    }

    void VulkanTexture2D::update_texture_region(VulkanContext* context, const void* data, uint32_t x_offset, uint32_t y_offset,
                                               uint32_t width, uint32_t height, uint32_t mip_level)
    {
        // 实现区域更新，这里暂时使用完整更新
        update_texture_data(context, data, width * height * get_pixel_size(format), mip_level);
    }

    //=============================================================================
    // VulkanTexture2DArray Implementation
    //=============================================================================

    VulkanTexture2DArray::VulkanTexture2DArray(uint32_t x, uint32_t y, uint32_t z, uint32_t mips, uint32_t samples, EPixelFormat format,
                                             ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info)
        : VulkanTexture2D(x, y, mips, samples, format, flags, create_info)
        , depth(z)
    {
    }

    VulkanTexture2DArray::~VulkanTexture2DArray()
    {
    }

    void VulkanTexture2DArray::create_vk_texture_array(VulkanContext* context, const RHIResourceCreateInfo& create_info)
    {
        VkImageCreateInfo image_info = {};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = size_x;
        image_info.extent.height = size_y;
        image_info.extent.depth = 1;
        image_info.mipLevels = mips_num;
        image_info.arrayLayers = depth;
        image_info.format = vk_format;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = convert_texture_flags_to_vk_usage(flags);
        image_info.samples = samples_num > 1 ? static_cast<VkSampleCountFlagBits>(samples_num) : VK_SAMPLE_COUNT_1_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;
        create_texture(context, image_info, memory_usage);

        // 创建纹理视图
        VkImageAspectFlags aspect = get_aspect_flags_from_format(vk_format);
        create_texture_view(context, VK_IMAGE_VIEW_TYPE_2D_ARRAY, vk_format, aspect, 0, mips_num, 0, depth);
    }

    uint32_t VulkanTexture2DArray::get_texture_size() const
    {
        uint32_t pixel_size = get_pixel_size(format);
        return size_x * size_y * depth * pixel_size;
    }

    void VulkanTexture2DArray::update_array_layer(VulkanContext* context, const void* data, uint32_t array_layer, uint32_t mip_level)
    {
        if (!data || array_layer >= depth)
            return;

        uint32_t layer_size = size_x * size_y * get_pixel_size(format);
        create_staging_buffer(context, layer_size);
        
        if (!has_staging_buffer)
            return;

        // 拷贝数据到staging buffer
        void* mapped_data;
        vmaMapMemory(context->get_vma_allocator(), staging_allocation, &mapped_data);
        memcpy(mapped_data, data, layer_size);
        vmaUnmapMemory(context->get_vma_allocator(), staging_allocation);

        // 转换布局
        if (current_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            transition_image_layout(context, current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        // 拷贝到指定数组层
        copy_buffer_to_image(context, staging_buffer, size_x, size_y, 1, mip_level, array_layer);

        // 转换回shader读取布局
        if ((flags & ETextureCreateFlags::Tex_ShaderResource) != ETextureCreateFlags::Tex_None)
        {
            transition_image_layout(context, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if(g_vulkan_submit_on_texture_update)
        {
            context->submit_current_command_buffer();
        }
    }

    //=============================================================================
    // VulkanTexture3D Implementation
    //=============================================================================

    VulkanTexture3D::VulkanTexture3D(uint32_t x, uint32_t y, uint32_t z, uint32_t mips, EPixelFormat format,
                                   ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info)
        : VulkanTexture(mips, 1, format, flags, create_info.clear_value)
        , size_x(x)
        , size_y(y)
        , depth(z)
    {
    }

    VulkanTexture3D::~VulkanTexture3D()
    {
    }

    void VulkanTexture3D::create_vk_texture3d(VulkanContext* context, const RHIResourceCreateInfo& create_info)
    {
        VkImageCreateInfo image_info = {};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_3D;
        image_info.extent.width = size_x;
        image_info.extent.height = size_y;
        image_info.extent.depth = depth;
        image_info.mipLevels = mips_num;
        image_info.arrayLayers = 1;
        image_info.format = vk_format;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = convert_texture_flags_to_vk_usage(flags);
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;
        create_texture(context, image_info, memory_usage);

        // 创建纹理视图
        VkImageAspectFlags aspect = get_aspect_flags_from_format(vk_format);
        create_texture_view(context, VK_IMAGE_VIEW_TYPE_3D, vk_format, aspect, 0, mips_num, 0, 1);

        // 如果有初始数据，更新纹理
        if (create_info.bulk_data != nullptr)
        {
            uint32_t texture_size = get_texture_size();
            update_texture_data(context, create_info.bulk_data, texture_size, 0);
        }
    }

    uint32_t VulkanTexture3D::get_texture_size() const
    {
        uint32_t pixel_size = get_pixel_size(format);
        return size_x * size_y * depth * pixel_size;
    }

    void VulkanTexture3D::update_texture_data(VulkanContext* context, const void* data, uint32_t size, uint32_t mip_level)
    {
        if (!data || size == 0)
            return;

        // 创建staging buffer
        create_staging_buffer(context, size);
        if (!has_staging_buffer)
            return;

        // 拷贝数据到staging buffer
        void* mapped_data;
        vmaMapMemory(context->get_vma_allocator(), staging_allocation, &mapped_data);
        memcpy(mapped_data, data, size);
        vmaUnmapMemory(context->get_vma_allocator(), staging_allocation);

        // 转换布局为传输目标
        if (current_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            transition_image_layout(context, current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        // 计算mip level的尺寸
        uint32_t mip_width = std::max(1u, size_x >> mip_level);
        uint32_t mip_height = std::max(1u, size_y >> mip_level);
        uint32_t mip_depth = std::max(1u, depth >> mip_level);

        // 拷贝staging buffer到纹理
        copy_buffer_to_image(context, staging_buffer, mip_width, mip_height, mip_depth, mip_level, 0);

        // 转换回shader读取布局
        if ((flags & ETextureCreateFlags::Tex_ShaderResource) != ETextureCreateFlags::Tex_None)
        {
            transition_image_layout(context, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if(g_vulkan_submit_on_texture_update)
        {
            context->submit_current_command_buffer();
        }
    }

    //=============================================================================
    // VulkanTextureCube Implementation
    //=============================================================================

    VulkanTextureCube::VulkanTextureCube(uint32_t x, uint32_t mips, EPixelFormat format,
                                       ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info)
        : VulkanTexture(mips, 1, format, flags, create_info)
        , size(x)
    {
    }

    VulkanTextureCube::~VulkanTextureCube()
    {
    }

    void VulkanTextureCube::create_vk_texture_cube(VulkanContext* context, const RHIResourceCreateInfo& create_info)
    {
        VkImageCreateInfo image_info = {};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = size;
        image_info.extent.height = size;
        image_info.extent.depth = 1;
        image_info.mipLevels = mips_num;
        image_info.arrayLayers = 6; // Cube has 6 faces
        image_info.format = vk_format;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = convert_texture_flags_to_vk_usage(flags);
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;
        create_texture(context, image_info, memory_usage);

        // 创建纹理视图
        VkImageAspectFlags aspect = get_aspect_flags_from_format(vk_format);
        create_texture_view(context, VK_IMAGE_VIEW_TYPE_CUBE, vk_format, aspect, 0, mips_num, 0, 6);

        // 如果有初始数据，更新纹理
        if (create_info.bulk_data != nullptr)
        {
            uint32_t texture_size = get_texture_size();
            update_texture_data(context, create_info.bulk_data, texture_size, 0);
        }
    }

    uint32_t VulkanTextureCube::get_texture_size() const
    {
        uint32_t pixel_size = get_pixel_size(format);
        return size * size * 6 * pixel_size; // 6 faces
    }

    void VulkanTextureCube::update_texture_data(VulkanContext* context, const void* data, uint32_t data_size, uint32_t mip_level)
    {
        if (!data || data_size == 0)
            return;

        // 创建staging buffer
        create_staging_buffer(context, data_size);
        if (!has_staging_buffer)
            return;

        // 拷贝数据到staging buffer
        void* mapped_data;
        vmaMapMemory(context->get_vma_allocator(), staging_allocation, &mapped_data);
        memcpy(mapped_data, data, data_size);
        vmaUnmapMemory(context->get_vma_allocator(), staging_allocation);

        // 转换布局为传输目标
        if (current_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            transition_image_layout(context, current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        // 计算mip level的尺寸
        uint32_t mip_size = std::max(1u, size >> mip_level);
        uint32_t face_size = mip_size * mip_size * get_pixel_size(format);

        // 拷贝每个面
        for (uint32_t face = 0; face < 6; ++face)
        {
            VkBufferImageCopy region = {};
            region.bufferOffset = face * face_size;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mip_level;
            region.imageSubresource.baseArrayLayer = face;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {mip_size, mip_size, 1};

            vkCmdCopyBufferToImage(command_buffer, staging_buffer, vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }

        // 转换回shader读取布局
        if ((flags & ETextureCreateFlags::Tex_ShaderResource) != ETextureCreateFlags::Tex_None)
        {
            transition_image_layout(context, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if(g_vulkan_submit_on_texture_update)
        {
            context->submit_current_command_buffer();
        }
    }

    void VulkanTextureCube::update_cube_face(VulkanContext* context, const void* data, ECubeFace face, uint32_t mip_level)
    {
        if (!data)
            return;

        uint32_t mip_size = std::max(1u, size >> mip_level);
        uint32_t face_size = mip_size * mip_size * get_pixel_size(format);

        create_staging_buffer(context, face_size);
        if (!has_staging_buffer)
            return;

        // 拷贝数据到staging buffer
        void* mapped_data;
        vmaMapMemory(context->get_vma_allocator(), staging_allocation, &mapped_data);
        memcpy(mapped_data, data, face_size);
        vmaUnmapMemory(context->get_vma_allocator(), staging_allocation);

        // 转换布局
        if (current_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            transition_image_layout(context, current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        // 拷贝到指定面
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mip_level;
        region.imageSubresource.baseArrayLayer = static_cast<uint32_t>(face);
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {mip_size, mip_size, 1};

        vkCmdCopyBufferToImage(command_buffer, staging_buffer, vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        // 转换回shader读取布局
        if ((flags & ETextureCreateFlags::Tex_ShaderResource) != ETextureCreateFlags::Tex_None)
        {
            transition_image_layout_immediate(context, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if(g_vulkan_submit_on_texture_update)
        {
            context->submit_current_command_buffer();
        }
    }

} // namespace toy3d