#include "renderscene/pass/base_pass.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "logging/logger.h"
#include "rendercore/geometry/vertex_factory.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/material/material_render_proxy.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/pass/mesh_draw_command.h"
#include "renderscene/view/view_info.h"

namespace toy3d
{
    namespace
    {
        RHIStatus apply_attachment_compatibility(const RHIRenderPassDesc& pass_desc,
                                                 RHIGraphicsPipelineDesc& pipeline_desc)
        {
            if (pass_desc.color_attachments.empty() || !pass_desc.has_depth_stencil_attachment)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Forward Base Pass requires color and depth attachments.");
            }
            if (pass_desc.color_attachments.size() > pipeline_desc.color_formats.size())
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Forward Base Pass has too many color attachments.");
            }

            pipeline_desc.color_attachment_count = static_cast<std::uint32_t>(pass_desc.color_attachments.size());
            for (std::size_t index = 0; index < pass_desc.color_attachments.size(); ++index)
            {
                const RHITextureViewRef& view = pass_desc.color_attachments[index].view;
                if (!view || !view->texture())
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                              "Forward Base Pass color attachment is unavailable.");
                }
                pipeline_desc.color_formats[index] = view->desc().format;
                if (index == 0u)
                {
                    pipeline_desc.sample_count = view->texture()->desc().sample_count;
                }
                else if (view->texture()->desc().sample_count != pipeline_desc.sample_count)
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                              "Forward Base Pass color sample counts do not match.");
                }
            }

            const RHITextureViewRef& depth_view = pass_desc.depth_stencil_attachment.view;
            if (!depth_view || !depth_view->texture() ||
                depth_view->texture()->desc().sample_count != pipeline_desc.sample_count)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Forward Base Pass depth attachment is unavailable or incompatible.");
            }
            pipeline_desc.depth_stencil_format = depth_view->desc().format;
            return RHIStatus::success();
        }

        RHIStatus make_render_pass_desc(const BasePassInputs& inputs, RHIRenderPassDesc& pass_desc)
        {
            if (!inputs.scene_color || !inputs.scene_depth || !inputs.scene_color->texture() ||
                !inputs.scene_depth->texture())
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Forward Base Pass requires color and depth attachment views.");
            }

            RHIColorAttachmentDesc color_attachment;
            color_attachment.view = inputs.scene_color;
            color_attachment.load = RHILoadOperation::Clear;
            color_attachment.store = RHIStoreOperation::Store;
            color_attachment.clear_value = RHIClearValue::color_value(inputs.clear_color);
            pass_desc.color_attachments.push_back(std::move(color_attachment));
            pass_desc.has_depth_stencil_attachment = true;
            pass_desc.depth_stencil_attachment.view = inputs.scene_depth;
            pass_desc.depth_stencil_attachment.depth_load = RHILoadOperation::Clear;
            pass_desc.depth_stencil_attachment.depth_store = RHIStoreOperation::Store;
            pass_desc.depth_stencil_attachment.stencil_load = RHILoadOperation::Discard;
            pass_desc.depth_stencil_attachment.stencil_store = RHIStoreOperation::Discard;
            pass_desc.depth_stencil_attachment.clear_value = RHIClearValue::DepthZero;
            pass_desc.debug_name = "ForwardBasePass";
            return validate_render_pass_desc(pass_desc);
        }
    } // namespace

    RHIStatus render_base_pass(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                               RHIGraphicsCommandContext& context, const BasePassInputs& inputs)
    {
        if (!context.is_owned_by(device))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Forward Base Pass context is not owned by the injected device.");
        }
        RHIRenderPassDesc pass_desc;
        RHIStatus status = make_render_pass_desc(inputs, pass_desc);
        if (!status)
        {
            return status;
        }

        std::vector<MeshPassDrawList> draw_lists;
        draw_lists.reserve(inputs.views.size());
        if (inputs.lighting_bindings.size() != inputs.views.size())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Forward Base Pass requires one Pass binding slot per View.");
        }
        for (std::size_t view_index = 0; view_index < inputs.views.size(); ++view_index)
        {
            const ViewInfo& view_info = inputs.views[view_index];
            const IntRect& view_rect = view_info.scene_view().view_rect();
            MeshPassDrawList draw_list;
            draw_list.viewport.x = static_cast<float>(view_rect.x);
            draw_list.viewport.y = static_cast<float>(view_rect.y);
            draw_list.viewport.width = static_cast<float>(view_rect.width);
            draw_list.viewport.height = static_cast<float>(view_rect.height);
            draw_list.scissor = view_rect;
            draw_list.commands.reserve(view_info.mesh_batches().size());

            for (std::size_t batch_index = 0; batch_index < view_info.mesh_batches().size(); ++batch_index)
            {
                const MeshBatch& mesh_batch = view_info.mesh_batches()[batch_index];
                MaterialRenderProxy& material_proxy = mesh_batch.material_render_proxy();
                const auto selected = mesh_batch.material_program();
                if (!selected.succeeded())
                {
                    if (mesh_batch.bone_matrices() || inputs.require_complete_meshes)
                    {
                        return RHIStatus::failure(RHIErrorCode::Unsupported, selected.error);
                    }
                    TOY_LOG_ERROR("Base Pass mesh shader is unavailable: {}", selected.error);
                    continue;
                }
                const ShaderMapProgramRef& shader_program = selected.program;
                const auto effective_state = material_proxy.effective_graphics_pass_state(*shader_program);
                if (!shader_program || !shader::is_valid_shader_graphics_pass_state(effective_state))
                {
                    if (inputs.require_complete_meshes)
                    {
                        return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                                  "Preview material candidate is invalid.");
                    }
                    TOY_LOG_ERROR("Forward Base Pass skipped View {} MeshBatch {} because its active Material "
                                  "candidate is invalid.",
                                  view_index, batch_index);
                    continue;
                }

                RHIGraphicsBindings owner_bindings;
                RHIStatus batch_status = resolve_mesh_draw_binding(device, *shader_program, RHIBindingGroup::Global,
                                                                   nullptr, owner_bindings.global);
                if (batch_status)
                {
                    batch_status = resolve_mesh_draw_binding(device, *shader_program, RHIBindingGroup::View,
                                                             view_info.view_binding(), owner_bindings.view);
                }
                if (batch_status)
                {
                    batch_status = resolve_mesh_draw_binding(device, *shader_program, RHIBindingGroup::Pass,
                                                             inputs.lighting_bindings[view_index], owner_bindings.pass);
                }
                if (batch_status)
                {
                    batch_status = resolve_mesh_draw_binding(device, *shader_program, RHIBindingGroup::Material,
                                                             mesh_batch.material_binding(), owner_bindings.material);
                }
                if (batch_status)
                {
                    batch_status = resolve_mesh_draw_binding(device, *shader_program, RHIBindingGroup::Object,
                                                             mesh_batch.object_binding(), owner_bindings.object);
                }
                if (!batch_status)
                {
                    if (inputs.require_complete_meshes)
                    {
                        return batch_status;
                    }
                    TOY_LOG_ERROR("Forward Base Pass skipped View {} MeshBatch {} because owner-provided bindings "
                                  "are incomplete or incompatible: {}",
                                  view_index, batch_index, batch_status.message());
                    continue;
                }

                RHIResult<RHIShaderProgramRef> cached_program = shader_program_cache.find_or_create(shader_program);
                if (!cached_program)
                {
                    if (inputs.require_complete_meshes)
                    {
                        return cached_program.status();
                    }
                    TOY_LOG_ERROR("Forward Base Pass skipped View {} MeshBatch {} because its RHI Shader Program "
                                  "could not be created: {}",
                                  view_index, batch_index, cached_program.status().message());
                    continue;
                }
                const RHIShaderProgram& program = *cached_program.value();

                std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout> vertex_layouts;
                std::vector<RHIGraphicsPipelineDesc::VertexAttribute> vertex_attributes;
                std::vector<RHIVertexBufferBinding> vertex_bindings;
                batch_status = mesh_batch.vertex_factory().build_vertex_input(
                    shader_program->data().vertex_inputs, vertex_layouts, vertex_attributes, vertex_bindings);
                if (!batch_status)
                {
                    if (inputs.require_complete_meshes)
                    {
                        return batch_status;
                    }
                    TOY_LOG_ERROR("Forward Base Pass skipped View {} MeshBatch {} because its vertex input is "
                                  "incompatible: {}",
                                  view_index, batch_index, batch_status.message());
                    continue;
                }

                RHIGraphicsPipelineDesc pipeline_desc;
                pipeline_desc.vertex_shader = program.vertex_shader;
                pipeline_desc.pixel_shader = program.pixel_shader;
                pipeline_desc.binding_layout = program.binding_layout;
                pipeline_desc.vertex_buffers = std::move(vertex_layouts);
                pipeline_desc.vertex_attributes = std::move(vertex_attributes);
                pipeline_desc.debug_name =
                    shader_program->data().shader_name + "/" + shader_program->data().pass_name + " ForwardBasePass";
                batch_status = apply_attachment_compatibility(pass_desc, pipeline_desc);
                if (!batch_status)
                {
                    if (inputs.require_complete_meshes)
                    {
                        return batch_status;
                    }
                    TOY_LOG_ERROR("Forward Base Pass skipped View {} MeshBatch {} because attachment compatibility "
                                  "is invalid: {}",
                                  view_index, batch_index, batch_status.message());
                    continue;
                }
                RHIResult<RHIGraphicsPipelineDesc> shader_pipeline =
                    build_shader_graphics_pipeline_desc(pipeline_desc, effective_state);
                if (!shader_pipeline)
                {
                    if (inputs.require_complete_meshes)
                    {
                        return shader_pipeline.status();
                    }
                    TOY_LOG_ERROR("Forward Base Pass skipped View {} MeshBatch {} because its Shader graphics state "
                                  "is invalid: {}",
                                  view_index, batch_index, shader_pipeline.status().message());
                    continue;
                }

                RHIResult<RHIGraphicsPipelineRef> pipeline =
                    device.create_graphics_pipeline(std::move(shader_pipeline).value());
                if (!pipeline)
                {
                    if (inputs.require_complete_meshes)
                    {
                        return pipeline.status();
                    }
                    TOY_LOG_ERROR("Forward Base Pass skipped View {} MeshBatch {} because its pipeline could not be "
                                  "created: {}",
                                  view_index, batch_index, pipeline.status().message());
                    continue;
                }

                MeshDrawCommand command;
                command.pipeline = std::move(pipeline).value();
                command.vertex_buffers = std::move(vertex_bindings);
                command.index_buffer = mesh_batch.index_buffer_binding();
                command.bindings = std::move(owner_bindings);
                command.draw_args.index_count = mesh_batch.index_count();
                command.draw_args.first_index = mesh_batch.first_index();
                draw_list.commands.push_back(std::move(command));
            }
            draw_lists.push_back(std::move(draw_list));
        }

        status = context.begin_render_pass(pass_desc);
        if (!status)
        {
            return status;
        }
        for (const MeshPassDrawList& draw_list : draw_lists)
        {
            for (const MeshDrawCommand& command : draw_list.commands)
            {
                status = context.set_graphics_pipeline(command.pipeline);
                if (status)
                {
                    status = context.set_viewport(draw_list.viewport);
                }
                if (status)
                {
                    status = context.set_scissor(draw_list.scissor);
                }
                if (status)
                {
                    status = context.set_blend_constants(vec4(1.0F, 1.0F, 1.0F, 1.0F));
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

        const RHIStatus end_status = context.end_render_pass();
        return status ? end_status : status;
    }
} // namespace toy3d
