#include "layout/shader_layout.h"

#include "shader/builtin_shader_parameters.h"

namespace toy3d::shader
{
    ShaderResourceParameter builtin_gpu_skin_resource()
    {
        ShaderResourceParameter resource;
        resource.name = "toy_bone_matrices";
        resource.group = BindingGroup::Object;
        resource.category = ShaderParameterCategory::ReadOnlyBuffer;
        resource.resource_kind = ResourceKind::Buffer;
        resource.element_type = ShaderResourceElementType::Float4;
        resource.parameter_id = make_shader_parameter_id(resource.group, resource.category, resource.name);
        return resource;
    }

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
