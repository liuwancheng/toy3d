#include "rendercore/geometry/local_vertex_factory.h"

#include <utility>

namespace toy3d
{
    LocalVertexFactory::LocalVertexFactory(std::vector<VertexStreamComponent> components)
        : stream_components(std::move(components))
    {
    }

    RHIStatus LocalVertexFactory::validate_streams() const
    {
        for (const auto& component : stream_components)
        {
            if (component.attribute_id == ShaderVertexAttributeId::BlendIndices0 ||
                component.attribute_id == ShaderVertexAttributeId::BlendWeights0 ||
                component.attribute_id == ShaderVertexAttributeId::BlendIndices1 ||
                component.attribute_id == ShaderVertexAttributeId::BlendWeights1)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "LocalVertexFactory cannot contain skin streams.");
            }
        }
        return validate_stream_components(stream_components);
    }

    RHIStatus LocalVertexFactory::build_vertex_input(
        const std::vector<ShaderVertexInput>& shader_inputs,
        std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>& vertex_layouts,
        std::vector<RHIGraphicsPipelineDesc::VertexAttribute>& vertex_attributes,
        std::vector<RHIVertexBufferBinding>& vertex_bindings) const
    {
        const auto status = validate_streams();
        if (!status)
        {
            vertex_layouts.clear();
            vertex_attributes.clear();
            vertex_bindings.clear();
            return status;
        }
        return build_vertex_input_from_components(stream_components, shader_inputs, vertex_layouts, vertex_attributes,
                                                  vertex_bindings);
    }
} // namespace toy3d
