#include "renderscene/pass/hit_proxy_pass.h"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/geometry/vertex_factory.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/global_shader_type_registry.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/shader/shader_parameters.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/view/view_info.h"
#include "shader_parameters/toy3d_editor_hitproxy.generated.h"

namespace toy3d
{
    const GlobalShaderType& hit_proxy_global_shader_type()
    {
        static const HitProxyPassParameters parameters;
        const ShaderParametersMetadata& metadata = shader_parameters_metadata(parameters);
        static const GlobalShaderType type(
            "HitProxyGlobalShader", "Toy3d/Editor/HitProxy", "HitProxy", shader::default_shader_permutation_key,
            GlobalShaderType::ProgramKind::Graphics, RHIShaderStageFlags::Vertex | RHIShaderStageFlags::Pixel, metadata,
            {GlobalShaderBindingRequirement(metadata.constant_buffer.binding_id, RHIBindingGroup::Pass,
                                            RHIResourceBindingType::UniformBuffer, 1, RHIShaderStageFlags::Pixel)});
        return type;
    }

    namespace
    {
        const GlobalShaderTypeRegistration hit_proxy_registration(hit_proxy_global_shader_type());
    } // namespace

    RHIStatus render_hit_proxy_pass(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                    const GlobalShaderMap& global_shader_map, RHIGraphicsCommandContext& context,
                                    const std::vector<ViewInfo>& views, const RHITextureViewRef& id_view,
                                    const RHITextureViewRef& depth_view, HitProxyTable& table)
    {
        table.clear();
        if (!id_view || !depth_view || !id_view->texture() || !depth_view->texture() ||
            id_view->desc().format != PixelFormat::R32UInt || depth_view->desc().format != PixelFormat::D32Float)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "HitProxyPass requires R32UInt and D32Float targets.");
        }
        const ShaderMapProgramResult found = global_shader_map.find(hit_proxy_global_shader_type());
        if (!found.succeeded())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "HitProxy Shader lookup failed: " + found.error);
        }
        const ShaderMapProgramRef& shader_program = found.program;
        RHIResult<RHIShaderProgramRef> cached = shader_program_cache.find_or_create(shader_program);
        if (!cached)
        {
            return cached.status();
        }

        RHIRenderPassDesc pass_desc;
        RHIColorAttachmentDesc color;
        color.view = id_view;
        color.load = RHILoadOperation::Clear;
        color.store = RHIStoreOperation::Store;
        color.clear_value = RHIClearValue::color_value(vec4(0.0f));
        pass_desc.color_attachments.push_back(std::move(color));
        pass_desc.has_depth_stencil_attachment = true;
        pass_desc.depth_stencil_attachment.view = depth_view;
        pass_desc.depth_stencil_attachment.depth_load = RHILoadOperation::Clear;
        pass_desc.depth_stencil_attachment.depth_store = RHIStoreOperation::Discard;
        pass_desc.depth_stencil_attachment.stencil_load = RHILoadOperation::Discard;
        pass_desc.depth_stencil_attachment.stencil_store = RHIStoreOperation::Discard;
        pass_desc.depth_stencil_attachment.clear_value = RHIClearValue::DepthZero;
        pass_desc.debug_name = "HitProxyPass";

        struct HitDraw
        {
            RHIGraphicsPipelineRef pipeline;
            std::vector<RHIVertexBufferBinding> vertices;
            RHIIndexBufferBinding indices;
            RHIGraphicsBindings bindings;
            RHIDrawIndexedArgs args;
            RHIViewport viewport;
            IntRect scissor;
        };
        std::vector<HitDraw> draws;
        for (const ViewInfo& view : views)
        {
            const IntRect& rect = view.scene_view().view_rect();
            for (const MeshBatch& batch : view.mesh_batches())
            {
                const std::uint32_t actor_id = batch.scene_proxy().actor_id();
                const std::uint32_t component_id = batch.scene_proxy().component_id();
                if (actor_id == 0u || component_id == 0u)
                {
                    continue;
                }

                // Reuse the same pixel ID when the same section appears in
                // multiple Views of this one submission.
                HitProxyId hit_id;
                for (std::size_t index = 0; index < table.size(); ++index)
                {
                    const HitProxyTarget& candidate = table[index];
                    if (candidate.actor_id == actor_id && candidate.component_id == component_id &&
                        candidate.mesh_section_index == batch.section_index())
                    {
                        hit_id.value = static_cast<std::uint32_t>(index + 1u);
                        break;
                    }
                }
                if (hit_id.value == 0u)
                {
                    if (table.size() >= std::numeric_limits<std::uint32_t>::max())
                    {
                        return RHIStatus::failure(RHIErrorCode::InvalidArgument, "HitProxy ID space exhausted.");
                    }
                    table.push_back({HitProxyTargetKind::MeshSection, actor_id, component_id, batch.section_index()});
                    hit_id.value = static_cast<std::uint32_t>(table.size());
                }

                HitDraw draw;
                std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout> layouts;
                std::vector<RHIGraphicsPipelineDesc::VertexAttribute> attributes;
                RHIStatus status = batch.vertex_factory().build_vertex_input(shader_program->data().vertex_inputs,
                                                                             layouts, attributes, draw.vertices);
                if (!status)
                {
                    return status;
                }
                RHIGraphicsPipelineDesc desc;
                desc.vertex_shader = cached.value()->vertex_shader;
                desc.pixel_shader = cached.value()->pixel_shader;
                desc.binding_layout = cached.value()->binding_layout;
                desc.vertex_buffers = std::move(layouts);
                desc.vertex_attributes = std::move(attributes);
                desc.color_attachment_count = 1u;
                desc.color_formats[0] = PixelFormat::R32UInt;
                desc.depth_stencil_format = PixelFormat::D32Float;
                desc.sample_count = 1u;
                desc.debug_name = "HitProxyPipeline";
                RHIResult<RHIGraphicsPipelineDesc> configured =
                    build_shader_graphics_pipeline_desc(desc, shader_program->data().graphics_pass_state);
                if (!configured)
                {
                    return configured.status();
                }
                RHIResult<RHIGraphicsPipelineRef> pipeline =
                    device.create_graphics_pipeline(std::move(configured).value());
                if (!pipeline)
                {
                    return pipeline.status();
                }
                draw.pipeline = std::move(pipeline).value();
                HitProxyPassParameters parameters;
                // Parameters v1 exposes Float2; two exact 16-bit lanes retain all
                // 32 HitProxy ID bits without changing the shared Shader language.
                parameters.hit_proxy_id_parts =
                    Vector2(static_cast<float>(hit_id.value & 0xffffu), static_cast<float>(hit_id.value >> 16u));
                RHIResult<RHIBindingSetRef> pass_binding = create_transient_shader_binding(device, context, parameters);
                if (!pass_binding)
                {
                    return pass_binding.status();
                }
                draw.bindings.view = view.view_binding();
                draw.bindings.pass = std::move(pass_binding).value();
                draw.bindings.object = batch.object_binding();
                draw.indices = batch.index_buffer_binding();
                draw.args.index_count = batch.index_count();
                draw.args.first_index = batch.first_index();
                draw.viewport.x = static_cast<float>(rect.x);
                draw.viewport.y = static_cast<float>(rect.y);
                draw.viewport.width = static_cast<float>(rect.width);
                draw.viewport.height = static_cast<float>(rect.height);
                draw.scissor = rect;
                draws.push_back(std::move(draw));
            }
        }

        RHIStatus status = context.begin_render_pass(pass_desc);
        if (!status)
        {
            return status;
        }
        for (const HitDraw& draw : draws)
        {
            status = context.set_graphics_pipeline(draw.pipeline);
            if (status)
            {
                status = context.set_viewport(draw.viewport);
            }
            if (status)
            {
                status = context.set_scissor(draw.scissor);
            }
            if (status)
            {
                status = context.set_blend_constants(vec4(1.0f));
            }
            if (status)
            {
                status = context.set_stencil_reference(0u);
            }
            if (status)
            {
                status = context.set_vertex_buffers(draw.vertices);
            }
            if (status)
            {
                status = context.set_index_buffer(draw.indices);
            }
            if (status)
            {
                status = context.bind_graphics_bindings(draw.bindings);
            }
            if (status)
            {
                status = context.draw_indexed(draw.args);
            }
            if (!status)
            {
                break;
            }
        }
        const RHIStatus ended = context.end_render_pass();
        return status ? ended : status;
    }
} // namespace toy3d
