#include "rendercore/geometry/gpu_skin_vertex_factory.h"

#include <algorithm>
#include <utility>

#include "asset/mesh/skeletal_mesh_asset.h"

namespace toy3d
{
    GPUSkinVertexFactory::GPUSkinVertexFactory(std::vector<VertexStreamComponent> components,
                                               std::uint32_t num_bone_influences)
        : num_bone_influences_(num_bone_influences), stream_components(std::move(components))
    {
        if (num_bone_influences == skin_influences_per_group)
        {
            // Both formats satisfy one reflected shader input contract. Reuse the
            // original four-slot bytes; the draw-uniform branch never skins them twice.
            for (const auto attribute :
                 {ShaderVertexAttributeId::BlendIndices0, ShaderVertexAttributeId::BlendWeights0})
            {
                const auto extra = attribute == ShaderVertexAttributeId::BlendIndices0
                                       ? ShaderVertexAttributeId::BlendIndices1
                                       : ShaderVertexAttributeId::BlendWeights1;
                const auto original = std::find_if(stream_components.begin(), stream_components.end(),
                                                   [attribute](const VertexStreamComponent& component)
                                                   {
                                                       return component.attribute_id == attribute;
                                                   });
                const auto existing = std::find_if(stream_components.begin(), stream_components.end(),
                                                   [extra](const VertexStreamComponent& component)
                                                   {
                                                       return component.attribute_id == extra;
                                                   });
                if (original != stream_components.end() && existing == stream_components.end())
                {
                    auto alias = *original;
                    alias.attribute_id = extra;
                    stream_components.push_back(std::move(alias));
                }
            }
        }
    }

    RHIStatus GPUSkinVertexFactory::validate_streams() const
    {
        if (num_bone_influences_ != skin_influences_per_group && num_bone_influences_ != max_skin_influences)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "GPUSkin requires four or eight storage slots.");
        }
        const auto status = validate_stream_components(stream_components);
        if (!status)
        {
            return status;
        }
        const ShaderVertexAttributeId required[] = {
            ShaderVertexAttributeId::BlendIndices0, ShaderVertexAttributeId::BlendWeights0,
            ShaderVertexAttributeId::BlendIndices1, ShaderVertexAttributeId::BlendWeights1};
        const VertexStreamComponent* first = nullptr;
        const std::uint32_t offsets[] = {0u, num_bone_influences_,
                                         num_bone_influences_ == max_skin_influences ? 4u : 0u,
                                         num_bone_influences_ == max_skin_influences ? 12u : 4u};
        std::size_t index = 0;
        for (const auto attribute : required)
        {
            const auto found = std::find_if(stream_components.begin(), stream_components.end(),
                                            [attribute](const VertexStreamComponent& component)
                                            {
                                                return component.attribute_id == attribute;
                                            });
            if (found == stream_components.end())
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "GPUSkinVertexFactory requires bone index and weight streams.");
            }
            if (!first)
            {
                first = &*found;
            }
            if (found->stride != num_bone_influences_ * 2u || found->byte_offset != offsets[index++] ||
                found->stream_index != first->stream_index || found->buffer.lock() != first->buffer.lock())
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "GPUSkin attributes disagree with the four/eight-slot storage layout.");
            }
        }
        return RHIStatus::success();
    }

    RHIStatus GPUSkinVertexFactory::build_vertex_input(
        const std::vector<ShaderVertexInput>& shader_inputs,
        std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>& vertex_layouts,
        std::vector<RHIGraphicsPipelineDesc::VertexAttribute>& vertex_attributes,
        std::vector<RHIVertexBufferBinding>& vertex_bindings) const
    {
        vertex_layouts.clear();
        vertex_attributes.clear();
        vertex_bindings.clear();
        const auto status = validate_streams();
        if (!status)
        {
            return status;
        }
        const ShaderVertexAttributeId required[] = {
            ShaderVertexAttributeId::BlendIndices0, ShaderVertexAttributeId::BlendWeights0,
            ShaderVertexAttributeId::BlendIndices1, ShaderVertexAttributeId::BlendWeights1};
        for (const auto attribute : required)
        {
            if (std::none_of(shader_inputs.begin(), shader_inputs.end(),
                             [attribute](const ShaderVertexInput& input)
                             {
                                 return input.attribute_id == attribute;
                             }))
            {
                return RHIStatus::failure(RHIErrorCode::Unsupported,
                                          "GPUSkin shader must consume bone index and weight attributes.");
            }
        }
        return build_vertex_input_from_components(stream_components, shader_inputs, vertex_layouts, vertex_attributes,
                                                  vertex_bindings);
    }
} // namespace toy3d
