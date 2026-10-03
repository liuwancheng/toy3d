#pragma once

#include "drivers/rhi/rhi_command_descriptors.h"
#include "rendercore/shader/shader_map.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace toy3d
{
    class VertexFactory;
    class MaterialRenderProxy;
    class PrimitiveSceneProxy;

    // Frame-local Render-side draw input. ViewInfo owns this value for one Draw;
    // Proxy, RenderData, VertexFactory, and Material remain owned by their
    // existing domains and are kept valid by RenderCommand FIFO ordering.
    class MeshBatch final
    {
      public:
        MeshBatch(const PrimitiveSceneProxy& scene_proxy, const VertexFactory& vertex_factory,
                  RHIIndexBufferBinding index_buffer, MaterialRenderProxy& material_render_proxy,
                  std::uint32_t first_index, std::uint32_t index_count, std::uint32_t section_index = 0,
                  RHIBufferViewRef bone_matrices = {}, std::uint32_t num_bone_influences = 0,
                  bool has_valid_tangent_frame = false);

        const PrimitiveSceneProxy& scene_proxy() const
        {
            return *scene_proxy_;
        }
        const RHIIndexBufferBinding& index_buffer_binding() const
        {
            return index_buffer_;
        }
        const VertexFactory& vertex_factory() const
        {
            return *vertex_factory_;
        }
        MaterialRenderProxy& material_render_proxy() const
        {
            return *material_render_proxy_;
        }
        const ObjectShaderParameters& object_shader_parameters() const
        {
            return object_shader_parameters_;
        }
        const RHIBufferViewRef& bone_matrices() const
        {
            return bone_matrices_;
        }
        bool has_valid_tangent_frame() const
        {
            return has_valid_tangent_frame_;
        }
        ShaderMapProgramResult find_program(const ShaderMapCollection& shader_map, shader::ShaderPassRole role) const;
        ShaderMapProgramResult material_program(bool shadow_active = false, bool environment_active = false) const;
        ShaderMapProgramResult mesh_pass_program(shader::ShaderPassRole role,
                                                 const ShaderMapCollection& default_shader_map) const;
        std::uint64_t object_data_generation() const
        {
            return object_data_generation_;
        }
        RHIBindingSetRef material_binding(const ShaderMapProgram& program) const;
        const RHIBindingSetRef& object_binding() const
        {
            return object_binding_;
        }
        // MaterialRenderProxy remains the persistent owner; the renderer only
        // snapshots its current binding into this frame-local draw input.
        void publish_material_binding(ShaderMapProgramRef program, RHIBindingSetRef binding_set);
        // create_object_shader_bindings() is the only frame-local creation path;
        // mesh passes consume the published draw-data reference directly.
        void publish_object_binding(RHIBindingSetRef binding_set);
        std::uint32_t first_index() const
        {
            return first_index_;
        }
        std::uint32_t index_count() const
        {
            return index_count_;
        }
        std::uint32_t section_index() const
        {
            return section_index_;
        }

      private:
        const PrimitiveSceneProxy* scene_proxy_ = nullptr;
        const VertexFactory* vertex_factory_ = nullptr;
        RHIIndexBufferBinding index_buffer_;
        MaterialRenderProxy* material_render_proxy_ = nullptr;
        ObjectShaderParameters object_shader_parameters_;
        std::uint64_t object_data_generation_ = 0u;
        RHIBufferViewRef bone_matrices_;
        std::vector<std::pair<ShaderMapProgramRef, RHIBindingSetRef>> material_bindings_;
        RHIBindingSetRef object_binding_;
        std::uint32_t first_index_ = 0;
        std::uint32_t index_count_ = 0;
        std::uint32_t section_index_ = 0;
        bool has_valid_tangent_frame_ = false;
    };
} // namespace toy3d
