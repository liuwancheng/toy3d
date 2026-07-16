#include "drivers/vulkan/canonical/vulkan_resource.h"

#include <cstring>
#include <utility>

namespace toy3d
{
    VkFormat vulkan_format_from_rhi(RHIFormat format)
    {
        switch (format)
        {
        case RHIFormat::R8UNorm:
            return VK_FORMAT_R8_UNORM;
        case RHIFormat::R8G8B8A8UNorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R8G8B8A8UNormSRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case RHIFormat::B8G8R8A8UNorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::B8G8R8A8UNormSRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case RHIFormat::R16Float:
            return VK_FORMAT_R16_SFLOAT;
        case RHIFormat::R16G16Float:
            return VK_FORMAT_R16G16_SFLOAT;
        case RHIFormat::R16G16B16A16Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RHIFormat::R32Float:
            return VK_FORMAT_R32_SFLOAT;
        case RHIFormat::R32G32Float:
            return VK_FORMAT_R32G32_SFLOAT;
        case RHIFormat::R32G32B32Float:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case RHIFormat::R32G32B32A32Float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case RHIFormat::R16UInt:
            return VK_FORMAT_R16_UINT;
        case RHIFormat::R32UInt:
            return VK_FORMAT_R32_UINT;
        case RHIFormat::R8SNorm:
            return VK_FORMAT_R8_SNORM;
        case RHIFormat::R8G8B8A8SNorm:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case RHIFormat::R10G10B10A2UNorm:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case RHIFormat::R11G11B10Float:
            return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case RHIFormat::BC1UNorm:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case RHIFormat::BC2UNorm:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case RHIFormat::BC3UNorm:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case RHIFormat::D16UNorm:
            return VK_FORMAT_D16_UNORM;
        case RHIFormat::D24UNormS8UInt:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32Float:
            return VK_FORMAT_D32_SFLOAT;
        case RHIFormat::D32FloatS8UInt:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    bool is_vk_depth_format(VkFormat format)
    {
        return format == VK_FORMAT_D16_UNORM ||
            format == VK_FORMAT_D24_UNORM_S8_UINT ||
            format == VK_FORMAT_D32_SFLOAT ||
            format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    bool is_vk_stencil_format(VkFormat format)
    {
        return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    VulkanBuffer::VulkanBuffer(
        RHIBufferDesc desc,
        VkDevice device,
        VkBuffer buffer,
        VkDeviceMemory memory,
        RHIAccess initial_access)
        : RHIBuffer(std::move(desc))
        , vk_device(device)
        , vk_buffer(buffer)
        , vk_memory(memory)
        , resource_access(initial_access == RHIAccess::Unknown ? RHIAccess::Common : initial_access)
    {
    }

    VulkanBuffer::~VulkanBuffer()
    {
        if (vk_buffer != VK_NULL_HANDLE && vk_device != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(vk_device, vk_buffer, nullptr);
        }
        if (vk_memory != VK_NULL_HANDLE && vk_device != VK_NULL_HANDLE)
        {
            vkFreeMemory(vk_device, vk_memory, nullptr);
        }
    }

    VkBuffer VulkanBuffer::buffer() const
    {
        return vk_buffer;
    }

    RHIAccess VulkanBuffer::current_access() const
    {
        return resource_access;
    }

    void VulkanBuffer::set_current_access(RHIAccess access)
    {
        resource_access = access;
    }

    VulkanTexture::VulkanTexture(
        RHITextureDesc desc,
        VkDevice device,
        VkImage image,
        VkDeviceMemory memory,
        bool owns_image,
        VkImageLayout initial_layout,
        RHIAccess initial_access)
        : RHITexture(std::move(desc))
        , vk_device(device)
        , vk_image(image)
        , vk_memory(memory)
        , owns_vk_image(owns_image)
        , current_image_layout(initial_layout)
        , resource_access(initial_access == RHIAccess::Unknown ? RHIAccess::Common : initial_access)
        , initial_layout_is_undefined(initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
    }

    VulkanTexture::~VulkanTexture()
    {
        if (!owns_vk_image || vk_device == VK_NULL_HANDLE)
        {
            return;
        }
        if (vk_image != VK_NULL_HANDLE)
        {
            vkDestroyImage(vk_device, vk_image, nullptr);
        }
        if (vk_memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(vk_device, vk_memory, nullptr);
        }
    }

    VkImage VulkanTexture::image() const
    {
        return vk_image;
    }

    VkImageLayout VulkanTexture::image_layout() const
    {
        return current_image_layout;
    }

    RHIAccess VulkanTexture::current_access() const
    {
        return resource_access;
    }

    bool VulkanTexture::has_undefined_initial_layout() const
    {
        return initial_layout_is_undefined;
    }

    void VulkanTexture::set_state(VkImageLayout layout, RHIAccess access)
    {
        current_image_layout = layout;
        resource_access = access;
        initial_layout_is_undefined = false;
    }

    VulkanTextureView::VulkanTextureView(
        std::shared_ptr<RHITexture> texture,
        RHITextureViewDesc desc,
        VkDevice device,
        VkImageView image_view,
        bool owns_image_view)
        : RHITextureView(std::move(texture), std::move(desc))
        , vk_device(device)
        , vk_image_view(image_view)
        , owns_vk_image_view(owns_image_view)
    {
    }

    VulkanTextureView::~VulkanTextureView()
    {
        if (owns_vk_image_view && vk_image_view != VK_NULL_HANDLE && vk_device != VK_NULL_HANDLE)
        {
            vkDestroyImageView(vk_device, vk_image_view, nullptr);
        }
    }

    VkImageView VulkanTextureView::image_view() const
    {
        return vk_image_view;
    }

    VulkanStagingBuffer::VulkanStagingBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
        : vk_device(device)
        , vk_buffer(buffer)
        , vk_memory(memory)
    {
    }

    VulkanStagingBuffer::~VulkanStagingBuffer()
    {
        if (vk_buffer != VK_NULL_HANDLE && vk_device != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(vk_device, vk_buffer, nullptr);
        }
        if (vk_memory != VK_NULL_HANDLE && vk_device != VK_NULL_HANDLE)
        {
            vkFreeMemory(vk_device, vk_memory, nullptr);
        }
    }

    VkBuffer VulkanStagingBuffer::buffer() const
    {
        return vk_buffer;
    }

    RHIResult<VulkanStagingBufferRef> create_vulkan_staging_buffer(
        VkPhysicalDevice physical_device,
        VkDevice device,
        const void* source_data,
        std::size_t source_size)
    {
        if (physical_device == VK_NULL_HANDLE || device == VK_NULL_HANDLE || source_data == nullptr || source_size == 0)
        {
            return RHIResult<VulkanStagingBufferRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan staging buffer requires a device and non-empty source data.");
        }

        VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_info.size = static_cast<VkDeviceSize>(source_size);
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkResult result = vkCreateBuffer(device, &buffer_info, nullptr, &buffer);
        if (result != VK_SUCCESS)
        {
            return RHIResult<VulkanStagingBufferRef>::failure(
                result == VK_ERROR_DEVICE_LOST ? RHIErrorCode::DeviceLost : RHIErrorCode::BackendFailure,
                "vkCreateBuffer failed while creating a Vulkan staging buffer.");
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer, &requirements);
        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
        std::uint32_t memory_type_index = VK_MAX_MEMORY_TYPES;
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index)
        {
            const VkMemoryPropertyFlags properties = memory_properties.memoryTypes[index].propertyFlags;
            if ((requirements.memoryTypeBits & (1U << index)) != 0 &&
                (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 &&
                (properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
            {
                memory_type_index = index;
                break;
            }
        }
        if (memory_type_index == VK_MAX_MEMORY_TYPES)
        {
            vkDestroyBuffer(device, buffer, nullptr);
            return RHIResult<VulkanStagingBufferRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan staging upload requires host-visible, host-coherent memory.");
        }

        VkMemoryAllocateInfo allocation_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation_info.allocationSize = requirements.size;
        allocation_info.memoryTypeIndex = memory_type_index;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        result = vkAllocateMemory(device, &allocation_info, nullptr, &memory);
        if (result != VK_SUCCESS)
        {
            vkDestroyBuffer(device, buffer, nullptr);
            return RHIResult<VulkanStagingBufferRef>::failure(
                result == VK_ERROR_DEVICE_LOST ? RHIErrorCode::DeviceLost : RHIErrorCode::OutOfMemory,
                "vkAllocateMemory failed while creating a Vulkan staging buffer.");
        }
        result = vkBindBufferMemory(device, buffer, memory, 0);
        if (result != VK_SUCCESS)
        {
            vkFreeMemory(device, memory, nullptr);
            vkDestroyBuffer(device, buffer, nullptr);
            return RHIResult<VulkanStagingBufferRef>::failure(
                result == VK_ERROR_DEVICE_LOST ? RHIErrorCode::DeviceLost : RHIErrorCode::BackendFailure,
                "vkBindBufferMemory failed while creating a Vulkan staging buffer.");
        }

        void* mapped_data = nullptr;
        result = vkMapMemory(device, memory, 0, static_cast<VkDeviceSize>(source_size), 0, &mapped_data);
        if (result != VK_SUCCESS || mapped_data == nullptr)
        {
            vkFreeMemory(device, memory, nullptr);
            vkDestroyBuffer(device, buffer, nullptr);
            return RHIResult<VulkanStagingBufferRef>::failure(
                result == VK_ERROR_DEVICE_LOST ? RHIErrorCode::DeviceLost : RHIErrorCode::BackendFailure,
                "vkMapMemory failed while initializing a Vulkan staging buffer.");
        }
        std::memcpy(mapped_data, source_data, source_size);
        vkUnmapMemory(device, memory);
        return RHIResult<VulkanStagingBufferRef>::success(
            std::make_shared<VulkanStagingBuffer>(device, buffer, memory));
    }

    VulkanRenderPassResources::VulkanRenderPassResources(
        VkDevice device,
        VkRenderPass render_pass,
        VkFramebuffer framebuffer,
        std::vector<RHIFormat> color_formats,
        std::uint32_t sample_count)
        : vk_device(device)
        , vk_render_pass(render_pass)
        , vk_framebuffer(framebuffer)
        , pass_color_formats(std::move(color_formats))
        , pass_sample_count(sample_count)
    {
    }

    VulkanRenderPassResources::~VulkanRenderPassResources()
    {
        if (vk_device == VK_NULL_HANDLE)
        {
            return;
        }
        if (vk_framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(vk_device, vk_framebuffer, nullptr);
        }
        if (vk_render_pass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(vk_device, vk_render_pass, nullptr);
        }
    }

    VkRenderPass VulkanRenderPassResources::render_pass() const
    {
        return vk_render_pass;
    }

    VkFramebuffer VulkanRenderPassResources::framebuffer() const
    {
        return vk_framebuffer;
    }

    bool VulkanRenderPassResources::is_compatible_with(const RHIGraphicsPipelineDesc& pipeline_desc) const
    {
        if (pipeline_desc.color_attachment_count != pass_color_formats.size() ||
            pipeline_desc.sample_count != pass_sample_count ||
            pipeline_desc.depth_stencil_format != RHIFormat::Unknown)
        {
            return false;
        }
        for (std::uint32_t index = 0; index < pipeline_desc.color_attachment_count; ++index)
        {
            if (pipeline_desc.color_formats[index] != pass_color_formats[index])
            {
                return false;
            }
        }
        return true;
    }

    VulkanShader::VulkanShader(RHIShaderDesc desc, VkDevice device, VkShaderModule shader_module)
        : RHIShader(std::move(desc))
        , vk_device(device)
        , vk_shader_module(shader_module)
    {
    }

    VulkanShader::~VulkanShader()
    {
        if (vk_device != VK_NULL_HANDLE && vk_shader_module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(vk_device, vk_shader_module, nullptr);
        }
    }

    VkShaderModule VulkanShader::shader_module() const
    {
        return vk_shader_module;
    }

    VulkanBindingLayout::VulkanBindingLayout(
        RHIBindingLayoutDesc desc,
        VkDevice device,
        std::array<VkDescriptorSetLayout, 5> descriptor_set_layouts,
        std::vector<NativeBinding> native_bindings)
        : RHIBindingLayout(std::move(desc))
        , vk_device(device)
        , vk_descriptor_set_layouts(descriptor_set_layouts)
        , binding_mappings(std::move(native_bindings))
    {
    }

    VulkanBindingLayout::~VulkanBindingLayout()
    {
        if (vk_device == VK_NULL_HANDLE)
        {
            return;
        }
        for (VkDescriptorSetLayout layout : vk_descriptor_set_layouts)
        {
            if (layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(vk_device, layout, nullptr);
            }
        }
    }

    VkDescriptorSetLayout VulkanBindingLayout::descriptor_set_layout(RHIBindingGroup group) const
    {
        return vk_descriptor_set_layouts[static_cast<std::size_t>(group)];
    }

    RHIResult<std::uint32_t> VulkanBindingLayout::native_binding(
        RHIBindingGroup group,
        RHIResourceBindingType type,
        std::uint32_t slot) const
    {
        for (const NativeBinding& mapping : binding_mappings)
        {
            if (mapping.group == group && mapping.type == type && mapping.slot == slot)
            {
                return RHIResult<std::uint32_t>::success(mapping.binding);
            }
        }
        return RHIResult<std::uint32_t>::failure(
            RHIErrorCode::InvalidArgument,
            "Vulkan binding layout has no matching native binding.");
    }

    const std::array<VkDescriptorSetLayout, 5>& VulkanBindingLayout::descriptor_set_layouts() const
    {
        return vk_descriptor_set_layouts;
    }

    VulkanSampler::VulkanSampler(RHISamplerDesc desc, VkDevice device, VkSampler sampler)
        : RHISampler(std::move(desc))
        , vk_device(device)
        , vk_sampler(sampler)
    {
    }

    VulkanSampler::~VulkanSampler()
    {
        if (vk_device != VK_NULL_HANDLE && vk_sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(vk_device, vk_sampler, nullptr);
        }
    }

    VkSampler VulkanSampler::sampler() const
    {
        return vk_sampler;
    }

    VulkanBindingSet::VulkanBindingSet(
        RHIBindingSetDesc desc,
        VkDevice device,
        VkDescriptorPool descriptor_pool,
        VkDescriptorSet descriptor_set)
        : RHIBindingSet(std::move(desc))
        , vk_device(device)
        , vk_descriptor_pool(descriptor_pool)
        , vk_descriptor_set(descriptor_set)
    {
    }

    VulkanBindingSet::~VulkanBindingSet()
    {
        if (vk_device != VK_NULL_HANDLE && vk_descriptor_pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(vk_device, vk_descriptor_pool, nullptr);
        }
    }

    VkDescriptorSet VulkanBindingSet::descriptor_set() const
    {
        return vk_descriptor_set;
    }

    VulkanGraphicsPipeline::VulkanGraphicsPipeline(
        RHIGraphicsPipelineDesc desc,
        VkDevice device,
        VkRenderPass compatibility_render_pass,
        VkPipelineLayout pipeline_layout,
        VkPipeline pipeline)
        : RHIGraphicsPipeline(std::move(desc))
        , vk_device(device)
        , vk_compatibility_render_pass(compatibility_render_pass)
        , vk_pipeline_layout(pipeline_layout)
        , vk_pipeline(pipeline)
    {
    }

    VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
    {
        if (vk_device == VK_NULL_HANDLE)
        {
            return;
        }
        if (vk_pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vk_device, vk_pipeline, nullptr);
        }
        if (vk_pipeline_layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(vk_device, vk_pipeline_layout, nullptr);
        }
        if (vk_compatibility_render_pass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(vk_device, vk_compatibility_render_pass, nullptr);
        }
    }

    VkPipeline VulkanGraphicsPipeline::pipeline() const
    {
        return vk_pipeline;
    }

    VkPipelineLayout VulkanGraphicsPipeline::pipeline_layout() const
    {
        return vk_pipeline_layout;
    }

    bool VulkanGraphicsPipeline::is_compatible_with(const VulkanRenderPassResources& render_pass) const
    {
        return render_pass.is_compatible_with(desc());
    }
}
