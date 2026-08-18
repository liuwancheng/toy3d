#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/vulkan/vulkan_memory_manager.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace toy3d
{
    class VulkanDeferredDeletionQueue;

    VkFormat vulkan_format_from_rhi(RHIFormat format);
    bool is_vk_depth_format(VkFormat format);
    bool is_vk_stencil_format(VkFormat format);

    struct VulkanTextureSubresourceState
    {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        RHIAccess access = RHIAccess::Unknown;
    };

    class VulkanBuffer final : public RHIBuffer
    {
    public:
        VulkanBuffer(
            RHIBufferDesc desc,
            VulkanMemoryManager& memory_manager,
            VulkanDeferredDeletionQueue& deletion_queue,
            VulkanAllocatedBuffer allocated_buffer,
            RHIAccess initial_access);
        ~VulkanBuffer() override;

        VkBuffer buffer() const;
        RHIAccess current_access() const;
        void set_current_access(RHIAccess access);
        void mark_used(RHIQueueCompletionValue completion_value);
        RHIQueueCompletionValue last_use_completion_value() const;

    private:
        VulkanMemoryManager* memory_manager_instance = nullptr;
        VulkanDeferredDeletionQueue* deletion_queue_instance = nullptr;
        VulkanAllocatedBuffer allocated_buffer;
        RHIAccess resource_access = RHIAccess::Common;
        RHIQueueCompletionValue last_use_value = 0;
    };

    class VulkanTexture final : public RHITexture
    {
    public:
        VulkanTexture(
            RHITextureDesc desc,
            VulkanMemoryManager& memory_manager,
            VulkanDeferredDeletionQueue& deletion_queue,
            VulkanAllocatedImage allocated_image,
            VkImageLayout initial_layout,
            RHIAccess initial_access);
        VulkanTexture(
            RHITextureDesc desc,
            VkImage external_image,
            VkImageLayout initial_layout,
            RHIAccess initial_access);
        ~VulkanTexture() override;

        VkImage image() const;
        VulkanTextureSubresourceState subresource_state(
            RHITextureAspect aspect,
            std::uint32_t mip,
            std::uint32_t layer) const;
        void set_subresource_state(
            RHITextureAspect aspect,
            std::uint32_t mip,
            std::uint32_t layer,
            VulkanTextureSubresourceState state);
        VkImageLayout image_layout() const;
        RHIAccess current_access() const;
        bool has_undefined_initial_layout() const;
        void set_state(VkImageLayout layout, RHIAccess access);
        void mark_used(RHIQueueCompletionValue completion_value);
        RHIQueueCompletionValue last_use_completion_value() const;

    private:
        VulkanMemoryManager* memory_manager_instance = nullptr;
        VulkanDeferredDeletionQueue* deletion_queue_instance = nullptr;
        VulkanAllocatedImage allocated_image;
        std::vector<VulkanTextureSubresourceState> subresource_states;
        bool initial_layout_is_undefined = true;
        RHIQueueCompletionValue last_use_value = 0;
    };

    class VulkanTextureView final : public RHITextureView
    {
    public:
        VulkanTextureView(
            std::shared_ptr<RHITexture> texture,
            RHITextureViewDesc desc,
            VkDevice device,
            VkImageView image_view,
            bool owns_image_view);
        ~VulkanTextureView() override;

        VkImageView image_view() const;

    private:
        VkDevice vk_device = VK_NULL_HANDLE;
        VkImageView vk_image_view = VK_NULL_HANDLE;
        bool owns_vk_image_view = false;
    };

    class VulkanRenderPassResources final
    {
    public:
        VulkanRenderPassResources(
            VkDevice device,
            VkRenderPass render_pass,
            VkFramebuffer framebuffer,
            std::vector<RHIFormat> color_formats,
            RHIFormat depth_stencil_format,
            bool depth_read_only,
            bool stencil_read_only,
            std::uint32_t sample_count);
        ~VulkanRenderPassResources();

        VkRenderPass render_pass() const;
        VkFramebuffer framebuffer() const;
        bool is_compatible_with(const RHIGraphicsPipelineDesc& pipeline_desc) const;

    private:
        VkDevice vk_device = VK_NULL_HANDLE;
        VkRenderPass vk_render_pass = VK_NULL_HANDLE;
        VkFramebuffer vk_framebuffer = VK_NULL_HANDLE;
        std::vector<RHIFormat> pass_color_formats;
        RHIFormat pass_depth_stencil_format = RHIFormat::Unknown;
        bool pass_depth_read_only = false;
        bool pass_stencil_read_only = false;
        std::uint32_t pass_sample_count = 1;
    };

    using VulkanRenderPassResourcesRef = std::shared_ptr<VulkanRenderPassResources>;

    class VulkanShader final : public RHIShader
    {
    public:
        VulkanShader(RHIShaderDesc desc, VkDevice device, VkShaderModule shader_module);
        ~VulkanShader() override;

        VkShaderModule shader_module() const;

    private:
        VkDevice vk_device = VK_NULL_HANDLE;
        VkShaderModule vk_shader_module = VK_NULL_HANDLE;
    };

    class VulkanBindingLayout final : public RHIBindingLayout
    {
    public:
        static constexpr std::size_t physical_set_count = 4;

        struct NativeBinding
        {
            RHIBindingGroup group = RHIBindingGroup::Material;
            std::uint32_t slot = 0;
            RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
            std::uint32_t binding = 0;
        };

        VulkanBindingLayout(
            RHIBindingLayoutDesc desc,
            VkDevice device,
            std::array<VkDescriptorSetLayout, physical_set_count> descriptor_set_layouts,
            std::vector<NativeBinding> native_bindings);
        ~VulkanBindingLayout() override;

        static std::uint32_t physical_set(RHIBindingGroup group);
        VkDescriptorSetLayout descriptor_set_layout(RHIBindingGroup group) const;
        RHIResult<std::uint32_t> native_binding(
            RHIBindingGroup group,
            RHIResourceBindingType type,
            std::uint32_t slot) const;
        const std::array<VkDescriptorSetLayout, physical_set_count>&
            descriptor_set_layouts() const;

    private:
        VkDevice vk_device = VK_NULL_HANDLE;
        std::array<VkDescriptorSetLayout, physical_set_count> vk_descriptor_set_layouts = {};
        std::vector<NativeBinding> binding_mappings;
    };

    class VulkanSampler final : public RHISampler
    {
    public:
        VulkanSampler(RHISamplerDesc desc, VkDevice device, VkSampler sampler);
        ~VulkanSampler() override;

        VkSampler sampler() const;

    private:
        VkDevice vk_device = VK_NULL_HANDLE;
        VkSampler vk_sampler = VK_NULL_HANDLE;
    };

    class VulkanBindingSet final : public RHIBindingSet
    {
    public:
        VulkanBindingSet(
            RHIBindingSetDesc desc,
            VkDevice device,
            VkDescriptorPool descriptor_pool,
            VkDescriptorSet descriptor_set);
        ~VulkanBindingSet() override;

        VkDescriptorSet descriptor_set() const;

    private:
        VkDevice vk_device = VK_NULL_HANDLE;
        VkDescriptorPool vk_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet vk_descriptor_set = VK_NULL_HANDLE;
    };

    // The compatibility render pass supplies the attachment signature Vulkan
    // requires when a graphics pipeline is created. Actual framebuffers and
    // render passes remain command-list local.
    class VulkanGraphicsPipeline final : public RHIGraphicsPipeline
    {
    public:
        VulkanGraphicsPipeline(
            RHIGraphicsPipelineDesc desc,
            VkDevice device,
            VkRenderPass compatibility_render_pass,
            VkPipelineLayout pipeline_layout,
            VkPipeline pipeline);
        ~VulkanGraphicsPipeline() override;

        VkPipeline pipeline() const;
        VkPipelineLayout pipeline_layout() const;
        bool is_compatible_with(const VulkanRenderPassResources& render_pass) const;

    private:
        VkDevice vk_device = VK_NULL_HANDLE;
        VkRenderPass vk_compatibility_render_pass = VK_NULL_HANDLE;
        VkPipelineLayout vk_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline vk_pipeline = VK_NULL_HANDLE;
    };
}
