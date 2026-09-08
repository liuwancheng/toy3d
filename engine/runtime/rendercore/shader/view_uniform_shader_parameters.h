#pragma once

#include "drivers/rhi/rhi_result.h"
#include "math/matrix4.h"
#include "math/vector3.h"

#include <memory>

namespace toy3d
{
    class RHIBindingLayout;
    class RHIBindingSet;
    class RHIBuffer;
    class RHICommandContext;
    class RHIDevice;
    class ShaderMapProgram;

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

    RHIResult<std::shared_ptr<RHIBuffer>> create_view_uniform_shader_buffer(
        RHIDevice& device, RHICommandContext& context, const ViewUniformShaderParameters& parameters);

    RHIStatus validate_view_uniform_shader_program(const ShaderMapProgram& shader_program);

    RHIResult<std::shared_ptr<RHIBindingSet>> create_view_uniform_shader_binding(
        RHIDevice& device, const std::shared_ptr<RHIBindingLayout>& binding_layout,
        const ShaderMapProgram& shader_program, const std::shared_ptr<RHIBuffer>& buffer);
} // namespace toy3d
