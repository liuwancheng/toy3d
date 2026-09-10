#include "layout/shader_layout.h"

namespace toy3d::shader
{
    ShaderParameterGroupInput builtin_shader_parameter_input(BindingGroup group)
    {
        ShaderParameterGroupInput input;
        input.group = group;
        switch (group)
        {
        case BindingGroup::Global:
            break;
        case BindingGroup::View:
            input.constant_members = {{"toy_view", ShaderValueType::Float32x4x4},
                                      {"toy_projection", ShaderValueType::Float32x4x4},
                                      {"toy_view_projection", ShaderValueType::Float32x4x4},
                                      {"toy_inverse_view", ShaderValueType::Float32x4x4},
                                      {"toy_inverse_projection", ShaderValueType::Float32x4x4},
                                      {"toy_inverse_view_projection", ShaderValueType::Float32x4x4},
                                      {"toy_camera_position", ShaderValueType::Float32x3},
                                      {"toy_camera_direction", ShaderValueType::Float32x3}};
            break;
        case BindingGroup::Object:
            input.constant_members = {{"toy_object_to_world", ShaderValueType::Float32x4x4}};
            break;
        case BindingGroup::Pass:
        case BindingGroup::Material:
            break;
        }
        return input;
    }
} // namespace toy3d::shader
