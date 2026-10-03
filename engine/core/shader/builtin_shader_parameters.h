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

    // Engine-owned Forward Pass constants are canonical for every Material
    // source. Features/macros govern active use, not author-defined ABI layouts.
    constexpr BuiltinShaderParameter builtin_forward_parameters[] = {
        {BindingGroup::Pass, "scene_light_direction", ShaderValueType::Float32x4},
        {BindingGroup::Pass, "scene_light_color", ShaderValueType::Float32x4},
        {BindingGroup::Pass, "point_light_positions", ShaderValueType::Float32x4x4},
        {BindingGroup::Pass, "point_light_colors", ShaderValueType::Float32x4x4},
        {BindingGroup::Pass, "point_light_count", ShaderValueType::Float32},
        {BindingGroup::Pass, "shadow_cascade_0_world_to_clip", ShaderValueType::Float32x4x4},
        {BindingGroup::Pass, "shadow_cascade_1_world_to_clip", ShaderValueType::Float32x4x4},
        {BindingGroup::Pass, "shadow_cascade_2_world_to_clip", ShaderValueType::Float32x4x4},
        {BindingGroup::Pass, "shadow_distance_data", ShaderValueType::Float32x4},
        {BindingGroup::Pass, "shadow_split_data", ShaderValueType::Float32x4},
        {BindingGroup::Pass, "shadow_cascade_0_region", ShaderValueType::Float32x4},
        {BindingGroup::Pass, "shadow_cascade_1_region", ShaderValueType::Float32x4},
        {BindingGroup::Pass, "shadow_cascade_2_region", ShaderValueType::Float32x4},
        {BindingGroup::Pass, "shadow_texel_size", ShaderValueType::Float32x4},
        {BindingGroup::Pass, "shadow_receiver_parameters", ShaderValueType::Float32x4},
        {BindingGroup::Pass, "environment_world_to_cube", ShaderValueType::Float32x4x4},
        {BindingGroup::Pass, "environment_parameters", ShaderValueType::Float32x4}};

    struct BuiltinForwardResource
    {
        const char* name;
        ResourceKind kind;
        ShaderResourceElementType element_type;
    };
    constexpr BuiltinForwardResource builtin_forward_resources[] = {
        {"shadow_atlas", ResourceKind::Texture2D, ShaderResourceElementType::Float},
        {"shadow_sampler", ResourceKind::Sampler, ShaderResourceElementType::None},
        {"environment_cube", ResourceKind::TextureCube, ShaderResourceElementType::Float4},
        {"environment_sampler", ResourceKind::Sampler, ShaderResourceElementType::None}};

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
        {BindingGroup::Object, "toy_num_bone_influences", ShaderValueType::UInt32}};
} // namespace toy3d::shader
