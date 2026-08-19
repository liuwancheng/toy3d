#pragma once

#include "rendercore/shader/shader_map_program.h"

#include <variant>

namespace toy3d
{
    struct ShaderConstantBinding
    {
        RHIBindingGroup group = RHIBindingGroup::Material;
        std::uint32_t constant_buffer_binding = 0;
        ShaderValueType value_type = ShaderValueType::Float32;
        std::uint32_t constant_buffer_size = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t array_stride = 0;
        std::uint32_t matrix_stride = 0;
    };

    struct ShaderResourceBinding
    {
        RHIBindingGroup group = RHIBindingGroup::Material;
        RHIResourceBindingType resource_type = RHIResourceBindingType::SampledTexture;
        std::uint32_t target_binding = 0;
        std::uint32_t array_count = 1;
    };

    // A reflected parameter is exactly one binding category. variant preserves
    // that closed choice without a nullable base pointer or manual type tag.
    using ShaderParameterBinding = std::variant<ShaderConstantBinding, ShaderResourceBinding>;
}
