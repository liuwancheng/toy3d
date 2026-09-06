#include "renderscene/view/forward_scene_renderer.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "logging/logger.h"
#include "math/matrix_construction.h"
#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/primitive_uniform_shader_parameters.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/shader/view_uniform_shader_parameters.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/geometry/static_mesh_render_data.h"
#include "renderscene/primitive_scene_info.h"
#include "renderscene/render_scene.h"
#include "renderscene/scene_render_targets.h"

namespace toy3d
{
    namespace
    {
        bool program_declares_group(
            const ShaderMapProgram& program,
            RHIBindingGroup group)
        {
            for (const ShaderMapBinding& binding : program.data().bindings)
            {
                if (binding.group == group)
                {
                    return true;
                }
            }
            return false;
        }

        RHIStatus apply_attachment_compatibility(
            const RHIRenderPassDesc& pass_desc,
            RHIGraphicsPipelineDesc& pipeline_desc)
        {
            if (pass_desc.color_attachments.empty() ||
                !pass_desc.has_depth_stencil_attachment)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Forward Base Pass requires color and depth attachments.");
            }
            if (pass_desc.color_attachments.size() >
                pipeline_desc.color_formats.size())
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Forward Base Pass has too many color attachments.");
            }

            pipeline_desc.color_attachment_count =
                static_cast<std::uint32_t>(
                    pass_desc.color_attachments.size());
            for (std::size_t index = 0;
                 index < pass_desc.color_attachments.size();
                 ++index)
            {
                const RHITextureViewRef& view =
                    pass_desc.color_attachments[index].view;
                if (!view || !view->texture())
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Forward Base Pass color attachment is unavailable.");
                }
                pipeline_desc.color_formats[index] = view->desc().format;
                if (index == 0)
                {
                    pipeline_desc.sample_count =
                        view->texture()->desc().sample_count;
                }
            }

            const RHITextureViewRef& depth_view =
                pass_desc.depth_stencil_attachment.view;
            if (!depth_view || !depth_view->texture())
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Forward Base Pass depth attachment is unavailable.");
            }
            pipeline_desc.depth_stencil_format = depth_view->desc().format;
            return RHIStatus::success();
        }
    }

    struct ForwardSceneRenderer::PreparedBasePass
    {
        struct Draw
        {
            RHIViewport viewport;
            RHIRect scissor;
            RHIGraphicsPipelineRef pipeline;
            std::vector<RHIVertexBufferBinding> vertex_bindings;
            RHIIndexBufferBinding index_binding;
            RHIGraphicsBindings bindings;
            RHIDrawIndexedArgs draw_args;
        };

        RHIRenderPassDesc pass_desc;
        std::vector<Draw> draws;
    };

    ForwardSceneRenderer::ForwardSceneRenderer(SceneViewFamily view_family)
        : SceneRenderer(std::move(view_family))
    {}

    RHIStatus ForwardSceneRenderer::render_scene_passes(
        RenderScene& render_scene,
        RHIDevice& device,
        RHIShaderProgramCache& shader_program_cache,
        RHIGraphicsCommandContext& context,
        SceneRenderTargets& scene_render_targets)
    {
        const bool scene_targets_complete =
            scene_render_targets.scene_color_texture() &&
            scene_render_targets.scene_color_view() &&
            scene_render_targets.scene_color_shader_resource_view() &&
            scene_render_targets.scene_depth_texture() &&
            scene_render_targets.scene_depth_view() &&
            scene_render_targets.scene_depth_shader_resource_view();
        const bool scene_targets_owned = scene_targets_complete &&
            scene_render_targets.scene_color_texture()->is_owned_by(device) &&
            scene_render_targets.scene_color_view()->is_owned_by(device) &&
            scene_render_targets.scene_color_shader_resource_view()
                ->is_owned_by(device) &&
            scene_render_targets.scene_depth_texture()->is_owned_by(device) &&
            scene_render_targets.scene_depth_view()->is_owned_by(device) &&
            scene_render_targets.scene_depth_shader_resource_view()
                ->is_owned_by(device);
        if (!scene_targets_owned)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward scene passes require complete SceneRenderTargets owned by the injected device.");
        }
        if (!init_views())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward scene passes rejected their SceneViewFamily inputs.");
        }

        compute_view_visibility(render_scene);
        collect_mesh_batches();

        std::vector<RHIResourceTransition> scene_attachment_transitions;
        if (scene_render_targets.scene_color_access() != RHIAccess::RenderTarget)
        {
            RHIResourceTransition scene_color_to_render_target;
            scene_color_to_render_target.resource =
                scene_render_targets.scene_color_texture();
            scene_color_to_render_target.subresources =
                scene_render_targets.scene_color_view()->desc().subresources;
            scene_color_to_render_target.before =
                scene_render_targets.scene_color_access();
            scene_color_to_render_target.after = RHIAccess::RenderTarget;
            scene_attachment_transitions.push_back(
                std::move(scene_color_to_render_target));
        }
        if (scene_render_targets.scene_depth_access() !=
            RHIAccess::DepthStencilWrite)
        {
            RHIResourceTransition scene_depth_to_write;
            scene_depth_to_write.resource =
                scene_render_targets.scene_depth_texture();
            scene_depth_to_write.subresources =
                scene_render_targets.scene_depth_view()->desc().subresources;
            scene_depth_to_write.before =
                scene_render_targets.scene_depth_access();
            scene_depth_to_write.after = RHIAccess::DepthStencilWrite;
            scene_attachment_transitions.push_back(
                std::move(scene_depth_to_write));
        }
        if (!scene_attachment_transitions.empty())
        {
            const RHIStatus transition_status =
                context.transition_resources(scene_attachment_transitions);
            if (!transition_status)
            {
                return transition_status;
            }
        }

        RHIRenderPassDesc pass_desc;
        RHIColorAttachmentDesc color_attachment;
        color_attachment.view = scene_render_targets.scene_color_view();
        color_attachment.load = RHILoadOperation::Clear;
        color_attachment.store = RHIStoreOperation::Store;
        color_attachment.clear_value =
            RHIClearValue::color_value(vec4(0.0F, 0.0F, 0.0F, 1.0F));
        pass_desc.color_attachments.push_back(std::move(color_attachment));
        pass_desc.has_depth_stencil_attachment = true;
        pass_desc.depth_stencil_attachment.view =
            scene_render_targets.scene_depth_view();
        pass_desc.depth_stencil_attachment.depth_load =
            RHILoadOperation::Clear;
        pass_desc.depth_stencil_attachment.depth_store =
            RHIStoreOperation::Store;
        pass_desc.depth_stencil_attachment.stencil_load =
            RHILoadOperation::Discard;
        pass_desc.depth_stencil_attachment.stencil_store =
            RHIStoreOperation::Discard;
        pass_desc.depth_stencil_attachment.clear_value =
            RHIClearValue::DepthZero;
        pass_desc.debug_name = "ForwardBasePass";
        PreparedBasePass prepared_base_pass;
        RHIStatus status = prepare_base_pass(
            device, shader_program_cache, context, pass_desc,
            prepared_base_pass);
        if (!status)
        {
            return status;
        }
        return execute_base_pass(context, prepared_base_pass);
    }

    bool ForwardSceneRenderer::init_views()
    {
        view_infos().clear();
        const UIntVector2 family_output_size = view_family().output_size();
        if (family_output_size.x == 0 || family_output_size.y == 0)
        {
            TOY_LOG_ERROR(
                "ForwardSceneRenderer init_views requires a non-empty family output.");
            return false;
        }

        std::vector<ViewInfo> initialized_views;
        initialized_views.reserve(view_family().views().size());
        for (std::size_t view_index = 0;
             view_index < view_family().views().size();
             ++view_index)
        {
            const SceneView& scene_view = view_family().views()[view_index];
            const UIntVector2 output_size = scene_view.output_size();
            const UIntVector2 rect_minimum = scene_view.view_rect_minimum();
            const UIntVector2 rect_size = scene_view.view_rect_size();
            if (output_size.x == 0 || output_size.y == 0 ||
                output_size != family_output_size)
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected View {} with invalid or inconsistent output size.",
                    view_index);
                return false;
            }
            if (rect_size.x == 0 || rect_size.y == 0 ||
                rect_minimum.x >= output_size.x ||
                rect_minimum.y >= output_size.y ||
                rect_size.x > output_size.x - rect_minimum.x ||
                rect_size.y > output_size.y - rect_minimum.y)
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected View {} with an empty or out-of-bounds view rect.",
                    view_index);
                return false;
            }
            if (!is_finite(scene_view.camera_position()) ||
                !is_finite(scene_view.camera_orientation()) ||
                !is_finite(scene_view.camera_direction()) ||
                !is_finite(scene_view.near_clip()) ||
                scene_view.near_clip() <= 0.0f)
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected View {} with non-finite camera input or a non-positive near plane.",
                    view_index);
                return false;
            }

            Matrix4 view_matrix;
            if (!try_make_view_matrix(
                    scene_view.camera_position(),
                    scene_view.camera_orientation(),
                    view_matrix))
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views failed to construct View {} world-to-view matrix.",
                    view_index);
                return false;
            }

            const float aspect =
                static_cast<float>(rect_size.x) /
                static_cast<float>(rect_size.y);
            Matrix4 projection_matrix;
            switch (scene_view.projection_mode())
            {
            case CameraProjectionMode::Perspective:
            {
                PerspectiveProjectionDesc projection_desc;
                projection_desc.vertical_fov = scene_view.vertical_fov();
                projection_desc.aspect = aspect;
                projection_desc.near_clip = scene_view.near_clip();
                projection_desc.far_clip = scene_view.far_clip();
                if (!try_make_perspective_projection(
                        projection_desc, projection_matrix))
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer init_views rejected View {} finite perspective inputs.",
                        view_index);
                    return false;
                }
                break;
            }
            case CameraProjectionMode::PerspectiveInfiniteFar:
            {
                InfinitePerspectiveProjectionDesc projection_desc;
                projection_desc.vertical_fov = scene_view.vertical_fov();
                projection_desc.aspect = aspect;
                projection_desc.near_clip = scene_view.near_clip();
                if (!try_make_infinite_perspective_projection(
                        projection_desc, projection_matrix))
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer init_views rejected View {} infinite-far perspective inputs.",
                        view_index);
                    return false;
                }
                break;
            }
            case CameraProjectionMode::Orthographic:
            case CameraProjectionMode::Custom:
            default:
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected unsupported projection mode for View {}.",
                    view_index);
                return false;
            }

            const Matrix4 view_projection_matrix =
                projection_matrix * view_matrix;
            Matrix4 inverse_view_matrix;
            Matrix4 inverse_projection_matrix;
            Matrix4 inverse_view_projection_matrix;
            if (!is_finite(view_matrix) ||
                !is_finite(projection_matrix) ||
                !is_finite(view_projection_matrix) ||
                !try_inverse(view_matrix, inverse_view_matrix) ||
                !try_inverse(projection_matrix, inverse_projection_matrix) ||
                !try_inverse(
                    view_projection_matrix,
                    inverse_view_projection_matrix))
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected non-finite or non-invertible derived matrices for View {}.",
                    view_index);
                return false;
            }

            ConvexVolume view_frustum;
            if (!try_make_reversed_z_frustum(
                    view_projection_matrix,
                    scene_view.infinite_far(),
                    view_frustum))
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected degenerate frustum planes for View {}.",
                    view_index);
                return false;
            }

            ViewInfo initialized_view(
                scene_view,
                std::move(view_matrix),
                std::move(projection_matrix),
                view_projection_matrix,
                std::move(inverse_view_matrix),
                std::move(inverse_projection_matrix),
                std::move(inverse_view_projection_matrix),
                std::move(view_frustum));
            initialized_views.push_back(std::move(initialized_view));
        }

        // A fresh vector makes each View's current-frame visibility start empty;
        // publication happens only after every View has initialized successfully.
        view_infos() = std::move(initialized_views);
        return true;
    }

    void ForwardSceneRenderer::compute_view_visibility(
        const RenderScene& render_scene)
    {
        for (ViewInfo& view_info : view_infos())
        {
            std::vector<PrimitiveSceneInfo*>& visible_primitives =
                view_info.visible_primitives_;
            visible_primitives.clear();
            visible_primitives.reserve(
                render_scene.primitive_scene_infos().size());

            for (const std::unique_ptr<PrimitiveSceneInfo>& primitive_info :
                 render_scene.primitive_scene_infos())
            {
                if (!primitive_info)
                {
                    continue;
                }

                PrimitiveSceneProxy* const proxy = primitive_info->proxy();
                if (proxy == nullptr || !proxy->visible())
                {
                    continue;
                }

                const AxisAlignedBounds& bounds = proxy->world_bounds();
                const Vector3 minimum(
                    bounds.minimum.x,
                    bounds.minimum.y,
                    bounds.minimum.z);
                const Vector3 maximum(
                    bounds.maximum.x,
                    bounds.maximum.y,
                    bounds.maximum.z);
                if (!view_info.view_frustum().intersects_axis_aligned_bounds(
                        minimum, maximum))
                {
                    continue;
                }

                visible_primitives.push_back(primitive_info.get());
            }
        }
    }

    void ForwardSceneRenderer::collect_mesh_batches()
    {
        for (ViewInfo& view_info : view_infos())
        {
            std::vector<MeshBatch>& mesh_batches = view_info.mesh_batches_;
            mesh_batches.clear();
            mesh_batches.reserve(view_info.visible_primitives_.size());

            for (PrimitiveSceneInfo* const primitive_info :
                 view_info.visible_primitives_)
            {
                if (primitive_info == nullptr)
                {
                    continue;
                }

                const auto* const static_mesh_proxy =
                    dynamic_cast<const StaticMeshSceneProxy*>(
                        primitive_info->proxy());
                if (static_mesh_proxy == nullptr)
                {
                    continue;
                }

                StaticMeshRenderData* const render_data =
                    static_mesh_proxy->render_data();
                if (render_data == nullptr)
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer skipped a visible StaticMesh with no StaticMeshRenderData.");
                    continue;
                }
                const RHIStatus prepared_render_data =
                    render_data->prepare_current_recording();
                if (!prepared_render_data)
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer skipped a visible StaticMesh whose render data is not ready in the current recording: {}",
                        prepared_render_data.message());
                    continue;
                }
                if (!render_data->is_drawable())
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer skipped a visible StaticMesh whose complete render-data gate is not drawable.");
                    continue;
                }

                const LocalVertexFactory* const vertex_factory =
                    render_data->vertex_factory();
                if (vertex_factory == nullptr)
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer skipped a visible StaticMesh with no LocalVertexFactory.");
                    continue;
                }

                const std::vector<MaterialRenderProxy*>& material_proxies =
                    static_mesh_proxy->material_render_proxies();
                const std::vector<StaticMeshSection>& sections =
                    render_data->sections();
                for (std::size_t section_index = 0;
                     section_index < sections.size();
                     ++section_index)
                {
                    const StaticMeshSection& section = sections[section_index];
                    const std::size_t first_index = section.first_index;
                    const std::size_t index_count = section.index_count;
                    if (index_count == 0u || index_count % 3u != 0u ||
                        first_index > render_data->index_count() ||
                        index_count > render_data->index_count() - first_index)
                    {
                        TOY_LOG_ERROR(
                            "ForwardSceneRenderer skipped StaticMesh section {} with an invalid index range.",
                            section_index);
                        continue;
                    }
                    if (section.material_slot >= material_proxies.size())
                    {
                        TOY_LOG_ERROR(
                            "ForwardSceneRenderer skipped StaticMesh section {} whose Material slot is out of range.",
                            section_index);
                        continue;
                    }

                    MaterialRenderProxy* const material_proxy =
                        material_proxies[section.material_slot];
                    if (material_proxy == nullptr ||
                        !material_proxy->shader_program())
                    {
                        TOY_LOG_ERROR(
                            "ForwardSceneRenderer skipped StaticMesh section {} with no usable Material binding.",
                            section_index);
                        continue;
                    }

                    std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>
                        vertex_layouts;
                    std::vector<RHIGraphicsPipelineDesc::VertexAttribute>
                        vertex_attributes;
                    std::vector<RHIVertexBufferBinding> vertex_bindings;
                    const RHIStatus vertex_status =
                        vertex_factory->build_vertex_input(
                            material_proxy->shader_program()->data().vertex_inputs,
                            vertex_layouts,
                            vertex_attributes,
                            vertex_bindings);
                    if (!vertex_status)
                    {
                        TOY_LOG_ERROR(
                            "ForwardSceneRenderer skipped StaticMesh section {} because LocalVertexFactory is incompatible with ShaderVertexInput: {}",
                            section_index,
                            vertex_status.message());
                        continue;
                    }

                    mesh_batches.emplace_back(
                        *static_mesh_proxy,
                        *render_data,
                        *vertex_factory,
                        *material_proxy,
                        section.first_index,
                        section.index_count);
                }
            }
        }
    }

    RHIStatus ForwardSceneRenderer::prepare_base_pass(
        RHIDevice& device,
        RHIShaderProgramCache& shader_program_cache,
        RHIGraphicsCommandContext& context,
        const RHIRenderPassDesc& pass_desc,
        PreparedBasePass& prepared_pass)
    {
        if (pass_desc.color_attachments.empty() ||
            !pass_desc.has_depth_stencil_attachment)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward Base Pass requires color and depth attachments.");
        }
        if (pass_desc.color_attachments.size() > RHI_MAX_COLOR_ATTACHMENTS)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward Base Pass has too many color attachments.");
        }
        const RHIStatus pass_validation =
            validate_render_pass_desc(pass_desc);
        if (!pass_validation)
        {
            return pass_validation;
        }

        PreparedBasePass result;
        result.pass_desc = pass_desc;
        for (std::size_t view_index = 0;
             view_index < view_infos().size();
             ++view_index)
        {
            const ViewInfo& view_info = view_infos()[view_index];
            const SceneView& scene_view = view_info.scene_view();
            const UIntVector2 rect_minimum =
                scene_view.view_rect_minimum();
            const UIntVector2 rect_size = scene_view.view_rect_size();
            if (rect_minimum.x > static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()) ||
                rect_minimum.y > static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()))
            {
                TOY_LOG_ERROR(
                    "Forward Base Pass skipped View {} because its scissor origin exceeds the RHI signed range.",
                    view_index);
                continue;
            }

            RHIViewport viewport;
            viewport.x = static_cast<float>(rect_minimum.x);
            viewport.y = static_cast<float>(rect_minimum.y);
            viewport.width = static_cast<float>(rect_size.x);
            viewport.height = static_cast<float>(rect_size.y);
            RHIRect scissor;
            scissor.x = static_cast<std::int32_t>(rect_minimum.x);
            scissor.y = static_cast<std::int32_t>(rect_minimum.y);
            scissor.width = rect_size.x;
            scissor.height = rect_size.y;

            for (std::size_t batch_index = 0;
                 batch_index < view_info.mesh_batches().size();
                 ++batch_index)
            {
                const MeshBatch& mesh_batch =
                    view_info.mesh_batches()[batch_index];
                MaterialRenderProxy& material_proxy =
                    mesh_batch.material_render_proxy();
                const ShaderMapProgramRef& shader_program =
                    material_proxy.shader_program();
                const shader::ShaderGraphicsPassState* effective_state =
                    material_proxy.effective_graphics_pass_state();
                if (!shader_program || effective_state == nullptr ||
                    !shader::is_valid_shader_graphics_pass_state(
                        *effective_state))
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its active Material candidate is invalid.",
                        view_index, batch_index);
                    continue;
                }
                if (program_declares_group(
                        *shader_program, RHIBindingGroup::Global) ||
                    program_declares_group(
                        *shader_program, RHIBindingGroup::Pass))
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its Program declares Global or Pass bindings without a canonical source.",
                        view_index, batch_index);
                    continue;
                }

                RHIResult<RHIShaderProgramRef> cached_program =
                    shader_program_cache.find_or_create(shader_program);
                if (!cached_program)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its RHI Shader Program could not be created: {}",
                        view_index, batch_index,
                        cached_program.status().message());
                    continue;
                }
                const RHIShaderProgram& program = *cached_program.value();

                std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>
                    vertex_layouts;
                std::vector<RHIGraphicsPipelineDesc::VertexAttribute>
                    vertex_attributes;
                std::vector<RHIVertexBufferBinding> vertex_bindings;
                RHIStatus batch_status =
                    mesh_batch.vertex_factory().build_vertex_input(
                        shader_program->data().vertex_inputs,
                        vertex_layouts,
                        vertex_attributes,
                        vertex_bindings);
                if (!batch_status)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its vertex input is incompatible: {}",
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
                    shader_program->data().shader_name + "/" +
                    shader_program->data().pass_name + " ForwardBasePass";
                batch_status = apply_attachment_compatibility(
                    pass_desc, pipeline_desc);
                if (!batch_status)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because attachment compatibility is invalid: {}",
                        view_index, batch_index, batch_status.message());
                    continue;
                }
                RHIResult<RHIGraphicsPipelineDesc> shader_pipeline =
                    build_shader_graphics_pipeline_desc(
                        pipeline_desc, *effective_state);
                if (!shader_pipeline)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its Shader graphics state is invalid: {}",
                        view_index, batch_index,
                        shader_pipeline.status().message());
                    continue;
                }
                pipeline_desc = std::move(shader_pipeline).value();

                RHIResult<RHIGraphicsPipelineRef> pipeline =
                    device.create_graphics_pipeline(pipeline_desc);
                if (!pipeline)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its pipeline could not be created: {}",
                        view_index, batch_index,
                        pipeline.status().message());
                    continue;
                }

                RHIResult<RHIBindingSetRef> view_binding =
                    materialize_view_uniform_shader_parameters(
                        device, context, program.binding_layout, *shader_program,
                        view_info.view_uniform_shader_parameters());
                if (!view_binding)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because View bindings could not be materialized: {}",
                        view_index, batch_index,
                        view_binding.status().message());
                    continue;
                }
                RHIBindingSetRef material_binding;
                if (program_declares_group(
                        *shader_program, RHIBindingGroup::Material))
                {
                    RHIResult<RHIBindingSetRef> materialized_material =
                        material_proxy.materialize(
                            device, context, program.binding_layout);
                    if (!materialized_material)
                    {
                        TOY_LOG_ERROR(
                            "Forward Base Pass skipped View {} MeshBatch {} because Material bindings could not be materialized: {}",
                            view_index, batch_index,
                            materialized_material.status().message());
                        continue;
                    }
                    material_binding =
                        std::move(materialized_material).value();
                }
                RHIResult<RHIBindingSetRef> object_binding =
                    materialize_primitive_uniform_shader_parameters(
                        device, context, program.binding_layout, *shader_program,
                        mesh_batch.scene_proxy()
                            .primitive_uniform_shader_parameters());
                if (!object_binding)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because Object bindings could not be materialized: {}",
                        view_index, batch_index,
                        object_binding.status().message());
                    continue;
                }

                RHIGraphicsBindings bindings;
                bindings.view = std::move(view_binding).value();
                bindings.material = std::move(material_binding);
                bindings.object = std::move(object_binding).value();

                RHIDrawIndexedArgs draw_args;
                draw_args.index_count = mesh_batch.index_count();
                draw_args.first_index = mesh_batch.first_index();
                PreparedBasePass::Draw draw;
                draw.viewport = viewport;
                draw.scissor = scissor;
                draw.pipeline = std::move(pipeline).value();
                draw.vertex_bindings = std::move(vertex_bindings);
                draw.index_binding =
                    mesh_batch.render_data().index_buffer_binding();
                draw.bindings = std::move(bindings);
                draw.draw_args = draw_args;
                result.draws.push_back(std::move(draw));
            }
        }

        prepared_pass = std::move(result);
        return RHIStatus::success();
    }

    RHIStatus ForwardSceneRenderer::execute_base_pass(
        RHIGraphicsCommandContext& context,
        const PreparedBasePass& prepared_pass)
    {
        RHIStatus status =
            context.begin_render_pass(prepared_pass.pass_desc);
        if (!status)
        {
            return status;
        }
        for (const PreparedBasePass::Draw& draw : prepared_pass.draws)
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
                status = context.set_blend_constants(
                    vec4(1.0F, 1.0F, 1.0F, 1.0F));
            }
            if (status)
            {
                status = context.set_stencil_reference(0u);
            }
            if (status)
            {
                status = context.set_vertex_buffers(
                    draw.vertex_bindings);
            }
            if (status)
            {
                status = context.set_index_buffer(draw.index_binding);
            }
            if (status)
            {
                status = context.bind_graphics_bindings(draw.bindings);
            }
            if (status)
            {
                status = context.draw_indexed(draw.draw_args);
            }
            if (!status)
            {
                break;
            }
        }

        const RHIStatus end_status = context.end_render_pass();
        return status ? end_status : status;
    }

}
