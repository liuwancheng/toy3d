#pragma once

#include "drivers/rhi/rhi_command_descriptors.h"
#include "rendercore/shader/shader_vertex_input.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace toy3d
{
    // Render-side value that references a buffer kept alive by the owning mesh
    // render data. Locking the weak reference creates only frame-local RHI holds.
    struct VertexStreamComponent
    {
        ShaderVertexAttributeId attribute_id = ShaderVertexAttributeId::Position0;
        std::uint32_t stream_index = 0;
        std::uint32_t byte_offset = 0;
        std::uint32_t stride = 0;
        PixelFormat format = PixelFormat::Unknown;
        std::weak_ptr<RHIBuffer> buffer;
    };

    // Render-side stream interpreter. It does not own mesh assets or RHI
    // resources and does not choose shaders, materials, permutations, or state.
    class VertexFactory
    {
      public:
        virtual ~VertexFactory() = default;

        virtual RHIStatus build_vertex_input(const std::vector<ShaderVertexInput>& shader_inputs,
                                             std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>& vertex_layouts,
                                             std::vector<RHIGraphicsPipelineDesc::VertexAttribute>& vertex_attributes,
                                             std::vector<RHIVertexBufferBinding>& vertex_bindings) const = 0;
    };
} // namespace toy3d
