#include "drivers/vulkan/vulkan_resource.h"

#include "drivers/vulkan/vulkan_deferred_deletion.h"

#include "core/misc/logger.h"

#include <algorithm>
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
        VulkanMemoryManager& memory_manager,
        VulkanDeferredDeletionQueue& deletion_queue,
        VulkanAllocatedBuffer allocated_buffer,
        RHIAccess initial_access)
        : RHIBuffer(std::move(desc))
        , memory_manager_instance(&memory_manager)
        , deletion_queue_instance(&deletion_queue)
        , allocated_buffer(std::move(allocated_buffer))
        , resource_access(initial_access == RHIAccess::Unknown ? RHIAccess::Common : initial_access)
    {
    }

    VulkanBuffer::~VulkanBuffer()
    {
        if (memory_manager_instance == nullptr)
        {
            return;
        }
        if (last_use_value == 0 || deletion_queue_instance == nullptr)
        {
            memory_manager_instance->destroy_buffer(allocated_buffer);
            return;
        }

        auto payload = std::make_shared<VulkanAllocatedBuffer>(std::move(allocated_buffer));
        VulkanMemoryManager* const memory_manager = memory_manager_instance;
        const RHIStatus status = deletion_queue_instance->enqueue(
            last_use_value,
            [memory_manager, payload](VkDevice)
            {
                memory_manager->destroy_buffer(*payload);
            });
        if (!status)
        {
            TOY_LOG_ERROR("Failed to defer Vulkan buffer deletion: {}", status.message());
            memory_manager_instance->destroy_buffer(*payload);
        }
    }

    VkBuffer VulkanBuffer::buffer() const
    {
        return allocated_buffer.buffer;
    }

    RHIAccess VulkanBuffer::current_access() const
    {
        return resource_access;
    }

    void VulkanBuffer::set_current_access(RHIAccess access)
    {
        resource_access = access;
    }

    void VulkanBuffer::mark_used(RHIQueueCompletionValue completion_value)
    {
        last_use_value = std::max(last_use_value, completion_value);
    }

    RHIQueueCompletionValue VulkanBuffer::last_use_completion_value() const
    {
        return last_use_value;
    }

    VulkanTexture::VulkanTexture(
        RHITextureDesc desc,
        VulkanMemoryManager& memory_manager,
        VulkanDeferredDeletionQueue& deletion_queue,
        VulkanAllocatedImage allocated_image,
        VkImageLayout initial_layout,
        RHIAccess initial_access)
        : RHITexture(std::move(desc))
        , memory_manager_instance(&memory_manager)
        , deletion_queue_instance(&deletion_queue)
        , allocated_image(std::move(allocated_image))
        , initial_layout_is_undefined(initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        subresource_states.assign(
            static_cast<std::size_t>(this->desc().mip_levels) * this->desc().array_layers * 2U,
            {initial_layout, initial_access == RHIAccess::Unknown ? RHIAccess::Common : initial_access});
    }

    VulkanTexture::VulkanTexture(
        RHITextureDesc desc,
        VkImage external_image,
        VkImageLayout initial_layout,
        RHIAccess initial_access)
        : RHITexture(std::move(desc))
        , initial_layout_is_undefined(initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        allocated_image.image = external_image;
        subresource_states.assign(
            static_cast<std::size_t>(this->desc().mip_levels) * this->desc().array_layers * 2U,
            {initial_layout, initial_access == RHIAccess::Unknown ? RHIAccess::Common : initial_access});
    }

    VulkanTexture::~VulkanTexture()
    {
        if (memory_manager_instance == nullptr)
        {
            return;
        }
        if (last_use_value == 0 || deletion_queue_instance == nullptr)
        {
            memory_manager_instance->destroy_image(allocated_image);
            return;
        }

        auto payload = std::make_shared<VulkanAllocatedImage>(std::move(allocated_image));
        VulkanMemoryManager* const memory_manager = memory_manager_instance;
        const RHIStatus status = deletion_queue_instance->enqueue(
            last_use_value,
            [memory_manager, payload](VkDevice)
            {
                memory_manager->destroy_image(*payload);
            });
        if (!status)
        {
            TOY_LOG_ERROR("Failed to defer Vulkan image deletion: {}", status.message());
            memory_manager_instance->destroy_image(*payload);
        }
    }

    VkImage VulkanTexture::image() const
    {
        return allocated_image.image;
    }

    VkImageLayout VulkanTexture::image_layout() const
    {
        return subresource_states.front().layout;
    }

    RHIAccess VulkanTexture::current_access() const
    {
        return subresource_states.front().access;
    }

    bool VulkanTexture::has_undefined_initial_layout() const
    {
        return initial_layout_is_undefined;
    }

    void VulkanTexture::set_state(VkImageLayout layout, RHIAccess access)
    {
        std::fill(subresource_states.begin(), subresource_states.end(), VulkanTextureSubresourceState{layout, access});
        initial_layout_is_undefined = false;
    }

    VulkanTextureSubresourceState VulkanTexture::subresource_state(
        RHITextureAspect aspect,
        std::uint32_t mip,
        std::uint32_t layer) const
    {
        const std::size_t plane = aspect == RHITextureAspect::Stencil ? 1U : 0U;
        const std::size_t index =
            (static_cast<std::size_t>(layer) * desc().mip_levels + mip) * 2U + plane;
        return subresource_states[index];
    }

    void VulkanTexture::set_subresource_state(
        RHITextureAspect aspect,
        std::uint32_t mip,
        std::uint32_t layer,
        VulkanTextureSubresourceState state)
    {
        const auto set_plane = [&](std::size_t plane)
        {
            const std::size_t index =
                (static_cast<std::size_t>(layer) * desc().mip_levels + mip) * 2U + plane;
            subresource_states[index] = state;
        };
        set_plane(aspect == RHITextureAspect::Stencil ? 1U : 0U);
        if (aspect == RHITextureAspect::DepthStencil)
        {
            set_plane(1U);
        }
        initial_layout_is_undefined = false;
    }

    void VulkanTexture::mark_used(RHIQueueCompletionValue completion_value)
    {
        last_use_value = std::max(last_use_value, completion_value);
    }

    RHIQueueCompletionValue VulkanTexture::last_use_completion_value() const
    {
        return last_use_value;
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

    VulkanRenderPassResources::VulkanRenderPassResources(
        VkDevice device,
        VkRenderPass render_pass,
        VkFramebuffer framebuffer,
        std::vector<RHIFormat> color_formats,
        RHIFormat depth_stencil_format,
        bool depth_read_only,
        bool stencil_read_only,
        std::uint32_t sample_count)
        : vk_device(device)
        , vk_render_pass(render_pass)
        , vk_framebuffer(framebuffer)
        , pass_color_formats(std::move(color_formats))
        , pass_depth_stencil_format(depth_stencil_format)
        , pass_depth_read_only(depth_read_only)
        , pass_stencil_read_only(stencil_read_only)
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
            pipeline_desc.depth_stencil_format != pass_depth_stencil_format)
        {
            return false;
        }
        if (pass_depth_stencil_format != RHIFormat::Unknown)
        {
            if (pass_depth_read_only && pipeline_desc.depth_stencil.depth_write_enable)
            {
                return false;
            }
            if (pass_stencil_read_only && pipeline_desc.depth_stencil.stencil_test_enable &&
                pipeline_desc.depth_stencil.stencil_write_mask != 0)
            {
                return false;
            }
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
        std::array<
            VkDescriptorSetLayout,
            static_cast<std::size_t>(RHIBindingGroup::Max)> descriptor_set_layouts,
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

    const std::array<
        VkDescriptorSetLayout,
        static_cast<std::size_t>(RHIBindingGroup::Max)>& VulkanBindingLayout::descriptor_set_layouts() const
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
