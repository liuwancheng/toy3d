#pragma once

#include "drivers/rhi/rhi_result.h"

#include <vector>

namespace toy3d
{
    class RHICommandContext;
    class RHIDevice;
    class ViewInfo;

    // Creates each unique Primitive/object-data generation once and publishes
    // the shared result only into frame-local MeshBatch values.
    RHIStatus create_object_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                            std::vector<ViewInfo>& view_infos);
} // namespace toy3d
