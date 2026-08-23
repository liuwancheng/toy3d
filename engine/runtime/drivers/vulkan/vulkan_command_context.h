#pragma once

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/vulkan/vulkan_graphics_state.h"
#include "drivers/vulkan/vulkan_resource.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace toy3d
{
    class VulkanDevice;
    class VulkanBuffer;
    class VulkanGraphicsPipeline;
    class VulkanBindingPacket;
    class VulkanRenderPassResources;
    class VulkanTexture;
    class VulkanUploadPage;
    class VulkanViewportContext;

    // Device-level command pools are retained by their command lists until
    // queue completion. Viewport frame-slot pools remain viewport-owned.
    class VulkanCommandPool final
    {
    public:
        VulkanCommandPool(VkDevice device, VkCommandPool command_pool);
        ~VulkanCommandPool();

        VulkanCommandPool(const VulkanCommandPool&) = delete;
        VulkanCommandPool& operator=(const VulkanCommandPool&) = delete;

        VkCommandPool handle() const { return vk_command_pool; }

    private:
        VkDevice vk_device = VK_NULL_HANDLE;
        VkCommandPool vk_command_pool = VK_NULL_HANDLE;
    };

    // A command list retains either a device-level pool or the viewport/frame
    // identity whose slot owns its native command buffer.
    class VulkanCommandList final : public RHICommandList
    {
    public:
        VulkanCommandList(
            const RHIDevice& device,
            VulkanViewportContext& owner,
            VkCommandBuffer command_buffer,
            std::uint64_t frame_id,
            std::string debug_name);
        VulkanCommandList(
            const RHIDevice& device,
            std::shared_ptr<VulkanCommandPool> command_pool,
            VkCommandBuffer command_buffer,
            std::string debug_name);

        VkCommandBuffer command_buffer() const;
        bool is_device_level() const;
        bool belongs_to(const VulkanViewportContext& viewport, std::uint64_t frame_id) const;
        void retain_resource(const RHIResourceRef& resource);
        const std::vector<RHIResourceRef>& retained_resources() const;
        void retain_upload_page(const std::shared_ptr<VulkanUploadPage>& upload_page);
        const std::vector<std::shared_ptr<VulkanUploadPage>>& retained_upload_pages() const;
        void retain_texture_view(const RHITextureViewRef& view);
        const std::vector<RHITextureViewRef>& retained_texture_views() const;
        void retain_graphics_pipeline(const RHIGraphicsPipelineRef& pipeline);
        const std::vector<RHIGraphicsPipelineRef>& retained_graphics_pipelines() const;
        void retain_binding_set(const RHIBindingSetRef& binding_set);
        const std::vector<RHIBindingSetRef>& retained_binding_sets() const;
        void retain_binding_packet(const std::shared_ptr<VulkanBindingPacket>& binding_packet);
        void retain_render_pass_resources(std::shared_ptr<VulkanRenderPassResources> resources);
        const std::vector<std::shared_ptr<VulkanRenderPassResources>>& retained_render_pass_resources() const;

        RHIAccess tracked_buffer_access(const std::shared_ptr<VulkanBuffer>& buffer) const;
        RHIResult<VulkanTextureSubresourceState> tracked_texture_state(
            const std::shared_ptr<VulkanTexture>& texture,
            const RHISubresourceRange& range) const;
        bool try_get_tracked_texture_state(
            const std::shared_ptr<VulkanTexture>& texture,
            VkImageLayout& layout,
            RHIAccess& access) const;
        void track_buffer_transition(const std::shared_ptr<VulkanBuffer>& buffer, RHIAccess access);
        void track_texture_transition(
            const std::shared_ptr<VulkanTexture>& texture,
            const RHISubresourceRange& range,
            VkImageLayout layout,
            RHIAccess access);
        RHIStatus validate_committed_resource_states() const;
        bool has_state_overlap(const VulkanCommandList& other) const;
        void commit_resource_states() const;

        RHIStatus begin_recording_by_context();
        RHIStatus close_by_context();
        RHIStatus mark_submitted_by_viewport();

    private:
        struct BufferState
        {
            std::shared_ptr<VulkanBuffer> resource;
            RHIAccess initial_access = RHIAccess::Unknown;
            RHIAccess final_access = RHIAccess::Unknown;
        };

        struct TextureState
        {
            std::shared_ptr<VulkanTexture> resource;
            struct Entry
            {
                RHITextureAspect aspect = RHITextureAspect::Color;
                std::uint32_t mip = 0;
                std::uint32_t layer = 0;
                VulkanTextureSubresourceState initial;
                VulkanTextureSubresourceState final;
            };
            std::vector<Entry> entries;
        };

        VulkanViewportContext* viewport_owner = nullptr;
        std::shared_ptr<VulkanCommandPool> owned_command_pool;
        VkCommandBuffer vk_command_buffer = VK_NULL_HANDLE;
        std::uint64_t command_frame_id = 0;
        std::vector<RHIResourceRef> resources;
        std::vector<std::shared_ptr<VulkanUploadPage>> upload_pages;
        std::vector<RHITextureViewRef> texture_views;
        std::vector<RHIGraphicsPipelineRef> graphics_pipelines;
        std::vector<RHIBindingSetRef> binding_sets;
        std::vector<std::shared_ptr<VulkanBindingPacket>> binding_packets;
        std::vector<std::shared_ptr<VulkanRenderPassResources>> render_pass_resources;
        std::unordered_map<const VulkanBuffer*, BufferState> buffer_states;
        std::unordered_map<const VulkanTexture*, TextureState> texture_states;
    };

    class VulkanGraphicsCommandContext final : public RHIGraphicsCommandContext
    {
    public:
        VulkanGraphicsCommandContext(
            VulkanDevice& device,
            VulkanViewportContext& viewport,
            VkCommandPool command_pool,
            std::uint64_t frame_id);
        VulkanGraphicsCommandContext(
            VulkanDevice& device,
            std::shared_ptr<VulkanCommandPool> command_pool);
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
        RHIStatus set_blend_constants(const vec4& constants) override;
        RHIStatus set_stencil_reference(std::uint8_t reference) override;
        RHIStatus set_vertex_buffers(
            const std::vector<RHIVertexBufferBinding>& bindings) override;
        RHIStatus set_index_buffer(const RHIIndexBufferBinding& binding) override;
        RHIStatus draw(const RHIDrawArgs& args) override;
        RHIStatus draw_indexed(const RHIDrawIndexedArgs& args) override;

    protected:
        RHIStatus bind_graphics_bindings_impl(
            const RHIGraphicsBindings& bindings) override;

    private:
        RHIStatus require_recording() const;
        RHIStatus flush_graphics_state(bool indexed_draw);
        RHIStatus unsupported_while_recording(const char* operation) const;

        VulkanDevice& vulkan_device;
        VulkanViewportContext* viewport_context = nullptr;
        std::shared_ptr<VulkanCommandPool> owned_command_pool;
        VkCommandPool vk_command_pool = VK_NULL_HANDLE;
        VkCommandBuffer vk_command_buffer = VK_NULL_HANDLE;
        std::uint64_t recording_frame_id = 0;
        std::shared_ptr<VulkanCommandList> recording_command_list;
        std::shared_ptr<VulkanRenderPassResources> active_render_pass;
        VulkanGraphicsState graphics_state;
        std::array<std::shared_ptr<VulkanBindingPacket>,
            VulkanBindingLayout::physical_set_count> active_binding_packets{};
    };
}
