#include "renderscene/view/scene_visibility.h"

#include <cstddef>
#include <memory>

#include "logging/logger.h"
#include "math/vector3.h"
#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "renderscene/geometry/static_mesh_render_data.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/primitive_scene_info.h"
#include "renderscene/render_scene.h"
#include "renderscene/view/view_info.h"

namespace toy3d
{
    void compute_scene_visibility(const RenderScene& render_scene, std::vector<ViewInfo>& view_infos)
    {
        for (ViewInfo& view_info : view_infos)
        {
            std::vector<PrimitiveSceneInfo*>& visible_primitives = view_info.visible_primitives_;
            std::vector<MeshBatch>& mesh_batches = view_info.mesh_batches_;
            visible_primitives.clear();
            mesh_batches.clear();
            visible_primitives.reserve(render_scene.primitive_scene_infos().size());

            for (const std::unique_ptr<PrimitiveSceneInfo>& primitive_info : render_scene.primitive_scene_infos())
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
                const Vector3 minimum(bounds.minimum.x, bounds.minimum.y, bounds.minimum.z);
                const Vector3 maximum(bounds.maximum.x, bounds.maximum.y, bounds.maximum.z);
                if (!is_finite(minimum) || !is_finite(maximum) || minimum.x > maximum.x ||
                    minimum.y > maximum.y || minimum.z > maximum.z)
                {
                    TOY_LOG_ERROR("Scene visibility skipped a Primitive with invalid world bounds.");
                    continue;
                }
                if (!view_info.view_frustum().intersects_axis_aligned_bounds(minimum, maximum))
                {
                    continue;
                }

                visible_primitives.push_back(primitive_info.get());
            }

            mesh_batches.reserve(visible_primitives.size());
            for (PrimitiveSceneInfo* const primitive_info : visible_primitives)
            {
                if (primitive_info == nullptr)
                {
                    continue;
                }

                const auto* const static_mesh_proxy =
                    dynamic_cast<const StaticMeshSceneProxy*>(primitive_info->proxy());
                if (static_mesh_proxy == nullptr)
                {
                    continue;
                }

                StaticMeshRenderData* const render_data = static_mesh_proxy->render_data();
                if (render_data == nullptr)
                {
                    TOY_LOG_ERROR("Scene visibility skipped a visible StaticMesh with no StaticMeshRenderData.");
                    continue;
                }
                const RHIStatus prepared_render_data = render_data->prepare_current_recording();
                if (!prepared_render_data)
                {
                    TOY_LOG_ERROR(
                        "Scene visibility skipped a visible StaticMesh whose render data is not ready in the "
                        "current recording: {}",
                        prepared_render_data.message());
                    continue;
                }
                if (!render_data->is_drawable())
                {
                    TOY_LOG_ERROR("Scene visibility skipped a visible StaticMesh whose complete render-data gate "
                                  "is not drawable.");
                    continue;
                }

                const LocalVertexFactory* const vertex_factory = render_data->vertex_factory();
                if (vertex_factory == nullptr)
                {
                    TOY_LOG_ERROR("Scene visibility skipped a visible StaticMesh with no LocalVertexFactory.");
                    continue;
                }

                const std::vector<MaterialRenderProxy*>& material_proxies =
                    static_mesh_proxy->material_render_proxies();
                const std::vector<StaticMeshSection>& sections = render_data->sections();
                for (std::size_t section_index = 0; section_index < sections.size(); ++section_index)
                {
                    const StaticMeshSection& section = sections[section_index];
                    const std::size_t first_index = section.first_index;
                    const std::size_t index_count = section.index_count;
                    if (index_count == 0u || index_count % 3u != 0u || first_index > render_data->index_count() ||
                        index_count > render_data->index_count() - first_index)
                    {
                        TOY_LOG_ERROR("Scene visibility skipped StaticMesh section {} with an invalid index range.",
                                      section_index);
                        continue;
                    }
                    if (section.material_slot >= material_proxies.size())
                    {
                        TOY_LOG_ERROR(
                            "Scene visibility skipped StaticMesh section {} whose Material slot is out of range.",
                            section_index);
                        continue;
                    }

                    MaterialRenderProxy* const material_proxy = material_proxies[section.material_slot];
                    if (material_proxy == nullptr || !material_proxy->shader_program())
                    {
                        TOY_LOG_ERROR(
                            "Scene visibility skipped StaticMesh section {} with no usable Material representation.",
                            section_index);
                        continue;
                    }

                    mesh_batches.emplace_back(*static_mesh_proxy, *render_data, *vertex_factory, *material_proxy,
                                              section.first_index, section.index_count);
                }
            }
        }
    }
} // namespace toy3d
