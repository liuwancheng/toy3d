#include "rendercore/scene/static_mesh_scene_proxy.h"

#include <utility>
#include <limits>

#include "rendercore/geometry/static_mesh_render_data.h"
#include "rendercore/material/material_render_proxy.h"
#include "renderscene/mesh_batch.h"

namespace toy3d
{
    StaticMeshSceneProxy::StaticMeshSceneProxy(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                                               StaticMeshRenderData* render_data,
                                               std::vector<MaterialRenderProxy*> material_render_proxies,
                                               std::uint32_t actor_id, std::uint32_t component_id, bool cast_shadows,
                                               bool receives_shadows)
        : PrimitiveSceneProxy(std::move(world_transform), std::move(world_bounds), visible, actor_id, component_id,
                              cast_shadows, receives_shadows, std::move(material_render_proxies)),
          render_data_(render_data)
    {
    }

    RHIStatus StaticMeshSceneProxy::begin_init_resources(RenderResourceManager& manager)
    {
        return render_data_ ? render_data_->begin_init(manager)
                            : RHIStatus::failure(RHIErrorCode::InvalidArgument, "StaticMesh has no render data.");
    }

    RHIStatus StaticMeshSceneProxy::release_resources(RenderResourceManager& manager, bool release_shared_geometry)
    {
        return render_data_ && release_shared_geometry ? render_data_->release(manager) : RHIStatus::success();
    }

    bool StaticMeshSceneProxy::shares_geometry_resources(const PrimitiveSceneProxy& other) const
    {
        const auto* mesh = dynamic_cast<const StaticMeshSceneProxy*>(&other);
        return render_data_ && mesh && render_data_ == mesh->render_data_;
    }

    bool StaticMeshSceneProxy::resources_drawable() const
    {
        return render_data_ && render_data_->is_drawable();
    }

    std::size_t StaticMeshSceneProxy::mesh_section_count() const
    {
        return render_data_ ? render_data_->sections().size() : 0;
    }

    RHIStatus StaticMeshSceneProxy::collect_mesh_batches(std::vector<MeshBatch>& batches) const
    {
        if (!render_data_)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "StaticMesh has no render data.");
        }
        const auto status = render_data_->prepare_current_recording();
        if (!status)
        {
            return status;
        }
        if (!resources_drawable() || !render_data_->vertex_factory())
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "StaticMesh geometry is not drawable.");
        }
        std::vector<MeshBatch> candidate;
        const auto& sections = render_data_->sections();
        const auto& materials = material_render_proxies();
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const auto& section = sections[i];
            if (i > std::numeric_limits<std::uint32_t>::max() || !section.index_count ||
                section.index_count % 3u != 0 || section.first_index > render_data_->index_count() ||
                section.index_count > render_data_->index_count() - section.first_index ||
                section.material_slot >= materials.size() || !materials[section.material_slot] ||
                !materials[section.material_slot]->shader_map())
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "StaticMesh section or material is invalid.");
            }
            candidate.emplace_back(*this, *render_data_->vertex_factory(), render_data_->index_buffer_binding(),
                                   *materials[section.material_slot], section.first_index, section.index_count,
                                   static_cast<std::uint32_t>(i));
        }
        batches.insert(batches.end(), candidate.begin(), candidate.end());
        return RHIStatus::success();
    }
} // namespace toy3d
