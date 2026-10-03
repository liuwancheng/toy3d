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
                         RHIBufferViewRef bone_matrices, std::uint32_t num_bone_influences,
                         bool has_valid_tangent_frame)
        : scene_proxy_(&scene_proxy), vertex_factory_(&vertex_factory), index_buffer_(std::move(index_buffer)),
          material_render_proxy_(&material_render_proxy),
          object_shader_parameters_(scene_proxy.object_shader_parameters()),
          object_data_generation_(scene_proxy.object_data_generation()), first_index_(first_index),
          bone_matrices_(std::move(bone_matrices)), index_count_(index_count), section_index_(section_index)
    {
        object_shader_parameters_.toy_num_bone_influences = num_bone_influences;
        has_valid_tangent_frame_ = has_valid_tangent_frame;
    }

    ShaderMapProgramResult MeshBatch::find_program(const ShaderMapCollection& shader_map,
                                                   shader::ShaderPassRole role) const
    {
        return shader_map.find(role, vertex_factory_->type());
    }

    ShaderMapProgramResult MeshBatch::material_program(bool shadow_active, bool environment_active) const
    {
        const auto& shader_map = material_render_proxy_->shader_map();
        if (!shader_map)
        {
            return {nullptr, "Material ShaderMap collection is missing."};
        }
        if (shader_map->requires_tangent_frame() && !has_valid_tangent_frame_)
        {
            return {nullptr, "Material configuration requires a valid mesh tangent frame."};
        }
        std::vector<shader::ShaderPermutationSelection> selections;
        const auto& policy = shader_map->index().policy;
        if (shader_map->features().shadows)
        {
            selections.push_back(
                {"SHADOW_MODE", shader::ShaderPermutationValueKind::Enumeration, false,
                 shadow_active && scene_proxy_->receives_shadows() && policy.allow_pcf ? "PCF" : "Off"});
        }
        if (shader_map->features().environment)
        {
            selections.push_back({"ENVIRONMENT_MODE", shader::ShaderPermutationValueKind::Enumeration, false,
                                  environment_active && policy.allow_sky ? "Sky" : "Off"});
        }
        return shader_map->find(shader::ShaderPassRole::Forward, vertex_factory_->type(), {}, selections);
    }

    RHIBindingSetRef MeshBatch::material_binding(const ShaderMapProgram& program) const
    {
        const auto found = std::find_if(material_bindings_.begin(), material_bindings_.end(),
                                        [&program](const auto& binding)
                                        {
                                            return binding.first.get() == &program;
                                        });
        return found == material_bindings_.end() ? nullptr : found->second;
    }

    void MeshBatch::publish_material_binding(ShaderMapProgramRef program, RHIBindingSetRef binding_set)
    {
        const auto found = std::find_if(material_bindings_.begin(), material_bindings_.end(),
                                        [&program](const auto& binding)
                                        {
                                            return binding.first == program;
                                        });
        if (found == material_bindings_.end())
        {
            material_bindings_.emplace_back(std::move(program), std::move(binding_set));
        }
        else
        {
            found->second = std::move(binding_set);
        }
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
        if ((role == shader::ShaderPassRole::ShadowDepth || role == shader::ShaderPassRole::HitProxy) &&
            material_map->programs().front()->data().contract.surface_mode == shader::ShaderSurfaceMode::Opaque)
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
