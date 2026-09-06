#include "rendercore/shader/shader_vertex_input.h"

#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        std::string normalize_semantic(std::string semantic)
        {
            for (char& character : semantic)
            {
                if (character >= 'a' && character <= 'z')
                {
                    character = static_cast<char>(
                        character - ('a' - 'A'));
                }
            }
            return semantic;
        }

        std::string reflected_logical_semantic(
            const shader::ReflectedInterfaceVariable& reflected)
        {
            if (!reflected.semantic.empty())
            {
                return reflected.semantic;
            }

            const std::size_t name_separator = reflected.name.rfind('.');
            return name_separator == std::string::npos
                ? reflected.name
                : reflected.name.substr(name_separator + 1u);
        }

        bool resolve_attribute(
            const std::string& semantic,
            ShaderVertexAttributeId& attribute_id,
            std::string& semantic_name,
            std::uint32_t& component_count)
        {
            if (semantic == "POSITION" || semantic == "POSITION0")
            {
                attribute_id = ShaderVertexAttributeId::Position0;
                semantic_name = "POSITION";
                component_count = 4u;
                return true;
            }
            if (semantic == "NORMAL" || semantic == "NORMAL0")
            {
                attribute_id = ShaderVertexAttributeId::Normal0;
                semantic_name = "NORMAL";
                component_count = 4u;
                return true;
            }
            if (semantic == "TEXCOORD" || semantic == "TEXCOORD0")
            {
                attribute_id = ShaderVertexAttributeId::TexCoord0;
                semantic_name = "TEXCOORD";
                component_count = 2u;
                return true;
            }
            if (semantic == "COLOR" || semantic == "COLOR0")
            {
                attribute_id = ShaderVertexAttributeId::Color0;
                semantic_name = "COLOR";
                component_count = 4u;
                return true;
            }
            return false;
        }
    }

    bool try_make_shader_vertex_input(
        const shader::ReflectedInterfaceVariable& reflected,
        ShaderVertexInput& output,
        std::string& error)
    {
        if (!reflected.input ||
            reflected.location == std::numeric_limits<std::uint32_t>::max())
        {
            error = "Shader vertex input requires a valid target location.";
            return false;
        }

        ShaderVertexAttributeId attribute_id =
            ShaderVertexAttributeId::Position0;
        std::string semantic_name;
        std::uint32_t expected_component_count = 0;
        if (!resolve_attribute(
                normalize_semantic(reflected_logical_semantic(reflected)),
                attribute_id,
                semantic_name,
                expected_component_count))
        {
            error = "Shader vertex input uses an unsupported logical attribute.";
            return false;
        }
        const bool supported_position_shape =
            attribute_id == ShaderVertexAttributeId::Position0 &&
            (reflected.component_count == 2u ||
             reflected.component_count == expected_component_count);
        if (reflected.scalar_type !=
                shader::ReflectedInterfaceVariable::ScalarType::Float32 ||
            (!supported_position_shape &&
             reflected.component_count != expected_component_count))
        {
            error = "Shader vertex input uses an unsupported scalar/component shape.";
            return false;
        }

        ShaderVertexInput converted;
        converted.attribute_id = attribute_id;
        converted.semantic_name = std::move(semantic_name);
        converted.semantic_index = 0;
        converted.scalar_type = reflected.scalar_type;
        converted.component_count = reflected.component_count;
        converted.target_location = reflected.location;
        output = std::move(converted);
        error.clear();
        return true;
    }

    bool have_same_shader_vertex_input_contract(
        const ShaderVertexInput& left,
        const ShaderVertexInput& right)
    {
        // Cross-target parity compares logical identity and data shape; native
        // location belongs to each target mapping and is deliberately excluded.
        return left.attribute_id == right.attribute_id &&
            left.semantic_name == right.semantic_name &&
            left.semantic_index == right.semantic_index &&
            left.scalar_type == right.scalar_type &&
            left.component_count == right.component_count;
    }
}
