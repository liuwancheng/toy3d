#pragma once

#include "drivers/rhi/rhi_command_descriptors.h"

#include <cstdint>
#include <vector>

namespace toy3d
{
    enum class VulkanGraphicsStateDirty : std::uint32_t
    {
        None = 0,
        Pipeline = 1U << 0,
        Bindings = 1U << 1,
        VertexBuffers = 1U << 2,
        IndexBuffer = 1U << 3,
        Viewport = 1U << 4,
        Scissor = 1U << 5,
        All = 0xffffffffU
    };

    // Mutable state belongs to one command context. It is intentionally not an
    // RHI resource and must never be shared by parallel pass recorders.
    class VulkanGraphicsState final
    {
    public:
        void reset();
        void set_pipeline(RHIGraphicsPipelineRef pipeline);
        void set_binding_set(RHIBindingSetRef binding_set);
        void set_vertex_buffers(std::vector<RHIVertexBufferBinding> bindings);
        void set_index_buffer(RHIIndexBufferBinding binding);
        void set_viewport(const RHIViewport& viewport);
        void set_scissor(const RHIRect& scissor);

        VulkanGraphicsStateDirty dirty_flags() const;
        void clear_dirty_flags(VulkanGraphicsStateDirty flags);

        const RHIGraphicsPipelineRef& pipeline() const;
        const std::vector<RHIBindingSetRef>& binding_sets() const;
        const std::vector<RHIVertexBufferBinding>& vertex_buffers() const;
        const RHIIndexBufferBinding& index_buffer() const;
        const RHIViewport& viewport() const;
        const RHIRect& scissor() const;
        bool has_index_buffer() const;
        bool has_viewport() const;
        bool has_scissor() const;

    private:
        void mark_dirty(VulkanGraphicsStateDirty flags);

        RHIGraphicsPipelineRef graphics_pipeline;
        std::vector<RHIBindingSetRef> resource_binding_sets;
        std::vector<RHIVertexBufferBinding> vertex_buffer_bindings;
        RHIIndexBufferBinding index_buffer_binding;
        RHIViewport current_viewport;
        RHIRect current_scissor;
        bool index_buffer_set = false;
        bool viewport_set = false;
        bool scissor_set = false;
        VulkanGraphicsStateDirty state_dirty_flags = VulkanGraphicsStateDirty::All;
    };
}
