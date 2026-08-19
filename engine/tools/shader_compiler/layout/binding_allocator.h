#pragma once

#include "layout/shader_layout.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace toy3d::shader
{
    struct BindingClassLimits
    {
        std::uint32_t constant_buffers = 0;
        std::uint32_t shader_resources = 0;
        std::uint32_t samplers = 0;
        std::uint32_t unordered_access = 0;
    };

    struct DescriptorLimits
    {
        std::uint32_t uniform_buffers = 0;
        std::uint32_t sampled_images = 0;
        std::uint32_t samplers = 0;
        std::uint32_t uniform_texel_buffers = 0;
        std::uint32_t storage_buffers = 0;
        std::uint32_t storage_texel_buffers = 0;
        std::uint32_t storage_images = 0;
    };

    struct TargetBindingLimits
    {
        std::array<BindingClassLimits, 3> per_stage;
        BindingClassLimits pipeline;
        std::array<DescriptorLimits, 3> per_stage_descriptors;
        DescriptorLimits pipeline_descriptors;
        std::uint32_t max_bound_descriptor_sets = 0;
        std::uint32_t max_bindings_per_set = 0;

        static TargetBindingLimits d3d11_sm5();
        static TargetBindingLimits d3d12_sm6();
        static TargetBindingLimits vulkan_portable_v1();
    };

    struct NativeBinding
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
        const ActiveBinding* logical_binding = nullptr;
    };

    struct TargetBindingLayout
    {
        ShaderTarget target = ShaderTarget::D3D11Dxbc;
        std::uint32_t mapping_version = 0;
        std::vector<NativeBinding> bindings;
        Sha256Hash target_binding_hash{};
    };

    struct TargetBindingResult
    {
        // optional publishes a target layout only when allocation completes
        // without collisions or profile-limit diagnostics.
        std::optional<TargetBindingLayout> layout;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    TargetBindingResult allocate_target_bindings(
        const ActiveShaderLayout& active_layout,
        ShaderTarget target,
        const TargetBindingLimits& limits);

    Sha256Hash calculate_target_binding_hash(const TargetBindingLayout& layout);
}
