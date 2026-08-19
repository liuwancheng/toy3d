#pragma once

#include "drivers/rhi/rhi_command_descriptors.h"
#include "drivers/rhi/rhi_result.h"

#include <memory>
#include <vector>

namespace toy3d
{
    class RHIQueue;

    class RHICommandList : public RHIObject
    {
    public:
        using RHIObject::RHIObject;
        ~RHICommandList() override = default;

        RHICommandListState state() const
        {
            return command_list_state;
        }

    protected:
        RHIStatus mark_recording()
        {
            if (command_list_state != RHICommandListState::Initial)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Only an initial command list can begin recording.");
            }
            command_list_state = RHICommandListState::Recording;
            return RHIStatus::success();
        }

        RHIStatus mark_closed()
        {
            if (command_list_state != RHICommandListState::Recording)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Only a recording command list can be closed.");
            }
            command_list_state = RHICommandListState::Closed;
            return RHIStatus::success();
        }

        RHIStatus mark_submitted()
        {
            if (command_list_state != RHICommandListState::Closed)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Only a closed command list can be submitted.");
            }
            command_list_state = RHICommandListState::Submitted;
            return RHIStatus::success();
        }

    private:
        friend class RHIQueue;
        RHICommandListState command_list_state = RHICommandListState::Initial;
    };

    using RHICommandListRef = std::shared_ptr<RHICommandList>;

    class RHICommandContext
    {
    public:
        RHICommandContext() = default;
        virtual ~RHICommandContext() = default;

        RHICommandContext(const RHICommandContext&) = delete;
        RHICommandContext& operator=(const RHICommandContext&) = delete;

        virtual RHIStatus begin_recording(const std::string& debug_name) = 0;
        virtual RHIStatus transition_resources(
            const std::vector<RHIResourceTransition>& transitions) = 0;
        virtual RHIStatus copy_buffer(const RHIBufferCopyDesc& desc) = 0;
        virtual RHIStatus upload_buffer(const RHIBufferUploadDesc& desc) = 0;
        virtual RHIStatus copy_texture(const RHITextureCopyDesc& desc) = 0;
        virtual RHIStatus upload_texture(const RHITextureUploadDesc& desc) = 0;
        virtual RHIStatus write_gpu_fence(
            const RHIGPUFenceRef& fence) = 0;
        virtual RHIResult<RHICommandListRef> finish_recording() = 0;
    };

    class RHIGraphicsCommandContext : public RHICommandContext
    {
    public:
        ~RHIGraphicsCommandContext() override = default;

        virtual RHIStatus begin_render_pass(const RHIRenderPassDesc& desc) = 0;
        virtual RHIStatus end_render_pass() = 0;

        virtual RHIStatus set_graphics_pipeline(
            const RHIGraphicsPipelineRef& pipeline) = 0;
        virtual RHIStatus set_viewport(const RHIViewport& viewport) = 0;
        virtual RHIStatus set_scissor(const RHIRect& rect) = 0;
        virtual RHIStatus set_blend_constants(const vec4& constants) = 0;
        virtual RHIStatus set_stencil_reference(std::uint8_t reference) = 0;
        virtual RHIStatus set_vertex_buffers(
            const std::vector<RHIVertexBufferBinding>& bindings) = 0;
        virtual RHIStatus set_index_buffer(
            const RHIIndexBufferBinding& binding) = 0;
        RHIStatus bind_graphics_bindings(const RHIGraphicsBindings& bindings);
        virtual RHIStatus draw(const RHIDrawArgs& args) = 0;
        virtual RHIStatus draw_indexed(const RHIDrawIndexedArgs& args) = 0;

    protected:
        virtual RHIStatus bind_graphics_bindings_impl(
            const RHIGraphicsBindings& bindings) = 0;
    };
}
