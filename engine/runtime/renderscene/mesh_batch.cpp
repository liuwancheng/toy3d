#include "renderscene/mesh_batch.h"

#include <utility>

#include "rendercore/scene/static_mesh_scene_proxy.h"

namespace toy3d
{
    MeshBatch::MeshBatch(const StaticMeshSceneProxy& scene_proxy, const StaticMeshRenderData& render_data,
                         const LocalVertexFactory& vertex_factory, MaterialRenderProxy& material_render_proxy,
                         std::uint32_t first_index, std::uint32_t index_count)
        : scene_proxy_(&scene_proxy), render_data_(&render_data), vertex_factory_(&vertex_factory),
          material_render_proxy_(&material_render_proxy),
          object_shader_parameters_(scene_proxy.object_shader_parameters()),
          object_data_generation_(scene_proxy.object_data_generation()), first_index_(first_index),
          index_count_(index_count)
    {
    }

    void MeshBatch::publish_material_binding(RHIBindingSetRef binding_set)
    {
        material_binding_ = std::move(binding_set);
    }

    void MeshBatch::publish_object_binding(RHIBindingSetRef binding_set)
    {
        object_binding_ = std::move(binding_set);
    }
} // namespace toy3d
