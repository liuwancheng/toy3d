#include "renderscene/pass/shadow_pass.h"

#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/shader/shader_parameters.h"
#include "renderscene/geometry/static_mesh_render_data.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/pass/mesh_draw_command.h"
#include "renderscene/shadow_render_targets.h"
#include "renderscene/view/view_info.h"
#include "shader_parameters/toy3d_shadowdepth_default.generated.h"

namespace toy3d
{
    RHIStatus render_shadow_pass(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                 RHIGraphicsCommandContext& context, const ViewInfo& view, std::size_t cascade_index,
                                 const RHITextureRef& texture, const RHITextureViewRef& depth_view,
                                 RHIAccess before_access, const ShaderMapProgramRef& shader_program)
    {
        if (!context.is_owned_by(device) || !texture || !depth_view || depth_view->texture() != texture ||
            cascade_index >= ShadowRenderTargets::k_cascade_count)
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "ShadowPass target or context is invalid.");
        const ShadowCascadeInfo& cascade = view.shadow_cascade(cascade_index);
        if (view.shadow_active() && !shader_program)
            return RHIStatus::failure(RHIErrorCode::NotReady, "ShadowPass ShaderMap program is unavailable.");

        RHIBindingSetRef pass_binding;
        RHIShaderProgramRef rhi_program;
        std::vector<MeshDrawCommand> commands;
        if (view.shadow_active() && !cascade.batches.empty())
        {
            ShadowDepthPassParameters parameters;
            parameters.shadow_world_to_clip = cascade.world_to_clip;
            parameters.shadow_light_direction = cascade.light_direction;
            parameters.shadow_bias_parameters = cascade.bias_parameters;
            auto created_binding = create_transient_shader_binding(device, context, parameters);
            if (!created_binding) return created_binding.status();
            pass_binding = std::move(created_binding).value();
            auto created_program = shader_program_cache.find_or_create(shader_program);
            if (!created_program) return created_program.status();
            rhi_program = std::move(created_program).value();
            commands.reserve(cascade.batches.size());
            for (const MeshBatch& batch : cascade.batches)
            {
                if (!batch.object_binding())
                    return RHIStatus::failure(RHIErrorCode::NotReady, "Shadow caster lacks its Object binding.");
                std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout> layouts;
                std::vector<RHIGraphicsPipelineDesc::VertexAttribute> attributes;
                std::vector<RHIVertexBufferBinding> buffers;
                RHIStatus status = batch.vertex_factory().build_vertex_input(shader_program->data().vertex_inputs,
                                                                               layouts, attributes, buffers);
                if (!status) return status;
                RHIGraphicsPipelineDesc pipeline_desc;
                pipeline_desc.vertex_shader = rhi_program->vertex_shader;
                pipeline_desc.pixel_shader = rhi_program->pixel_shader;
                pipeline_desc.binding_layout = rhi_program->binding_layout;
                pipeline_desc.vertex_buffers = std::move(layouts);
                pipeline_desc.vertex_attributes = std::move(attributes);
                pipeline_desc.color_attachment_count = 0u;
                pipeline_desc.depth_stencil_format = PixelFormat::D32Float;
                pipeline_desc.sample_count = 1u;
                pipeline_desc.debug_name = "ShadowPass.Opaque";
                shader::ShaderGraphicsPassState state = shader_program->data().graphics_pass_state;
                const auto* material_state = batch.material_render_proxy().effective_graphics_pass_state();
                if (material_state && material_state->cull_mode == shader::ShaderGraphicsPassState::CullMode::None)
                    state.cull_mode = shader::ShaderGraphicsPassState::CullMode::None;
                auto shader_pipeline = build_shader_graphics_pipeline_desc(pipeline_desc, state);
                if (!shader_pipeline) return shader_pipeline.status();
                auto pipeline = device.create_graphics_pipeline(std::move(shader_pipeline).value());
                if (!pipeline) return pipeline.status();
                MeshDrawCommand command;
                command.pipeline = std::move(pipeline).value();
                command.vertex_buffers = std::move(buffers);
                command.index_buffer = batch.render_data().index_buffer_binding();
                command.bindings.pass = pass_binding;
                command.bindings.object = batch.object_binding();
                command.draw_args.index_count = batch.index_count();
                command.draw_args.first_index = batch.first_index();
                commands.push_back(std::move(command));
            }
        }

        if (before_access != RHIAccess::DepthStencilWrite)
        {
            RHIResourceTransition transition;
            transition.resource = texture;
            transition.subresources = depth_view->desc().subresources;
            transition.before = before_access;
            transition.after = RHIAccess::DepthStencilWrite;
            const RHIStatus status = context.transition_resources({transition});
            if (!status) return status;
        }
        RHIRenderPassDesc pass;
        pass.has_depth_stencil_attachment = true;
        pass.depth_stencil_attachment.view = depth_view;
        pass.depth_stencil_attachment.depth_load = RHILoadOperation::Clear;
        pass.depth_stencil_attachment.depth_store = RHIStoreOperation::Store;
        pass.depth_stencil_attachment.stencil_load = RHILoadOperation::Discard;
        pass.depth_stencil_attachment.stencil_store = RHIStoreOperation::Discard;
        pass.depth_stencil_attachment.clear_value = RHIClearValue::DepthZero;
        pass.debug_name = "DirectionalShadowPass";
        RHIStatus status = context.begin_render_pass(pass);
        if (!status) return status;
        RHIViewport viewport;
        viewport.width = static_cast<float>(ShadowRenderTargets::k_resolution);
        viewport.height = static_cast<float>(ShadowRenderTargets::k_resolution);
        RHIRect scissor;
        scissor.width = ShadowRenderTargets::k_resolution;
        scissor.height = ShadowRenderTargets::k_resolution;
        for (const MeshDrawCommand& command : commands)
        {
            status = context.set_graphics_pipeline(command.pipeline);
            if (status) status = context.set_viewport(viewport);
            if (status) status = context.set_scissor(scissor);
            if (status) status = context.set_blend_constants(vec4(1, 1, 1, 1));
            if (status) status = context.set_stencil_reference(0u);
            if (status) status = context.set_vertex_buffers(command.vertex_buffers);
            if (status) status = context.set_index_buffer(command.index_buffer);
            if (status) status = context.bind_graphics_bindings(command.bindings);
            if (status) status = context.draw_indexed(command.draw_args);
            if (!status) break;
        }
        const RHIStatus ended = context.end_render_pass();
        if (!status) return status;
        if (!ended) return ended;
        RHIResourceTransition to_read;
        to_read.resource = texture;
        to_read.subresources = depth_view->desc().subresources;
        to_read.before = RHIAccess::DepthStencilWrite;
        to_read.after = RHIAccess::ShaderResourceGraphics;
        return context.transition_resources({to_read});
    }
} // namespace toy3d
