#include "layout/shader_layout.h"

#include "shader/builtin_shader_parameters.h"

namespace toy3d::shader
{
    ShaderParameterGroupInput builtin_shader_parameter_input(BindingGroup group)
    {
        ShaderParameterGroupInput input;
        input.group = group;
        for (const BuiltinShaderParameter& parameter : builtin_shader_parameters)
        {
            if (parameter.group == group)
            {
                input.constant_members.push_back({parameter.name, parameter.type});
            }
        }
        return input;
    }
} // namespace toy3d::shader
