#include "layout/shader_layout.h"

#include <algorithm>
#include <utility>

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

    std::vector<ShaderResourceParameter> builtin_forward_resource_inputs()
    {
        std::vector<ShaderResourceParameter> result;
        for (const auto& input : builtin_forward_resources)
        {
            ShaderResourceParameter resource;
            resource.name = input.name;
            resource.group = BindingGroup::Pass;
            resource.category = input.kind == ResourceKind::Sampler ? ShaderParameterCategory::Sampler
                                                                    : ShaderParameterCategory::SampledTexture;
            resource.resource_kind = input.kind;
            resource.element_type = input.element_type;
            resource.texture_usage = TextureUsage::LinearData;
            resource.parameter_id = make_shader_parameter_id(resource.group, resource.category, resource.name);
            result.push_back(std::move(resource));
        }
        std::sort(result.begin(), result.end(),
                  [](const ShaderResourceParameter& left, const ShaderResourceParameter& right)
                  {
                      if (left.category != right.category)
                      {
                          return left.category < right.category;
                      }
                      return left.parameter_id < right.parameter_id;
                  });
        return result;
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
        if (group == BindingGroup::Pass)
        {
            for (const auto& parameter : builtin_forward_parameters)
            {
                input.constant_members.push_back({parameter.name, parameter.type});
            }
        }
        return input;
    }
} // namespace toy3d::shader
