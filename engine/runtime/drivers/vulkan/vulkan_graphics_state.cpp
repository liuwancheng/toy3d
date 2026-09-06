#include "drivers/vulkan/vulkan_graphics_state.h"

#include <utility>

namespace toy3d
{
    void VulkanGraphicsState::reset()
    {
        graphics_pipeline.reset();
        graphics_bindings = {};
        vertex_buffer_bindings.clear();
        index_buffer_binding = {};
        current_viewport = {};
        current_scissor = {};
        current_blend_constants = vec4(0.0F);
        current_stencil_reference = 0;
        index_buffer_set = false;
        viewport_set = false;
        scissor_set = false;
        blend_constants_set = false;
        stencil_reference_set = false;
        state_dirty_flags = VulkanGraphicsStateDirty::All;
    }

    void VulkanGraphicsState::set_pipeline(RHIGraphicsPipelineRef pipeline)
    {
        graphics_pipeline = std::move(pipeline);
        mark_dirty(VulkanGraphicsStateDirty::Pipeline);
    }

    void VulkanGraphicsState::set_graphics_bindings(RHIGraphicsBindings bindings)
    {
        graphics_bindings = std::move(bindings);
        mark_dirty(VulkanGraphicsStateDirty::Bindings);
    }

    void VulkanGraphicsState::set_vertex_buffers(std::vector<RHIVertexBufferBinding> bindings)
    {
        vertex_buffer_bindings = std::move(bindings);
        mark_dirty(VulkanGraphicsStateDirty::VertexBuffers);
    }

    void VulkanGraphicsState::set_index_buffer(RHIIndexBufferBinding binding)
    {
        index_buffer_binding = std::move(binding);
        index_buffer_set = true;
        mark_dirty(VulkanGraphicsStateDirty::IndexBuffer);
    }

    void VulkanGraphicsState::set_viewport(const RHIViewport& viewport)
    {
        current_viewport = viewport;
        viewport_set = true;
        mark_dirty(VulkanGraphicsStateDirty::Viewport);
    }

    void VulkanGraphicsState::set_scissor(const RHIRect& scissor)
    {
        current_scissor = scissor;
        scissor_set = true;
        mark_dirty(VulkanGraphicsStateDirty::Scissor);
    }

    void VulkanGraphicsState::set_blend_constants(const vec4& constants)
    {
        current_blend_constants = constants;
        blend_constants_set = true;
        mark_dirty(VulkanGraphicsStateDirty::BlendConstants);
    }

    void VulkanGraphicsState::set_stencil_reference(std::uint8_t reference)
    {
        current_stencil_reference = reference;
        stencil_reference_set = true;
        mark_dirty(VulkanGraphicsStateDirty::StencilReference);
    }

    VulkanGraphicsStateDirty VulkanGraphicsState::dirty_flags() const
    {
        return state_dirty_flags;
    }

    void VulkanGraphicsState::clear_dirty_flags(VulkanGraphicsStateDirty flags)
    {
        state_dirty_flags &= ~flags;
    }

    const RHIGraphicsPipelineRef& VulkanGraphicsState::pipeline() const
    {
        return graphics_pipeline;
    }

    const RHIGraphicsBindings& VulkanGraphicsState::bindings() const
    {
        return graphics_bindings;
    }

    const std::vector<RHIVertexBufferBinding>& VulkanGraphicsState::vertex_buffers() const
    {
        return vertex_buffer_bindings;
    }

    const RHIIndexBufferBinding& VulkanGraphicsState::index_buffer() const
    {
        return index_buffer_binding;
    }

    const RHIViewport& VulkanGraphicsState::viewport() const
    {
        return current_viewport;
    }

    const RHIRect& VulkanGraphicsState::scissor() const
    {
        return current_scissor;
    }

    const vec4& VulkanGraphicsState::blend_constants() const
    {
        return current_blend_constants;
    }

    std::uint8_t VulkanGraphicsState::stencil_reference() const
    {
        return current_stencil_reference;
    }

    bool VulkanGraphicsState::has_index_buffer() const
    {
        return index_buffer_set;
    }

    bool VulkanGraphicsState::has_viewport() const
    {
        return viewport_set;
    }

    bool VulkanGraphicsState::has_scissor() const
    {
        return scissor_set;
    }

    bool VulkanGraphicsState::has_blend_constants() const
    {
        return blend_constants_set;
    }

    bool VulkanGraphicsState::has_stencil_reference() const
    {
        return stencil_reference_set;
    }

    void VulkanGraphicsState::mark_dirty(VulkanGraphicsStateDirty flags)
    {
        state_dirty_flags |= flags;
    }
} // namespace toy3d
