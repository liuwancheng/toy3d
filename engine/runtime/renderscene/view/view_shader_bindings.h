#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"

#include <vector>

namespace toy3d
{
    class RHICommandContext;
    class RHIDevice;
    class ViewInfo;

    RHIStatus create_view_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                          std::vector<ViewInfo>& view_infos);
} // namespace toy3d
