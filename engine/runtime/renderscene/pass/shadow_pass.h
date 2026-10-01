#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "rendercore/shader/shader_map.h"

#include <cstddef>

namespace toy3d
{
    class RHIDevice;
    class RHIShaderProgramCache;
    class RHIGraphicsCommandContext;
    class ViewInfo;
    class ShadowRenderTargets;

    // Clears one View atlas once and records all active cascades in the caller's context.
    RHIStatus render_shadow_pass(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                 RHIGraphicsCommandContext& context, const ViewInfo& view,
                                 const ShadowRenderTargets& targets, std::size_t view_index,
                                 const ShaderMapProgramRef& shader_program);
} // namespace toy3d
