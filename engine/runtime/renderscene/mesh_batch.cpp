#include "renderscene/mesh_batch.h"

#include <utility>

#include "rendercore/scene/primitive_scene_proxy.h"

namespace toy3d
{
    MeshBatch::MeshBatch(const PrimitiveSceneProxy& scene_proxy, const VertexFactory& vertex_factory,
                         RHIIndexBufferBinding index_buffer, MaterialRenderProxy& material_render_proxy,
                         std::uint32_t first_index, std::uint32_t index_count, std::uint32_t section_index)
        : scene_proxy_(&scene_proxy), vertex_factory_(&vertex_factory), index_buffer_(std::move(index_buffer)),
          material_render_proxy_(&material_render_proxy),
          object_shader_parameters_(scene_proxy.object_shader_parameters()),
          object_data_generation_(scene_proxy.object_data_generation()), first_index_(first_index),
          index_count_(index_count), section_index_(section_index)
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
