#pragma once

#include "common/sha256.h"
#include "frontend/diagnostic.h"
#include "frontend/shader_ast.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toy3d::shader
{
    using ShaderParameterId = std::uint64_t;

    constexpr std::uint32_t toy_shader_abi_version = 1;
    constexpr std::uint32_t shader_parameter_id_version = 1;
    constexpr std::uint32_t d3d_binding_mapping_version = 1;
    constexpr std::uint32_t vulkan_binding_mapping_version = 1;
    constexpr std::uint32_t max_constant_buffer_size = 16u * 1024u;

    enum class ShaderValueType
    {
        Float32,
        Float32x2,
        Float32x3,
        Float32x4,
        Int32,
        Int32x2,
        Int32x3,
        Int32x4,
        UInt32,
        UInt32x2,
        UInt32x3,
        UInt32x4,
        Float32x2x2,
        Float32x2x3,
        Float32x2x4,
        Float32x3x2,
        Float32x3x3,
        Float32x3x4,
        Float32x4x2,
        Float32x4x3,
        Float32x4x4
    };

    enum class ShaderParameterCategory
    {
        Constant,
        SampledTexture,
        Sampler,
        ReadOnlyBuffer,
        StorageBuffer,
        StorageTexture
    };

    enum class ShaderStageFlags : std::uint8_t
    {
        None = 0,
        Vertex = 1u << 0u,
        Pixel = 1u << 1u,
        Compute = 1u << 2u
    };

    ShaderStageFlags operator|(ShaderStageFlags left, ShaderStageFlags right);
    ShaderStageFlags& operator|=(ShaderStageFlags& left, ShaderStageFlags right);
    bool has_stage(ShaderStageFlags flags, ShaderStageFlags stage);

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

    // v1 hashes three canonical ASCII fields, each prefixed by a little-endian
    // uint32 byte length: BindingGroup, ShaderParameterCategory, parameter name.
    ShaderParameterId make_shader_parameter_id(
        BindingGroup group,
        ShaderParameterCategory category,
        std::string_view name);
    std::uint32_t structured_element_stride(ResourceElementType type);
    ConstantBufferPackResult pack_constant_buffer(
        BindingGroup group,
        const std::vector<ConstantMemberInput>& members);
    LogicalLayoutResult compile_logical_layout(const ShaderAsset& asset);
    ActiveLayoutResult build_active_layout(
        const LogicalShaderLayout& logical_layout,
        const std::vector<ParameterUsage>& usage);
}
