#pragma once

#include "compiler/compile_request.h"

#include <optional>
#include <vector>

namespace toy3d::shader
{
    struct ReflectedConstantMember
    {
        ShaderParameterId parameter_id = 0;
        std::string name;
        ShaderValueType type = ShaderValueType::Float32;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t array_stride = 0;
        std::uint32_t matrix_stride = 0;
    };

    struct ReflectedBinding
    {
        ShaderParameterId parameter_id = 0;
        std::string name;
        BindingGroup group = BindingGroup::Material;
        ShaderParameterCategory category = ShaderParameterCategory::Constant;
        std::optional<ResourceKind> resource_kind;
        ShaderStageFlags stages = ShaderStageFlags::None;
        std::uint32_t array_count = 1;
        std::uint32_t descriptor_set = 0;
        std::uint32_t descriptor_binding = 0;
        std::uint32_t constant_buffer_size = 0;
        std::vector<ReflectedConstantMember> constant_members;
    };

    struct ReflectedInterfaceVariable
    {
        enum class ScalarType
        {
            Float32,
            Int32,
            UInt32
        };

        std::string name;
        std::string semantic;
        std::uint32_t location = 0;
        bool input = true;
        ScalarType scalar_type = ScalarType::Float32;
        std::uint32_t component_count = 1;
    };

    struct ShaderStageReflection
    {
        ShaderStageFlags stage = ShaderStageFlags::None;
        std::string entry_point;
        std::vector<ReflectedBinding> bindings;
        std::vector<ReflectedInterfaceVariable> interface_variables;
        std::uint32_t thread_group_size_x = 0;
        std::uint32_t thread_group_size_y = 0;
        std::uint32_t thread_group_size_z = 0;
        Sha256Hash reflection_hash{};
    };

    struct SpirvReflectionResult
    {
        std::optional<ShaderStageReflection> reflection;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    SpirvReflectionResult reflect_and_validate_spirv(
        const std::vector<std::uint8_t>& binary,
        const ShaderCompileRequest& request,
        const TargetBindingLayout& expected_layout,
        bool require_all_expected_bindings = true);
}
