#pragma once

#include "format/sha256.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    using ShaderParameterId = std::uint64_t;
    using ShaderVariantId = std::uint64_t;
    using ShaderEnumValueId = std::uint64_t;

    constexpr std::uint32_t shader_compile_request_version = 1;
    constexpr std::uint32_t shader_variant_id_version = 1;
    constexpr std::uint32_t shader_permutation_version = 1;
    constexpr std::uint32_t toy_shader_abi_version = 1;
    constexpr std::uint32_t shader_parameter_id_version = 1;
    constexpr std::uint32_t d3d_binding_mapping_version = 1;
    constexpr std::uint32_t vulkan_binding_mapping_version = 1;
    constexpr std::uint32_t max_constant_buffer_size = 16u * 1024u;
    constexpr Sha256Hash default_shader_permutation_key = {
        0x7d, 0x45, 0x04, 0x65, 0xce, 0xb4, 0x90, 0x83,
        0x70, 0x8a, 0x69, 0x70, 0x82, 0x7f, 0x0e, 0x0b,
        0x11, 0x6e, 0xd2, 0x85, 0x07, 0x2a, 0x95, 0xb4,
        0x51, 0xe5, 0x5f, 0x58, 0x3f, 0x56, 0xda, 0x8d};

    enum class BindingGroup
    {
        Global,
        View,
        Pass,
        Material,
        Object
    };

    enum class ResourceKind
    {
        Texture2D,
        Texture2DArray,
        Texture3D,
        TextureCube,
        Texture2DMS,
        Sampler,
        ComparisonSampler,
        Buffer,
        ByteAddressBuffer,
        StructuredBuffer,
        RWBuffer,
        RWByteAddressBuffer,
        RWStructuredBuffer,
        RWTexture2D,
        RWTexture2DArray,
        RWTexture3D
    };

    enum class ShaderTarget
    {
        D3D11Dxbc,
        D3D12Dxil,
        VulkanSpirV
    };

    enum class ShaderCompileProfile
    {
        VulkanPortableV1,
        D3D11FeatureLevel11_0,
        D3D12ShaderModel6
    };

    enum class ShaderDebugMode
    {
        Debug,
        Development,
        Shipping
    };

    enum class NativeRegisterClass
    {
        ConstantBuffer,
        ShaderResource,
        Sampler,
        UnorderedAccess
    };

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

    struct ShaderDependency
    {
        std::string virtual_path;
        Sha256Hash content_hash{};
    };

    struct ShaderCompileRequest
    {
        std::uint32_t version = shader_compile_request_version;
        ShaderTarget target = ShaderTarget::VulkanSpirV;
        ShaderCompileProfile profile = ShaderCompileProfile::VulkanPortableV1;
        ShaderStageFlags stage = ShaderStageFlags::None;
        ShaderDebugMode debug_mode = ShaderDebugMode::Development;
        std::string entry_point;
        std::string source_virtual_path;
        std::string compiler_identity;
        std::string source;
        std::vector<ShaderDependency> dependencies;
        Sha256Hash logical_layout_hash{};
        Sha256Hash target_binding_hash{};
        Sha256Hash compile_key{};
    };

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
        // Only resource bindings have a ResourceKind; optional keeps constant
        // members representable without an artificial sentinel enum value.
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

    struct ShaderMapBinding
    {
        ShaderParameterId binding_id = 0;
        std::string name;
        BindingGroup group = BindingGroup::Material;
        ShaderParameterCategory category = ShaderParameterCategory::Constant;
        ShaderStageFlags stages = ShaderStageFlags::None;
        NativeRegisterClass register_class = NativeRegisterClass::ConstantBuffer;
        std::uint32_t register_index = 0;
        std::uint32_t descriptor_set = 0;
        std::uint32_t descriptor_binding = 0;
    };

    struct ShaderCodeEntry
    {
        ShaderCompileRequest request;
        ShaderStageReflection reflection;
        std::vector<std::uint8_t> binary;
    };

    struct ShaderMapEntry
    {
        std::string shader_name;
        std::string pass_name;
        ShaderTarget target = ShaderTarget::VulkanSpirV;
        ShaderCompileProfile profile = ShaderCompileProfile::VulkanPortableV1;
        Sha256Hash logical_layout_hash{};
        Sha256Hash target_binding_hash{};
        Sha256Hash pass_template_hash{};
        std::uint32_t variant_id_version = shader_variant_id_version;
        std::uint32_t permutation_version = shader_permutation_version;
        Sha256Hash permutation_key{};
        std::uint32_t mapping_version = 0;
        std::vector<ShaderMapBinding> bindings;
        std::vector<ShaderCodeEntry> stages;
    };

    Sha256Hash calculate_target_binding_hash(
        ShaderTarget target,
        std::uint32_t mapping_version,
        const std::vector<ShaderMapBinding>& bindings);
    Sha256Hash calculate_shader_stage_reflection_hash(
        const ShaderStageReflection& reflection);
}
