#pragma once

#include "drivers/rhi/rhi_result.h"
#include "math/matrix4.h"

#include <memory>

namespace toy3d
{
    class RHIBindingSet;
    class RHICommandContext;
    class RHIDevice;

    // Render-side canonical values for the Object logical Binding Group.
    // The value is copied from PrimitiveSceneProxy state and contains no Game,
    // RHI, or backend ownership.
    struct PrimitiveUniformShaderParameters
    {
        Matrix4 object_to_world;
    };

    RHIResult<std::shared_ptr<RHIBindingSet>> materialize_primitive_uniform_shader_parameters(
        RHIDevice& device, RHICommandContext& context, const PrimitiveUniformShaderParameters& parameters);
} // namespace toy3d
