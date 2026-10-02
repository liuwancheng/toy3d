#include "renderscene/mesh_batch.h"

#include <utility>

#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/geometry/vertex_factory.h"
#include "rendercore/material/material_render_proxy.h"

namespace toy3d
{
    MeshBatch::MeshBatch(const PrimitiveSceneProxy& scene_proxy, const VertexFactory& vertex_factory,
                         RHIIndexBufferBinding index_buffer, MaterialRenderProxy& material_render_proxy,
                         std::uint32_t first_index, std::uint32_t index_count, std::uint32_t section_index,
                         RHIBufferViewRef bone_matrices, std::uint32_t num_bone_influences)
        : scene_proxy_(&scene_proxy), vertex_factory_(&vertex_factory), index_buffer_(std::move(index_buffer)),
          material_render_proxy_(&material_render_proxy),
          object_shader_parameters_(scene_proxy.object_shader_parameters()),
          object_data_generation_(scene_proxy.object_data_generation()), first_index_(first_index),
          bone_matrices_(std::move(bone_matrices)), index_count_(index_count), section_index_(section_index)
    {
        object_shader_parameters_.toy_num_bone_influences = num_bone_influences;
    }

    ShaderMapProgramResult MeshBatch::resolve_program(const ShaderMapProgramRef& local) const
    {
        if (!local)
        {
            return {nullptr, "Mesh shader program is missing."};
        }
        if (vertex_factory_->type() == shader::MeshVertexFactoryType::Local)
        {
            return {local, {}};
        }
        if (!local->gpu_skin_program())
        {
            return {nullptr, "Material/pass shader does not support GPUSkin. Adapt ToyMeshVertex and recompile."};
        }
        return {local->gpu_skin_program(), {}};
    }

    ShaderMapProgramResult MeshBatch::material_program() const
    {
        return resolve_program(material_render_proxy_->shader_program());
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
