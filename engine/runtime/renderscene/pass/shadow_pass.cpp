#include "renderscene/pass/shadow_pass.h"

#include <array>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/geometry/vertex_factory.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/shader/shader_parameters.h"
#include "rendercore/material/material_render_proxy.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/pass/mesh_draw_command.h"
#include "renderscene/shadow_render_targets.h"
#include "renderscene/view/view_info.h"
#include "shader_parameters/toy3d_shadowdepth_default.generated.h"

namespace toy3d
{
    RHIStatus render_shadow_pass(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                 RHIGraphicsCommandContext& context, const ViewInfo& view,
                                 const ShadowRenderTargets& targets, std::size_t view_index,
                                 const ShaderMapProgramRef& shader_program)
    {
        if (view_index >= targets.view_count())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "ShadowPass View index is out of range.");
        }
        const RHITextureRef& texture = targets.texture(view_index);
        const RHITextureViewRef& depth_view = targets.depth_view(view_index);
        const ShadowAtlasLayout& layout = targets.layout();
        const RHIAccess before_access = targets.access(view_index);
        if (!context.is_owned_by(device) || !texture || !depth_view || depth_view->texture() != texture ||
            layout.cascade_count < 1u || layout.cascade_count > ShadowRenderTargets::k_max_cascade_count ||
            view.shadow_cascade_count() > layout.cascade_count)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "ShadowPass target or context is invalid.");
        }
        if (view.shadow_active() && !shader_program)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "ShadowPass ShaderMap program is unavailable.");
        }

        // Prepare uploads and pipelines before beginning the shared atlas render pass.
        std::array<std::vector<MeshDrawCommand>, ShadowRenderTargets::k_max_cascade_count> cascade_commands;
        for (std::size_t index = 0u; index < view.shadow_cascade_count(); ++index)
        {
            const ShadowCascadeInfo& cascade = view.shadow_cascade(index);
            if (!view.shadow_active() || cascade.batches.empty())
            {
                continue;
            }
            RHIBindingSetRef pass_binding;
            RHIShaderProgramRef rhi_program;
            std::vector<MeshDrawCommand>& commands = cascade_commands[index];
            ShadowDepthPassParameters parameters;
            parameters.shadow_world_to_clip = cascade.world_to_clip;
            parameters.shadow_light_direction = cascade.light_direction;
            parameters.shadow_bias_parameters = cascade.bias_parameters;
            auto created_binding = create_transient_shader_binding(device, context, parameters);
            if (!created_binding)
            {
                return created_binding.status();
            }
            pass_binding = std::move(created_binding).value();
            commands.reserve(cascade.batches.size());
            for (const MeshBatch& batch : cascade.batches)
            {
                const auto selected = batch.resolve_program(shader_program);
                if (!selected.succeeded())
                {
                    return RHIStatus::failure(RHIErrorCode::Unsupported, selected.error);
                }
                const auto created_program = shader_program_cache.find_or_create(selected.program);
                if (!created_program)
                {
                    return created_program.status();
                }
                rhi_program = created_program.value();
                if (!batch.object_binding())
                {
                    return RHIStatus::failure(RHIErrorCode::NotReady, "Shadow caster lacks its Object binding.");
                }
                std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout> layouts;
                std::vector<RHIGraphicsPipelineDesc::VertexAttribute> attributes;
                std::vector<RHIVertexBufferBinding> buffers;
                RHIStatus status = batch.vertex_factory().build_vertex_input(selected.program->data().vertex_inputs,
                                                                             layouts, attributes, buffers);
                if (!status)
                {
                    return status;
                }
                RHIGraphicsPipelineDesc pipeline_desc;
                pipeline_desc.vertex_shader = rhi_program->vertex_shader;
                pipeline_desc.pixel_shader = rhi_program->pixel_shader;
                pipeline_desc.binding_layout = rhi_program->binding_layout;
                pipeline_desc.vertex_buffers = std::move(layouts);
                pipeline_desc.vertex_attributes = std::move(attributes);
                pipeline_desc.color_attachment_count = 0u;
                pipeline_desc.depth_stencil_format = PixelFormat::D32Float;
                pipeline_desc.sample_count = 1u;
                pipeline_desc.debug_name = "ShadowPass.Default";
                shader::ShaderGraphicsPassState state = selected.program->data().graphics_pass_state;
                const auto* material_state = batch.material_render_proxy().effective_graphics_pass_state();
                if (material_state && material_state->cull_mode == shader::ShaderGraphicsPassState::CullMode::None)
                {
                    state.cull_mode = shader::ShaderGraphicsPassState::CullMode::None;
                }
                auto shader_pipeline = build_shader_graphics_pipeline_desc(pipeline_desc, state);
                if (!shader_pipeline)
                {
                    return shader_pipeline.status();
                }
                auto pipeline = device.create_graphics_pipeline(std::move(shader_pipeline).value());
                if (!pipeline)
                {
                    return pipeline.status();
                }
                MeshDrawCommand command;
                command.pipeline = std::move(pipeline).value();
                command.vertex_buffers = std::move(buffers);
                command.index_buffer = batch.index_buffer_binding();
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
            if (!status)
            {
                return status;
            }
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
        if (!status)
        {
            return status;
        }
        for (std::size_t index = 0u; index < view.shadow_cascade_count(); ++index)
        {
            const ShadowCascadeTile& tile = layout.tiles[index];
            RHIViewport viewport;
            viewport.x = static_cast<float>(tile.x + ShadowCascadeTile::k_border);
            viewport.y = static_cast<float>(tile.y + ShadowCascadeTile::k_border);
            viewport.width = static_cast<float>(tile.resolution());
            viewport.height = viewport.width;
            RHIRect scissor;
            scissor.x = static_cast<std::int32_t>(tile.x + ShadowCascadeTile::k_border);
            scissor.y = static_cast<std::int32_t>(tile.y + ShadowCascadeTile::k_border);
            scissor.width = tile.resolution();
            scissor.height = tile.resolution();
            for (const MeshDrawCommand& command : cascade_commands[index])
            {
                status = context.set_graphics_pipeline(command.pipeline);
                if (status)
                {
                    status = context.set_viewport(viewport);
                }
                if (status)
                {
                    status = context.set_scissor(scissor);
                }
                if (status)
                {
                    status = context.set_blend_constants(vec4(1, 1, 1, 1));
                }
                if (status)
                {
                    status = context.set_stencil_reference(0u);
                }
                if (status)
                {
                    status = context.set_vertex_buffers(command.vertex_buffers);
                }
                if (status)
                {
                    status = context.set_index_buffer(command.index_buffer);
                }
                if (status)
                {
                    status = context.bind_graphics_bindings(command.bindings);
                }
                if (status)
                {
                    status = context.draw_indexed(command.draw_args);
                }
                if (!status)
                {
                    break;
                }
            }
            if (!status)
            {
                break;
            }
        }
        const RHIStatus ended = context.end_render_pass();
        if (!status)
        {
            return status;
        }
        if (!ended)
        {
            return ended;
        }
        RHIResourceTransition to_read;
        to_read.resource = texture;
        to_read.subresources = depth_view->desc().subresources;
        to_read.before = RHIAccess::DepthStencilWrite;
        to_read.after = RHIAccess::ShaderResourceGraphics;
        return context.transition_resources({to_read});
    }
} // namespace toy3d
