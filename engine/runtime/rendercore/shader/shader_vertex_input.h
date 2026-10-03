#pragma once

#include "shader/shader_format_types.h"

#include <cstdint>
#include <string>

namespace toy3d
{
    enum class ShaderVertexAttributeId
    {
        Position0,
        Normal0,
        TexCoord0,
        Color0,
        BlendIndices0,
        BlendWeights0,
        BlendIndices1,
        BlendWeights1,
        Tangent0
    };

    // Immutable RenderCore value derived from verified vertex-stage reflection.
    // It keeps logical identity/shape separate from the current target location.
    struct ShaderVertexInput
    {
        ShaderVertexAttributeId attribute_id = ShaderVertexAttributeId::Position0;
        std::string semantic_name;
        std::uint32_t semantic_index = 0;
        shader::ReflectedInterfaceVariable::ScalarType scalar_type =
            shader::ReflectedInterfaceVariable::ScalarType::Float32;
        std::uint32_t component_count = 0;
        std::uint32_t target_location = 0;
    };

    bool try_make_shader_vertex_input(const shader::ReflectedInterfaceVariable& reflected, ShaderVertexInput& output,
                                      std::string& error);

    bool have_same_shader_vertex_input_contract(const ShaderVertexInput& left, const ShaderVertexInput& right);
} // namespace toy3d
