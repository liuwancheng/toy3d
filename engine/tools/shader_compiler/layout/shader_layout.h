#pragma once

#include "format/shader_format_types.h"
#include "frontend/diagnostic.h"
#include "frontend/shader_ast.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toy3d::shader
{
    struct ConstantMemberInput
    {
        std::string name;
        ShaderValueType type = ShaderValueType::Float32;
        std::uint32_t array_count = 1;
        SourceLocation location;
    };

    struct ShaderConstantMember
    {
        ShaderParameterId parameter_id = 0;
        std::string name;
        ShaderValueType type = ShaderValueType::Float32;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t array_count = 1;
        std::uint32_t array_stride = 0;
        std::uint32_t matrix_stride = 0;
        std::vector<std::uint8_t> default_value;
        SourceLocation location;
    };

    struct ConstantBufferLayout
    {
        BindingGroup group = BindingGroup::Material;
        std::uint32_t size = 0;
        std::vector<ShaderConstantMember> members;
    };

    struct ConstantBufferPackResult
    {
        // optional keeps layout results absent on validation failure so callers
        // cannot consume partially packed or partially activated data.
        std::optional<ConstantBufferLayout> layout;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    struct ShaderResourceParameter
    {
        ShaderParameterId parameter_id = 0;
        std::string name;
        BindingGroup group = BindingGroup::Material;
        ShaderParameterCategory category = ShaderParameterCategory::SampledTexture;
        ResourceKind resource_kind = ResourceKind::Texture2D;
        ResourceElementType element_type = ResourceElementType::None;
        std::uint32_t array_count = 1;
        DefaultValue default_value;
        SourceLocation location;
    };

    struct LogicalShaderLayout
    {
        std::vector<ConstantBufferLayout> constant_buffers;
        std::vector<ShaderResourceParameter> resources;
        Sha256Hash parameter_schema_hash{};
        Sha256Hash logical_layout_hash{};
    };

    struct LogicalLayoutResult
    {
        std::optional<LogicalShaderLayout> layout;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    struct ParameterUsage
    {
        std::string name;
        ShaderStageFlags stages = ShaderStageFlags::None;
    };

    struct ActiveBinding
    {
        ShaderParameterId binding_id = 0;
        std::string name;
        BindingGroup group = BindingGroup::Material;
        ShaderParameterCategory category = ShaderParameterCategory::Constant;
        ShaderStageFlags stages = ShaderStageFlags::None;
        const ConstantBufferLayout* constant_buffer = nullptr;
        const ShaderResourceParameter* resource = nullptr;
    };

    struct ActiveShaderLayout
    {
        const LogicalShaderLayout* logical_layout = nullptr;
        std::vector<ActiveBinding> bindings;
    };

    struct ActiveLayoutResult
    {
        std::optional<ActiveShaderLayout> layout;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    std::uint32_t structured_element_stride(ResourceElementType type);
    ConstantBufferPackResult pack_constant_buffer(
        BindingGroup group,
        const std::vector<ConstantMemberInput>& members);
    LogicalLayoutResult compile_logical_layout(const ShaderAsset& asset);
    ActiveLayoutResult build_active_layout(
        const LogicalShaderLayout& logical_layout,
        const std::vector<ParameterUsage>& usage);
}
