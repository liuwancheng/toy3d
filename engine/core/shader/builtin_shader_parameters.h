#pragma once

#include "shader/shader_format_types.h"

namespace toy3d::shader
{
    struct BuiltinShaderParameter
    {
        BindingGroup group;
        const char* name;
        ShaderValueType type;
    };

    // The compiler derives the built-in logical schema, HLSL bindings and C++ parameters from this list.
    constexpr BuiltinShaderParameter builtin_shader_parameters[] = {
        {BindingGroup::View, "toy_view", ShaderValueType::Float32x4x4},
        {BindingGroup::View, "toy_projection", ShaderValueType::Float32x4x4},
        {BindingGroup::View, "toy_view_projection", ShaderValueType::Float32x4x4},
        {BindingGroup::View, "toy_inverse_view", ShaderValueType::Float32x4x4},
        {BindingGroup::View, "toy_inverse_projection", ShaderValueType::Float32x4x4},
        {BindingGroup::View, "toy_inverse_view_projection", ShaderValueType::Float32x4x4},
        {BindingGroup::View, "toy_camera_position", ShaderValueType::Float32x3},
        {BindingGroup::View, "toy_camera_direction", ShaderValueType::Float32x3},
        {BindingGroup::Object, "toy_object_to_world", ShaderValueType::Float32x4x4},
        {BindingGroup::Object, "toy_object_normal_to_world", ShaderValueType::Float32x4x4},
        {BindingGroup::Object, "toy_receives_shadows", ShaderValueType::Float32},
        {BindingGroup::Object, "toy_num_bone_influences", ShaderValueType::UInt32}};
} // namespace toy3d::shader
