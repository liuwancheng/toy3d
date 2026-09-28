#pragma once

#include "drivers/rhi/rhi_resource.h"

#include <vector>

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class RHIShaderProgramCache;
    class RHIStatus;
    class ViewInfo;

    // Logical-RT input for one BasePass invocation. View lifetime is borrowed
    // for the call; attachment strong references keep only declared resources.
    struct BasePassInputs
    {
        const std::vector<ViewInfo>& views;
        RHITextureViewRef scene_color;
        RHITextureViewRef scene_depth;
        RHIBindingSetRef lighting_binding;
    };

    RHIStatus render_base_pass(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                               RHIGraphicsCommandContext& context, const BasePassInputs& inputs);
} // namespace toy3d
