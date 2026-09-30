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

    // Records one View cascade's depth pass in the caller's graphics context.
    RHIStatus render_shadow_pass(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                 RHIGraphicsCommandContext& context, const ViewInfo& view, std::size_t cascade_index,
                                 const RHITextureRef& texture, const RHITextureViewRef& depth_view,
                                 RHIAccess before_access, const ShaderMapProgramRef& shader_program);
} // namespace toy3d
