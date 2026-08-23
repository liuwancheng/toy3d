#pragma once

#include "rendercore/geometry/vertex_factory.h"

#include <vector>

namespace toy3d
{
    // StaticMesh vertex factory for the fixed first-stage logical stream set.
    class LocalVertexFactory final : public VertexFactory
    {
    public:
        explicit LocalVertexFactory(std::vector<VertexStreamComponent> components);

        RHIStatus validate_streams() const;

        RHIStatus build_vertex_input(
            const std::vector<ShaderVertexInput>& shader_inputs,
            std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>& vertex_layouts,
            std::vector<RHIGraphicsPipelineDesc::VertexAttribute>& vertex_attributes,
            std::vector<RHIVertexBufferBinding>& vertex_bindings) const override;

    private:
        std::vector<VertexStreamComponent> stream_components;
    };
}
