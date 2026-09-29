#include "drivers/rhi/rhi_command_context.h"

#include <array>

namespace toy3d
{
    namespace
    {
        RHIStatus foreign_object(const char* operation)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      std::string(operation) + " cannot use an object created by another device.");
        }
    } // namespace

    // --------------------------------------------------------------------------
    // RHICommandContext: shared validation before backend command recording
    // --------------------------------------------------------------------------
    RHIStatus RHICommandContext::transition_resources(const std::vector<RHIResourceTransition>& transitions)
    {
        for (const RHIResourceTransition& transition : transitions)
        {
            const RHIStatus validation = validate_resource_transition(transition);
            if (!validation)
            {
                return validation;
            }
            if (!transition.resource->is_owned_by(*owner_device()))
            {
                return foreign_object("Resource transition");
            }
        }
        return transition_resources_impl(transitions);
    }

    RHIStatus RHICommandContext::copy_buffer(const RHIBufferCopyDesc& desc)
    {
        const RHIStatus validation = validate_buffer_copy_desc(desc);
        if (!validation)
        {
            return validation;
        }
        if (!desc.source->is_owned_by(*owner_device()) || !desc.destination->is_owned_by(*owner_device()))
        {
            return foreign_object("Buffer copy");
        }
        return copy_buffer_impl(desc);
    }

    RHIStatus RHICommandContext::upload_buffer(const RHIBufferUploadDesc& desc)
    {
        const RHIStatus validation = validate_buffer_upload_desc(desc);
        if (!validation)
        {
            return validation;
        }
        if (!desc.destination->is_owned_by(*owner_device()))
        {
            return foreign_object("Buffer upload");
        }
        return upload_buffer_impl(desc);
    }

    RHIResult<RHIUniformBufferSlice>
    RHICommandContext::upload_transient_uniform_data(const RHITransientUniformDataDesc& desc)
    {
        const RHIStatus validation = validate_transient_uniform_data_desc(desc);
        if (!validation)
        {
            return RHIResult<RHIUniformBufferSlice>::failure(validation.code(), validation.message());
        }
        RHIResult<RHIUniformBufferSlice> result = upload_transient_uniform_data_impl(desc);
        if (result && (!result.value().buffer || !result.value().buffer->is_owned_by(*owner_device())))
        {
            return RHIResult<RHIUniformBufferSlice>::failure(
                RHIErrorCode::BackendFailure,
                "Transient uniform upload returned no buffer or a buffer created by another device.");
        }
        return result;
    }

    RHIResult<RHIUniformBufferSlice>
    RHICommandContext::upload_transient_uniform_data_impl(const RHITransientUniformDataDesc& desc)
    {
        (void)desc;
        return RHIResult<RHIUniformBufferSlice>::failure(
            RHIErrorCode::Unsupported, "Transient uniform data is unsupported by this RHI backend.");
    }

    RHIStatus RHICommandContext::copy_texture(const RHITextureCopyDesc& desc)
    {
        const RHIStatus validation = validate_texture_copy_desc(desc);
        if (!validation)
        {
            return validation;
        }
        if (!desc.source.texture->is_owned_by(*owner_device()) ||
            !desc.destination.texture->is_owned_by(*owner_device()))
        {
            return foreign_object("Texture copy");
        }
        return copy_texture_impl(desc);
    }

    RHIStatus RHICommandContext::upload_texture(const RHITextureUploadDesc& desc)
    {
        const RHIStatus validation = validate_texture_upload_desc(desc);
        if (!validation)
        {
            return validation;
        }
        if (!desc.destination.texture->is_owned_by(*owner_device()))
        {
            return foreign_object("Texture upload");
        }
        return upload_texture_impl(desc);
    }

    RHIStatus RHICommandContext::readback_texture_pixel(const RHITexturePixelReadbackDesc& desc)
    {
        const RHIStatus validation = validate_texture_pixel_readback_desc(desc);
        if (!validation)
            return validation;
        if (!desc.source.texture->is_owned_by(*owner_device()) ||
            !desc.destination->is_owned_by(*owner_device()))
        {
            return foreign_object("Pixel readback");
        }
        return readback_texture_pixel_impl(desc);
    }

    RHIStatus RHICommandContext::readback_texture_pixel_impl(const RHITexturePixelReadbackDesc&)
    {
        return RHIStatus::failure(RHIErrorCode::Unsupported,
                                  "This RHI backend does not support pixel readback.");
    }

    RHIStatus RHICommandContext::readback_texture(const RHITextureReadbackDesc& desc)
    {
        const RHIStatus validation = validate_texture_readback_desc(desc);
        if (!validation) return validation;
        if (!desc.source.texture->is_owned_by(*owner_device()) || !desc.destination->is_owned_by(*owner_device()))
            return foreign_object("Texture readback");
        if (desc.destination->copy_recorded_ || desc.destination->last_use_completion_value() != 0)
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Texture readback is single-use.");
        const auto status = readback_texture_impl(desc);
        if (status) desc.destination->copy_recorded_ = true;
        return status;
    }

    RHIStatus RHICommandContext::readback_texture_impl(const RHITextureReadbackDesc&)
    {
        return RHIStatus::failure(RHIErrorCode::Unsupported, "This RHI backend does not support color readback.");
    }

    RHIStatus RHICommandContext::write_gpu_fence(const RHIGPUFenceRef& fence)
    {
        if (fence && !fence->is_owned_by(*owner_device()))
        {
            return foreign_object("GPU fence write");
        }
        return write_gpu_fence_impl(fence);
    }

    // --------------------------------------------------------------------------
    // RHIGraphicsCommandContext: shared graphics-command validation
    // --------------------------------------------------------------------------
    RHIStatus RHIGraphicsCommandContext::begin_render_pass(const RHIRenderPassDesc& desc)
    {
        const RHIStatus validation = validate_render_pass_desc(desc);
        if (!validation)
        {
            return validation;
        }
        for (const RHIColorAttachmentDesc& attachment : desc.color_attachments)
        {
            if (!attachment.view->is_owned_by(*owner_device()) ||
                (attachment.resolve_view && !attachment.resolve_view->is_owned_by(*owner_device())))
            {
                return foreign_object("Render pass");
            }
        }
        if (desc.has_depth_stencil_attachment &&
            !desc.depth_stencil_attachment.view->is_owned_by(*owner_device()))
        {
            return foreign_object("Render pass");
        }
        return begin_render_pass_impl(desc);
    }

    RHIStatus RHIGraphicsCommandContext::set_graphics_pipeline(const RHIGraphicsPipelineRef& pipeline)
    {
        if (!pipeline)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Graphics pipeline is required.");
        }
        if (!pipeline->is_owned_by(*owner_device()))
        {
            return foreign_object("Graphics pipeline binding");
        }
        return set_graphics_pipeline_impl(pipeline);
    }

    RHIStatus RHIGraphicsCommandContext::set_vertex_buffers(const std::vector<RHIVertexBufferBinding>& bindings)
    {
        for (const RHIVertexBufferBinding& binding : bindings)
        {
            if (!binding.buffer)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Vertex-buffer binding requires a buffer.");
            }
            if (!binding.buffer->is_owned_by(*owner_device()))
            {
                return foreign_object("Vertex-buffer binding");
            }
        }
        return set_vertex_buffers_impl(bindings);
    }

    RHIStatus RHIGraphicsCommandContext::set_index_buffer(const RHIIndexBufferBinding& binding)
    {
        if (!binding.buffer)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Index-buffer binding requires a buffer.");
        }
        if (!binding.buffer->is_owned_by(*owner_device()))
        {
            return foreign_object("Index-buffer binding");
        }
        return set_index_buffer_impl(binding);
    }

    RHIStatus RHIGraphicsCommandContext::bind_graphics_bindings(const RHIGraphicsBindings& bindings)
    {
        const RHIStatus validation = validate_graphics_bindings(bindings);
        if (!validation)
        {
            return validation;
        }
        const std::array<RHIBindingSetRef, static_cast<std::size_t>(RHIBindingGroup::Max)> sets = {
            bindings.global, bindings.view, bindings.pass, bindings.material, bindings.object};
        for (const RHIBindingSetRef& set : sets)
        {
            if (set && !set->is_owned_by(*owner_device()))
            {
                return foreign_object("Graphics binding");
            }
        }
        return bind_graphics_bindings_impl(bindings);
    }
} // namespace toy3d
