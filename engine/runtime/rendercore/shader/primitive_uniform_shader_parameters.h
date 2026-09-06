#pragma once

#include "drivers/rhi/rhi_result.h"
#include "math/matrix4.h"

#include <memory>

namespace toy3d
{
    class RHIBindingLayout;
    class RHIBindingSet;
    class RHICommandContext;
    class RHIDevice;
    class ShaderMapProgram;

    // Render-side canonical values for the Object logical Binding Group.
    // The value is copied from PrimitiveSceneProxy state and contains no Game,
    // RHI, or backend ownership.
    struct PrimitiveUniformShaderParameters
    {
        Matrix4 object_to_world;
    };

    RHIResult<std::shared_ptr<RHIBindingSet>> materialize_primitive_uniform_shader_parameters(
        RHIDevice& device, RHICommandContext& context, const std::shared_ptr<RHIBindingLayout>& binding_layout,
        const ShaderMapProgram& shader_program, const PrimitiveUniformShaderParameters& parameters);
} // namespace toy3d
