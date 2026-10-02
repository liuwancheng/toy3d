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
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Only an initial command list can begin recording.");
            }
            command_list_state = RHICommandListState::Recording;
            return RHIStatus::success();
        }

        RHIStatus mark_closed()
        {
            if (command_list_state != RHICommandListState::Recording)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Only a recording command list can be closed.");
            }
            command_list_state = RHICommandListState::Closed;
            return RHIStatus::success();
        }

        RHIStatus mark_submitted()
        {
            if (command_list_state != RHICommandListState::Closed)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Only a closed command list can be submitted.");
            }
            command_list_state = RHICommandListState::Submitted;
            return RHIStatus::success();
        }

      private:
        friend class RHIQueue;

        void publish_submitted()
        {
            command_list_state = RHICommandListState::Submitted;
        }

        RHICommandListState command_list_state = RHICommandListState::Initial;
    };

    using RHICommandListRef = std::shared_ptr<RHICommandList>;

    class RHICommandContext : public RHIObject
    {
      public:
        explicit RHICommandContext(const RHIDevice& owner) : RHIObject(owner)
        {
        }
        virtual ~RHICommandContext() = default;

        RHICommandContext(const RHICommandContext&) = delete;
        RHICommandContext& operator=(const RHICommandContext&) = delete;

        virtual RHIStatus begin_recording(const std::string& debug_name) = 0;
        RHIStatus transition_resources(const std::vector<RHIResourceTransition>& transitions);
        RHIStatus copy_buffer(const RHIBufferCopyDesc& desc);
        RHIStatus upload_buffer(const RHIBufferUploadDesc& desc);
        RHIResult<RHIUniformBufferSlice> upload_transient_uniform_data(const RHITransientUniformDataDesc& desc);
        RHIStatus copy_texture(const RHITextureCopyDesc& desc);
        RHIStatus readback_texture_pixel(const RHITexturePixelReadbackDesc& desc);
        RHIStatus readback_texture(const RHITextureReadbackDesc& desc);
        RHIStatus upload_texture(const RHITextureUploadDesc& desc);
        RHIStatus write_gpu_fence(const RHIGPUFenceRef& fence);
        virtual RHIResult<RHICommandListRef> finish_recording() = 0;

      protected:
        virtual RHIStatus transition_resources_impl(const std::vector<RHIResourceTransition>& transitions) = 0;
        virtual RHIStatus copy_buffer_impl(const RHIBufferCopyDesc& desc) = 0;
        virtual RHIStatus upload_buffer_impl(const RHIBufferUploadDesc& desc) = 0;
        virtual RHIResult<RHIUniformBufferSlice> upload_transient_uniform_data_impl(
            const RHITransientUniformDataDesc& desc);
        virtual RHIStatus copy_texture_impl(const RHITextureCopyDesc& desc) = 0;
        virtual RHIStatus readback_texture_pixel_impl(const RHITexturePixelReadbackDesc& desc);
        virtual RHIStatus readback_texture_impl(const RHITextureReadbackDesc& desc);
        virtual RHIStatus upload_texture_impl(const RHITextureUploadDesc& desc) = 0;
        virtual RHIStatus write_gpu_fence_impl(const RHIGPUFenceRef& fence) = 0;
    };

    class RHIGraphicsCommandContext : public RHICommandContext
    {
      public:
        explicit RHIGraphicsCommandContext(const RHIDevice& owner) : RHICommandContext(owner)
        {
        }

        ~RHIGraphicsCommandContext() override = default;

        RHIStatus begin_render_pass(const RHIRenderPassDesc& desc);
        virtual RHIStatus end_render_pass() = 0;

        RHIStatus set_graphics_pipeline(const RHIGraphicsPipelineRef& pipeline);
        virtual RHIStatus set_viewport(const RHIViewport& viewport) = 0;
        virtual RHIStatus set_scissor(const RHIRect& rect) = 0;
        virtual RHIStatus set_blend_constants(const vec4& constants) = 0;
        virtual RHIStatus set_stencil_reference(std::uint8_t reference) = 0;
        RHIStatus set_vertex_buffers(const std::vector<RHIVertexBufferBinding>& bindings);
        RHIStatus set_index_buffer(const RHIIndexBufferBinding& binding);
        RHIStatus bind_graphics_bindings(const RHIGraphicsBindings& bindings);
        virtual RHIStatus draw(const RHIDrawArgs& args) = 0;
        virtual RHIStatus draw_indexed(const RHIDrawIndexedArgs& args) = 0;

      protected:
        virtual RHIStatus begin_render_pass_impl(const RHIRenderPassDesc& desc) = 0;
        virtual RHIStatus set_graphics_pipeline_impl(const RHIGraphicsPipelineRef& pipeline) = 0;
        virtual RHIStatus set_vertex_buffers_impl(const std::vector<RHIVertexBufferBinding>& bindings) = 0;
        virtual RHIStatus set_index_buffer_impl(const RHIIndexBufferBinding& binding) = 0;
        virtual RHIStatus bind_graphics_bindings_impl(const RHIGraphicsBindings& bindings) = 0;
    };
} // namespace toy3d
