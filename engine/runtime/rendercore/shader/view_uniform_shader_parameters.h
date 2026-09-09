#pragma once

#include "drivers/rhi/rhi_command_descriptors.h"
#include "math/matrix4.h"
#include "math/vector3.h"

#include <memory>

namespace toy3d
{
    class RHIBindingSet;
    class RHICommandContext;
    class RHIDevice;

    // Render-side canonical values for the View logical Binding Group.
    // Shader layout metadata serializes these values later; this type is not a
    // native constant-buffer layout and must not be uploaded with raw memcpy.
    struct ViewUniformShaderParameters
    {
        Matrix4 view_matrix;
        Matrix4 projection_matrix;
        Matrix4 view_projection_matrix;
        Matrix4 inverse_view_matrix;
        Matrix4 inverse_projection_matrix;
        Matrix4 inverse_view_projection_matrix;
        Vector3 camera_position;
        float camera_position_padding = 0.0f;
        Vector3 camera_direction;
        float camera_direction_padding = 0.0f;
    };

    RHIResult<RHIUniformBufferSlice> upload_view_uniform_shader_parameters(
        RHICommandContext& context, const ViewUniformShaderParameters& parameters);
    RHIResult<std::shared_ptr<RHIBindingSet>> create_view_uniform_shader_binding(
        RHIDevice& device, const RHIUniformBufferSlice& slice);
} // namespace toy3d
