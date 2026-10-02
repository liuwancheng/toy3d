#pragma once

#include "drivers/rhi/rhi_command_descriptors.h"
#include "drivers/rhi/rhi_resource.h"

#include <cstdint>
#include <vector>

namespace toy3d
{
    class RHIDevice;
    class ShaderMapProgram;

    RHIStatus resolve_mesh_draw_binding(RHIDevice& device, const ShaderMapProgram& program, RHIBindingGroup group,
                                        const RHIBindingSetRef& owner_binding, RHIBindingSetRef& resolved_binding);

    // Frame-local, fully resolved input for one indexed mesh draw. RHI strong
    // references are intentionally copied into the command so execution never
    // reads scene, proxy, material, or MeshBatch preparation sources.
    struct MeshDrawCommand
    {
        RHIGraphicsPipelineRef pipeline;
        std::vector<RHIVertexBufferBinding> vertex_buffers;
        RHIIndexBufferBinding index_buffer;
        RHIGraphicsBindings bindings;
        RHIDrawIndexedArgs draw_args;
        std::uint64_t sort_key = 0;
    };

    // One render view's ordered commands for one concrete mesh pass. Attachment
    // scope remains owned by that pass rather than by this reusable draw layer.
    struct MeshPassDrawList
    {
        RHIViewport viewport;
        RHIRect scissor;
        std::vector<MeshDrawCommand> commands;
    };
} // namespace toy3d
