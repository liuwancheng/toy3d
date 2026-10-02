#pragma once

#include "rendercore/geometry/vertex_factory.h"

namespace toy3d
{
    // Four/eight section-local UInt indices and UNorm weights share one shader.
    // Shader selection remains the renderer's responsibility.
    class GPUSkinVertexFactory final : public VertexFactory
    {
      public:
        GPUSkinVertexFactory(std::vector<VertexStreamComponent> components, std::uint32_t num_bone_influences);
        std::uint32_t num_bone_influences() const
        {
            return num_bone_influences_;
        }
        shader::VertexFactoryType type() const override
        {
            return shader::VertexFactoryType::GPUSkin;
        }
        RHIStatus validate_streams() const;
        RHIStatus build_vertex_input(const std::vector<ShaderVertexInput>& shader_inputs,
                                     std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>& vertex_layouts,
                                     std::vector<RHIGraphicsPipelineDesc::VertexAttribute>& vertex_attributes,
                                     std::vector<RHIVertexBufferBinding>& vertex_bindings) const override;

      private:
        std::uint32_t num_bone_influences_ = 0;
        std::vector<VertexStreamComponent> stream_components;
    };
} // namespace toy3d
