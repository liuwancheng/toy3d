#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/view/scene_view.h"

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class GlobalShaderMap;
    class GlobalShaderType;
    class SceneRenderTargets;
    class ViewInfo;

    const GlobalShaderType& debug_lines_global_shader_type();

    // RT-owned pipelines share one program. Depth visibility is raster state,
    // not a shader permutation. Each recording owns its uploaded line vertices.
    class DebugLinePassResources final
    {
      public:
        RHIStatus initialize(RHIDevice& device, RHIShaderProgramCache& cache, const GlobalShaderMap& shaders);
        RHIStatus render(RHIDevice& device, RHIGraphicsCommandContext& context, const SceneRenderTargets& targets,
                         const ViewInfo& view, const std::vector<DebugLineVertex>& vertices, bool depth_test) const;

      private:
        RHIShaderProgramRef program_;
        RHIGraphicsPipelineRef depth_pipeline_;
        RHIGraphicsPipelineRef overlay_pipeline_;
    };
} // namespace toy3d
