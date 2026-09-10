#pragma once

#include "drivers/rhi/rhi_result.h"

#include <vector>

namespace toy3d
{
    class RHICommandContext;
    class RHIDevice;
    class ViewInfo;

    // Resolves persistent Material owner bindings before any mesh render pass
    // and snapshots successful results into the current frame's MeshBatch data.
    RHIStatus create_material_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                              std::vector<ViewInfo>& view_infos);
} // namespace toy3d
