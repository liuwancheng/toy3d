#include "renderscene/mesh_batch.h"

#include <utility>
#include <algorithm>

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

    ShaderMapProgramResult MeshBatch::find_program(const ShaderMapCollection& shader_map,
                                                   shader::ShaderPassRole role) const
    {
        return shader_map.find(role, vertex_factory_->type());
    }

    ShaderMapProgramResult MeshBatch::material_program() const
    {
        const auto& shader_map = material_render_proxy_->shader_map();
        return shader_map ? find_program(*shader_map, shader::ShaderPassRole::Forward)
                          : ShaderMapProgramResult{nullptr, "Material ShaderMap collection is missing."};
    }

    void MeshBatch::publish_material_binding(RHIBindingSetRef binding_set)
    {
        material_binding_ = std::move(binding_set);
    }

    ShaderMapProgramResult MeshBatch::mesh_pass_program(shader::ShaderPassRole role,
                                                        const ShaderMapCollection& default_shader_map) const
    {
        const auto& material_map = material_render_proxy_->shader_map();
        if (!material_map)
        {
            return {nullptr, "Material ShaderMap collection is missing."};
        }
        const auto& passes = material_map->index().passes;
        const bool declared = std::any_of(passes.begin(), passes.end(),
                                          [role](const shader::ShaderMapIndexPass& pass)
                                          {
                                              return pass.role == role;
                                          });
        // A declared custom role must resolve exactly. Engine defaults apply
        // only when this opaque material does not declare that role.
        if (declared)
        {
            return find_program(*material_map, role);
        }
        const auto& name = material_map->index().shader_name;
        if (name == "Toy3d/Surface/Phong" || name == "Toy3d/Surface/Unlit")
        {
            return find_program(default_shader_map, role);
        }
        return {nullptr, "Custom material " + name + " does not declare the required mesh Pass role."};
    }

    void MeshBatch::publish_object_binding(RHIBindingSetRef binding_set)
    {
        object_binding_ = std::move(binding_set);
    }
} // namespace toy3d
