#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "rendercore/shader/rhi_shader_program_cache.h"

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class RHIShaderProgramCache;
    class GlobalShaderMap;
    class GlobalShaderType;
    class RenderScene;
    class SceneRenderTargets;
    class ViewInfo;

    const GlobalShaderType& environment_background_global_shader_type();

    // RT-owned immutable pipeline. Commands retain the pipeline, Cube and bindings until GPU completion.
    class EnvironmentBackgroundPassResources final
    {
      public:
        RHIStatus initialize(RHIDevice& device, RHIShaderProgramCache& cache, const GlobalShaderMap& shaders);
        RHIStatus render(RHIDevice& device, RHIGraphicsCommandContext& context, RenderScene& scene,
                         const SceneRenderTargets& targets, const ViewInfo& view) const;

      private:
        RHIShaderProgramRef program_;
        RHIGraphicsPipelineRef pipeline_;
    };
} // namespace toy3d
