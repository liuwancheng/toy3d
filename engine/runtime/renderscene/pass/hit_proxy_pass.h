#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "rendercore/shader/global_shader_type.h"
#include "rendercore/hit_proxy.h"

#include <vector>

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class RHIShaderProgramCache;
    class GlobalShaderMap;
    class ViewInfo;

    const GlobalShaderType& hit_proxy_global_shader_type();

    // Called only for an editor click. Reuses the visible frame's MeshBatches
    // and their View/Object bindings, but clears and owns separate ID/depth targets.
    RHIStatus render_hit_proxy_pass(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                    const GlobalShaderMap& global_shader_map, RHIGraphicsCommandContext& context,
                                    const std::vector<ViewInfo>& views, const RHITextureViewRef& id_view,
                                    const RHITextureViewRef& depth_view, HitProxyTable& table);
} // namespace toy3d
