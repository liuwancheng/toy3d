#include "renderscene/view/forward_scene_renderer.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "logging/logger.h"
#include "math/matrix_construction.h"
#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/geometry/static_mesh_render_data.h"
#include "renderscene/primitive_scene_info.h"
#include "renderscene/render_scene.h"

namespace toy3d
{
    ForwardSceneRenderer::ForwardSceneRenderer(SceneViewFamily view_family)
        : SceneRenderer(std::move(view_family))
    {}

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

                const StaticMeshRenderData* const render_data =
                    static_mesh_proxy->render_data();
                if (render_data == nullptr)
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer skipped a visible StaticMesh with no StaticMeshRenderData.");
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

    void ForwardSceneRenderer::render(RenderScene& render_scene) noexcept
    {
        if (!init_views())
        {
            return;
        }
        compute_view_visibility(render_scene);
        collect_mesh_batches();
    }
}
