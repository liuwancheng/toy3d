#include "drivers/vulkan/vulkan_resource.h"

#include "drivers/vulkan/vulkan_deferred_deletion.h"
#include "drivers/vulkan/vulkan_type_mapping.h"
#include "drivers/vulkan/vulkan_upload_manager.h"

#include "logging/logger.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace toy3d
{
    // --------------------------------------------------------------------------
    // VulkanBuffer: GPU buffer access state and deferred destruction
    // --------------------------------------------------------------------------
    VulkanBuffer::VulkanBuffer(const RHIDevice& owner, RHIBufferDesc desc, VulkanMemoryManager& memory_manager,
                               VulkanDeferredDeletionQueue& deletion_queue, VulkanAllocatedBuffer allocated_buffer,
                               RHIAccess initial_access)
        : RHIBuffer(owner, std::move(desc)), memory_manager_instance(&memory_manager),
          deletion_queue_instance(&deletion_queue), allocated_buffer(std::move(allocated_buffer)),
          resource_access(initial_access == RHIAccess::Unknown ? RHIAccess::Common : initial_access)
    {
    }

    VulkanBuffer::VulkanBuffer(const RHIDevice& owner, RHIBufferDesc desc,
                               std::shared_ptr<VulkanUploadPage> upload_page, RHIAccess initial_access)
        : RHIBuffer(owner, std::move(desc)), transient_upload_page(std::move(upload_page)),
          resource_access(initial_access)
    {
    }

    VulkanBuffer::~VulkanBuffer()
    {
        if (transient_upload_page || memory_manager_instance == nullptr)
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
        const RHIStatus status = deletion_queue_instance->enqueue(last_use_value,
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
        return transient_upload_page ? transient_upload_page->buffer() : allocated_buffer.buffer;
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

    // --------------------------------------------------------------------------
    // VulkanBufferView: formatted view retained by recorded binding packets
    // --------------------------------------------------------------------------
    VulkanBufferView::VulkanBufferView(RHIBufferRef buffer, RHIBufferViewDesc desc, VkDevice device, VkBufferView view)
        : RHIBufferView(std::move(buffer), std::move(desc)), vk_device(device), vk_buffer_view(view)
    {
    }

    VulkanBufferView::~VulkanBufferView()
    {
        // Command lists retain the source binding sets (and this view) until actual
        // completion. The backing buffer remains owned by RHIBufferView during destruction.
        if (vk_device != VK_NULL_HANDLE && vk_buffer_view != VK_NULL_HANDLE)
        {
            vkDestroyBufferView(vk_device, vk_buffer_view, nullptr);
        }
    }

    VkBufferView VulkanBufferView::buffer_view() const
    {
        return vk_buffer_view;
    }

    // --------------------------------------------------------------------------
    // VulkanReadback: CPU-visible pixel data and GPU-completion lifetime
    // --------------------------------------------------------------------------
    VulkanReadback::VulkanReadback(const RHIDevice& owner, std::string debug_name, VulkanMemoryManager& memory_manager,
                                   VulkanDeferredDeletionQueue& deletion_queue, VulkanAllocatedBuffer allocated_buffer,
                                   PixelFormat format, Extent extent)
        : RHIReadback(owner, std::move(debug_name), format, extent), memory_manager_instance(&memory_manager),
          deletion_queue_instance(&deletion_queue), allocated_buffer(std::move(allocated_buffer))
    {
    }

    VulkanReadback::~VulkanReadback()
    {
        if (memory_manager_instance == nullptr)
        {
            return;
        }
        const RHIQueueCompletionValue last_use = last_use_completion_value();
        if (last_use == 0 || deletion_queue_instance == nullptr)
        {
            memory_manager_instance->destroy_buffer(allocated_buffer);
            return;
        }
        auto payload = std::make_shared<VulkanAllocatedBuffer>(std::move(allocated_buffer));
        VulkanMemoryManager* const memory_manager = memory_manager_instance;
        const RHIStatus status = deletion_queue_instance->enqueue(last_use,
                                                                  [memory_manager, payload](VkDevice)
                                                                  {
                                                                      memory_manager->destroy_buffer(*payload);
                                                                  });
        if (!status)
        {
            TOY_LOG_ERROR("Failed to defer Vulkan readback deletion: {}", status.message());
            memory_manager_instance->destroy_buffer(*payload);
        }
    }

    RHIResult<std::uint32_t> VulkanReadback::read_uint32_impl() const
    {
        if (memory_manager_instance == nullptr || allocated_buffer.allocation.mapped_data == nullptr)
        {
            return RHIResult<std::uint32_t>::failure(RHIErrorCode::BackendFailure,
                                                     "Vulkan readback allocation is not mapped.");
        }
        const RHIStatus status =
            memory_manager_instance->invalidate_allocation(allocated_buffer.allocation, 0, sizeof(std::uint32_t));
        if (!status)
        {
            return RHIResult<std::uint32_t>::failure(status.code(), status.message());
        }
        std::uint32_t result = 0;
        std::memcpy(&result, allocated_buffer.allocation.mapped_data, sizeof(result));
        return RHIResult<std::uint32_t>::success(result);
    }

    RHIResult<RHITextureReadbackData> VulkanReadback::read_texture_impl() const
    {
        if (readback_format() == PixelFormat::R32UInt || !memory_manager_instance ||
            !allocated_buffer.allocation.mapped_data)
        {
            return RHIResult<RHITextureReadbackData>::failure(RHIErrorCode::InvalidArgument,
                                                              "Color readback is not mapped.");
        }
        const std::size_t size = static_cast<std::size_t>(readback_extent().width) * readback_extent().height * 4;
        const auto status = memory_manager_instance->invalidate_allocation(allocated_buffer.allocation, 0, size);
        if (!status)
        {
            return RHIResult<RHITextureReadbackData>::failure(status.code(), status.message());
        }
        RHITextureReadbackData data;
        data.format = readback_format();
        data.extent = readback_extent();
        data.row_pitch = readback_extent().width * 4;
        data.bytes.resize(size);
        std::memcpy(data.bytes.data(), allocated_buffer.allocation.mapped_data, size);
        return RHIResult<RHITextureReadbackData>::success(std::move(data));
    }

    // --------------------------------------------------------------------------
    // VulkanTexture: image subresource state and deferred destruction
    // --------------------------------------------------------------------------
    VulkanTexture::VulkanTexture(const RHIDevice& owner, RHITextureDesc desc, VulkanMemoryManager& memory_manager,
                                 VulkanDeferredDeletionQueue& deletion_queue, VulkanAllocatedImage allocated_image,
                                 VkImageLayout initial_layout, RHIAccess initial_access)
        : RHITexture(owner, std::move(desc)), memory_manager_instance(&memory_manager),
          deletion_queue_instance(&deletion_queue), allocated_image(std::move(allocated_image)),
          initial_layout_is_undefined(initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        subresource_states.assign(
            static_cast<std::size_t>(this->desc().mip_levels) * this->desc().array_layers * 2U,
            {initial_layout, initial_access == RHIAccess::Unknown ? RHIAccess::Common : initial_access});
    }

    VulkanTexture::VulkanTexture(const RHIDevice& owner, RHITextureDesc desc, VkImage external_image,
                                 VkImageLayout initial_layout, RHIAccess initial_access)
        : RHITexture(owner, std::move(desc)), initial_layout_is_undefined(initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
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
        const RHIStatus status = deletion_queue_instance->enqueue(last_use_value,
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

    VulkanTextureSubresourceState VulkanTexture::subresource_state(RHITextureAspect aspect, std::uint32_t mip,
                                                                   std::uint32_t layer) const
    {
        const std::size_t plane = aspect == RHITextureAspect::Stencil ? 1U : 0U;
        const std::size_t index = (static_cast<std::size_t>(layer) * desc().mip_levels + mip) * 2U + plane;
        return subresource_states[index];
    }

    void VulkanTexture::set_subresource_state(RHITextureAspect aspect, std::uint32_t mip, std::uint32_t layer,
                                              VulkanTextureSubresourceState state)
    {
        const auto set_plane = [&](std::size_t plane)
        {
            const std::size_t index = (static_cast<std::size_t>(layer) * desc().mip_levels + mip) * 2U + plane;
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

    // --------------------------------------------------------------------------
    // VulkanTextureView: native image-view ownership
    // --------------------------------------------------------------------------
    VulkanTextureView::VulkanTextureView(std::shared_ptr<RHITexture> texture, RHITextureViewDesc desc, VkDevice device,
                                         VkImageView image_view, bool owns_image_view)
        : RHITextureView(std::move(texture), std::move(desc)), vk_device(device), vk_image_view(image_view),
          owns_vk_image_view(owns_image_view)
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

    // --------------------------------------------------------------------------
    // VulkanRenderPassResources: render-pass lifetime and compatibility
    // --------------------------------------------------------------------------
    VulkanRenderPassResources::VulkanRenderPassResources(VkDevice device, VkRenderPass render_pass,
                                                         VkFramebuffer framebuffer,
                                                         std::vector<PixelFormat> color_formats,
                                                         PixelFormat depth_stencil_format, bool depth_read_only,
                                                         bool stencil_read_only, std::uint32_t sample_count)
        : vk_device(device), vk_render_pass(render_pass), vk_framebuffer(framebuffer),
          pass_color_formats(std::move(color_formats)), pass_depth_stencil_format(depth_stencil_format),
          pass_depth_read_only(depth_read_only), pass_stencil_read_only(stencil_read_only),
          pass_sample_count(sample_count)
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
        if (pass_depth_stencil_format != PixelFormat::Unknown)
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

    // --------------------------------------------------------------------------
    // VulkanShader: native shader-module lifetime
    // --------------------------------------------------------------------------
    VulkanShader::VulkanShader(const RHIDevice& owner, RHIShaderDesc desc, VkDevice device,
                               VkShaderModule shader_module)
        : RHIShader(owner, std::move(desc)), vk_device(device), vk_shader_module(shader_module)
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

    // --------------------------------------------------------------------------
    // VulkanBindingLayout: logical groups and descriptor-set layouts
    // --------------------------------------------------------------------------
    VulkanBindingLayout::VulkanBindingLayout(
        const RHIDevice& owner, RHIBindingLayoutDesc desc, VkDevice device,
        std::array<VkDescriptorSetLayout, physical_set_count> descriptor_set_layouts)
        : RHIBindingLayout(owner, std::move(desc)), vk_device(device), vk_descriptor_set_layouts(descriptor_set_layouts)
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

    std::uint32_t VulkanBindingLayout::physical_set(RHIBindingGroup group)
    {
        switch (group)
        {
        case RHIBindingGroup::Global:
        case RHIBindingGroup::View:
            return 0;
        case RHIBindingGroup::Pass:
            return 1;
        case RHIBindingGroup::Material:
            return 2;
        case RHIBindingGroup::Object:
            return 3;
        case RHIBindingGroup::Max:
            break;
        }
        return static_cast<std::uint32_t>(physical_set_count);
    }

    VkDescriptorSetLayout VulkanBindingLayout::descriptor_set_layout(RHIBindingGroup group) const
    {
        const std::uint32_t set = physical_set(group);
        return set < vk_descriptor_set_layouts.size() ? vk_descriptor_set_layouts[set] : VK_NULL_HANDLE;
    }

    const std::array<VkDescriptorSetLayout, VulkanBindingLayout::physical_set_count>& VulkanBindingLayout::
        descriptor_set_layouts() const
    {
        return vk_descriptor_set_layouts;
    }

    // --------------------------------------------------------------------------
    // VulkanSampler: native sampler lifetime
    // --------------------------------------------------------------------------
    VulkanSampler::VulkanSampler(const RHIDevice& owner, RHISamplerDesc desc, VkDevice device, VkSampler sampler)
        : RHISampler(owner, std::move(desc)), vk_device(device), vk_sampler(sampler)
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

    // --------------------------------------------------------------------------
    // VulkanBindingPacket: descriptor-set page and binding retention
    // --------------------------------------------------------------------------
    VulkanBindingPacket::VulkanBindingPacket(std::shared_ptr<VulkanDescriptorPoolPage> descriptor_page,
                                             VkDescriptorSet descriptor_set, std::vector<RHIBindingSetRef> logical_sets)
        : pool_page(std::move(descriptor_page)), vk_descriptor_set(descriptor_set), source_sets(std::move(logical_sets))
    {
    }

    VulkanBindingPacket::~VulkanBindingPacket()
    {
        vk_descriptor_set = VK_NULL_HANDLE;
    }

    VkDescriptorSet VulkanBindingPacket::descriptor_set() const
    {
        return vk_descriptor_set;
    }

    // --------------------------------------------------------------------------
    // VulkanGraphicsPipeline: native pipeline lifetime and pass compatibility
    // --------------------------------------------------------------------------
    VulkanGraphicsPipeline::VulkanGraphicsPipeline(const RHIDevice& owner, RHIGraphicsPipelineDesc desc,
                                                   VkDevice device, VkRenderPass compatibility_render_pass,
                                                   VkPipelineLayout pipeline_layout, VkPipeline pipeline)
        : RHIGraphicsPipeline(owner, std::move(desc)), vk_device(device),
          vk_compatibility_render_pass(compatibility_render_pass), vk_pipeline_layout(pipeline_layout),
          vk_pipeline(pipeline)
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
} // namespace toy3d
