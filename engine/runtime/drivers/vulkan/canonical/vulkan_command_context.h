#pragma once

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/vulkan/canonical/vulkan_graphics_state.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace toy3d
{
    class VulkanDevice;
    class VulkanGraphicsPipeline;
    class VulkanRenderPassResources;
    class VulkanStagingBuffer;
    class VulkanViewportContext;

    // A command list is allocated from one viewport frame slot. Its native
    // command buffer remains valid until that slot's completion fence allows
    // the viewport to reset its command pool.
    class VulkanCommandList final : public RHICommandList
    {
    public:
        VulkanCommandList(
            VulkanViewportContext& owner,
            VkCommandBuffer command_buffer,
            std::uint64_t frame_id,
            std::string debug_name);

        VkCommandBuffer command_buffer() const;
        bool belongs_to(const VulkanViewportContext& viewport, std::uint64_t frame_id) const;
        void retain_resource(const RHIResourceRef& resource);
        const std::vector<RHIResourceRef>& retained_resources() const;
        void retain_staging_buffer(std::shared_ptr<VulkanStagingBuffer> staging_buffer);
        const std::vector<std::shared_ptr<VulkanStagingBuffer>>& retained_staging_buffers() const;
        void retain_texture_view(const RHITextureViewRef& view);
        const std::vector<RHITextureViewRef>& retained_texture_views() const;
        void retain_graphics_pipeline(const RHIGraphicsPipelineRef& pipeline);
        const std::vector<RHIGraphicsPipelineRef>& retained_graphics_pipelines() const;
        void retain_binding_set(const RHIBindingSetRef& binding_set);
        const std::vector<RHIBindingSetRef>& retained_binding_sets() const;
        void retain_render_pass_resources(std::shared_ptr<VulkanRenderPassResources> resources);
        const std::vector<std::shared_ptr<VulkanRenderPassResources>>& retained_render_pass_resources() const;

        RHIStatus begin_recording_by_context();
        RHIStatus close_by_context();
        RHIStatus mark_submitted_by_viewport();

    private:
        VulkanViewportContext* viewport_owner = nullptr;
        VkCommandBuffer vk_command_buffer = VK_NULL_HANDLE;
        std::uint64_t command_frame_id = 0;
        std::vector<RHIResourceRef> resources;
        std::vector<std::shared_ptr<VulkanStagingBuffer>> staging_buffers;
        std::vector<RHITextureViewRef> texture_views;
        std::vector<RHIGraphicsPipelineRef> graphics_pipelines;
        std::vector<RHIBindingSetRef> binding_sets;
        std::vector<std::shared_ptr<VulkanRenderPassResources>> render_pass_resources;
    };

    class VulkanGraphicsCommandContext final : public RHIGraphicsCommandContext
    {
    public:
        VulkanGraphicsCommandContext(
            VulkanDevice& device,
            VulkanViewportContext& viewport,
            VkCommandPool command_pool,
            std::uint64_t frame_id);
        ~VulkanGraphicsCommandContext() override = default;

        RHIStatus begin_recording(const std::string& debug_name) override;
        RHIStatus transition_resources(
            const std::vector<RHIResourceTransition>& transitions) override;
        RHIStatus copy_buffer(const RHIBufferCopyDesc& desc) override;
        RHIStatus upload_buffer(const RHIBufferUploadDesc& desc) override;
        RHIStatus copy_texture(const RHITextureCopyDesc& desc) override;
        RHIStatus upload_texture(const RHITextureUploadDesc& desc) override;
        RHIStatus write_gpu_fence(const RHIGPUFenceRef& fence) override;
        RHIResult<RHICommandListRef> finish_recording() override;

        RHIStatus begin_render_pass(const RHIRenderPassDesc& desc) override;
        RHIStatus end_render_pass() override;
        RHIStatus set_graphics_pipeline(
            const RHIGraphicsPipelineRef& pipeline) override;
        RHIStatus set_viewport(const RHIViewport& viewport) override;
        RHIStatus set_scissor(const RHIRect& rect) override;
        RHIStatus set_vertex_buffers(
            const std::vector<RHIVertexBufferBinding>& bindings) override;
        RHIStatus set_index_buffer(const RHIIndexBufferBinding& binding) override;
        RHIStatus bind_binding_set(const RHIBindingSetRef& binding_set) override;
        RHIStatus draw(const RHIDrawArgs& args) override;
        RHIStatus draw_indexed(const RHIDrawIndexedArgs& args) override;

    private:
        RHIStatus require_recording() const;
        RHIStatus flush_graphics_state(bool indexed_draw);
        RHIStatus unsupported_while_recording(const char* operation) const;

        VulkanDevice& vulkan_device;
        VulkanViewportContext& viewport_context;
        VkCommandPool vk_command_pool = VK_NULL_HANDLE;
        VkCommandBuffer vk_command_buffer = VK_NULL_HANDLE;
        std::uint64_t recording_frame_id = 0;
        std::shared_ptr<VulkanCommandList> recording_command_list;
        std::shared_ptr<VulkanRenderPassResources> active_render_pass;
        VulkanGraphicsState graphics_state;
    };
}
