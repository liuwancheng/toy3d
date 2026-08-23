#pragma once

#include "drivers/rhi/rhi_public_definitions.h"
#include "format/shader_format_types.h"
#include "rendercore/shader/shader_vertex_input.h"

#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    enum class ShaderPlatform
    {
        VulkanPortableV1,
        D3D11SM5,
        D3D12SM6
    };

    using ShaderParameterId = std::uint64_t;
    using ShaderContentHash = shader::Sha256Hash;

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

    struct ShaderMapProgramKey
    {
        std::string shader_name;
        std::string pass_name;
        ShaderPlatform platform = ShaderPlatform::VulkanPortableV1;
        ShaderContentHash permutation_key = shader::default_shader_permutation_key;
    };

    struct ShaderMapBinding
    {
        ShaderParameterId parameter_id = 0;
        std::string name;
        RHIBindingGroup group = RHIBindingGroup::Material;
        RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
        RHIShaderStageFlags stages = RHIShaderStageFlags::None;
        std::uint32_t target_binding = 0;
        std::uint32_t array_count = 1;
        std::uint32_t constant_buffer_size = 0;

        struct ConstantMember
        {
            ShaderParameterId parameter_id = 0;
            std::string name;
            ShaderValueType type = ShaderValueType::Float32;
            std::uint32_t offset = 0;
            std::uint32_t size = 0;
            std::uint32_t array_stride = 0;
            std::uint32_t matrix_stride = 0;
        };

        std::vector<ConstantMember> constant_members;
    };

    struct ShaderMapStage
    {
        RHIShaderStage stage = RHIShaderStage::Vertex;
        std::string entry_point;
        std::vector<std::uint8_t> binary;
        ShaderContentHash content_hash{};
        std::vector<ShaderMapBinding> reflection;
        std::vector<shader::ReflectedInterfaceVariable> interface_variables;
    };

    struct ShaderMapProgramData
    {
        std::string shader_name;
        std::string pass_name;
        ShaderPlatform platform = ShaderPlatform::VulkanPortableV1;
        std::uint32_t mapping_version = 0;
        ShaderContentHash logical_layout_hash{};
        ShaderContentHash target_binding_hash{};
        ShaderContentHash pass_template_hash{};
        ShaderContentHash permutation_key{};
        std::vector<ShaderMapBinding> bindings;
        std::vector<ShaderMapStage> stages;
        std::vector<ShaderVertexInput> vertex_inputs;
    };
}
