#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"

#include <vector>

namespace toy3d
{
    class RHICommandContext;
    class RHIDevice;
    class ShaderMapProgram;
    class ViewInfo;

    RHIStatus prepare_view_uniform_resources(RHIDevice& device, RHICommandContext& context,
                                             std::vector<ViewInfo>& view_infos);
    RHIResult<RHIBindingSetRef> resolve_view_uniform_binding(RHIDevice& device, const ViewInfo& view_info,
                                                             const RHIBindingLayoutRef& binding_layout,
                                                             const ShaderMapProgram& shader_program);
} // namespace toy3d
