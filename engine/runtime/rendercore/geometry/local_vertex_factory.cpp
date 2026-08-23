#include "rendercore/geometry/local_vertex_factory.h"

#include <algorithm>
#include <set>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool expected_stream_format(
            ShaderVertexAttributeId attribute_id,
            PixelFormat& format,
            std::uint32_t& byte_size)
        {
            switch (attribute_id)
            {
            case ShaderVertexAttributeId::Position0:
            case ShaderVertexAttributeId::Normal0:
                format = PixelFormat::R32G32B32Float;
                byte_size = 12u;
                return true;
            case ShaderVertexAttributeId::TexCoord0:
                format = PixelFormat::R32G32Float;
                byte_size = 8u;
                return true;
            case ShaderVertexAttributeId::Color0:
                format = PixelFormat::R8G8B8A8UNorm;
                byte_size = 4u;
                return true;
            }
            return false;
        }

        bool expected_shader_shape(
            ShaderVertexAttributeId attribute_id,
            shader::ReflectedInterfaceVariable::ScalarType& scalar_type,
            std::uint32_t& component_count)
        {
            scalar_type = shader::ReflectedInterfaceVariable::ScalarType::Float32;
            switch (attribute_id)
            {
            case ShaderVertexAttributeId::Position0:
            case ShaderVertexAttributeId::Normal0:
                component_count = 3u;
                return true;
            case ShaderVertexAttributeId::TexCoord0:
                component_count = 2u;
                return true;
            case ShaderVertexAttributeId::Color0:
                component_count = 4u;
                return true;
            }
            return false;
        }
    }

    LocalVertexFactory::LocalVertexFactory(
        std::vector<VertexStreamComponent> components)
        : stream_components(std::move(components))
    {
    }

    RHIStatus LocalVertexFactory::build_vertex_input(
        const std::vector<ShaderVertexInput>& shader_inputs,
        std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>& vertex_layouts,
        std::vector<RHIGraphicsPipelineDesc::VertexAttribute>& vertex_attributes,
        std::vector<RHIVertexBufferBinding>& vertex_bindings) const
    {
        vertex_layouts.clear();
        vertex_attributes.clear();
        vertex_bindings.clear();
        std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout> built_layouts;
        std::vector<RHIGraphicsPipelineDesc::VertexAttribute> built_attributes;
        std::vector<RHIVertexBufferBinding> built_bindings;

        std::set<ShaderVertexAttributeId> available_attributes;
        std::set<std::uint32_t> available_streams;
        for (const VertexStreamComponent& component : stream_components)
        {
            const RHIBufferRef buffer = component.buffer.lock();
            PixelFormat expected_format = PixelFormat::Unknown;
            std::uint32_t byte_size = 0u;
            if (!buffer ||
                !rhi_has_any_flag(buffer->desc().usage, RHIResourceUsage::VertexBuffer) ||
                !expected_stream_format(
                    component.attribute_id, expected_format, byte_size) ||
                component.format != expected_format || component.stride == 0u ||
                component.byte_offset > component.stride ||
                byte_size > component.stride - component.byte_offset ||
                buffer->desc().size < component.stride ||
                component.byte_offset > buffer->desc().size ||
                byte_size > buffer->desc().size - component.byte_offset)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "LocalVertexFactory contains an invalid buffer, range, stride, or fixed stream format.");
            }
            if (!available_attributes.insert(component.attribute_id).second)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "LocalVertexFactory logical attributes must be unique.");
            }
            if (!available_streams.insert(component.stream_index).second)
            {
                for (const VertexStreamComponent& existing : stream_components)
                {
                    if (&existing == &component ||
                        existing.stream_index != component.stream_index)
                    {
                        continue;
                    }
                    if (existing.buffer.lock() != buffer ||
                        existing.stride != component.stride)
                    {
                        return RHIStatus::failure(
                            RHIErrorCode::InvalidArgument,
                            "Components sharing a vertex stream must reference one buffer and stride.");
                    }
                    break;
                }
            }
        }

        const ShaderVertexAttributeId required_attributes[] = {
            ShaderVertexAttributeId::Position0,
            ShaderVertexAttributeId::Normal0,
            ShaderVertexAttributeId::TexCoord0};
        for (ShaderVertexAttributeId attribute_id : required_attributes)
        {
            if (available_attributes.find(attribute_id) == available_attributes.end())
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "LocalVertexFactory requires POSITION0, NORMAL0, and TEXCOORD0 streams.");
            }
        }

        std::set<ShaderVertexAttributeId> matched_attributes;
        std::vector<const VertexStreamComponent*> matched_components;
        for (const ShaderVertexInput& shader_input : shader_inputs)
        {
            if (!matched_attributes.insert(shader_input.attribute_id).second)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Shader vertex inputs must have unique logical attributes.");
            }
            const auto component = std::find_if(
                stream_components.begin(), stream_components.end(),
                [&](const VertexStreamComponent& candidate)
                {
                    return candidate.attribute_id == shader_input.attribute_id;
                });
            shader::ReflectedInterfaceVariable::ScalarType expected_scalar_type =
                shader::ReflectedInterfaceVariable::ScalarType::Float32;
            std::uint32_t expected_component_count = 0u;
            if (component == stream_components.end() ||
                !expected_shader_shape(shader_input.attribute_id,
                    expected_scalar_type, expected_component_count) ||
                shader_input.scalar_type != expected_scalar_type ||
                shader_input.component_count != expected_component_count)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "LocalVertexFactory cannot match the Shader vertex-input logical attribute or data shape.");
            }
            matched_components.push_back(&*component);
            built_attributes.push_back({shader_input.target_location,
                component->stream_index, component->format,
                component->byte_offset});
        }

        std::sort(matched_components.begin(), matched_components.end(),
            [](const VertexStreamComponent* left, const VertexStreamComponent* right)
            {
                return left->stream_index < right->stream_index;
            });
        std::uint32_t previous_stream = 0u;
        bool have_previous_stream = false;
        for (const VertexStreamComponent* component : matched_components)
        {
            if (have_previous_stream && component->stream_index == previous_stream)
            {
                continue;
            }
            const RHIBufferRef buffer = component->buffer.lock();
            if (!buffer)
            {
                return RHIStatus::failure(
                    RHIErrorCode::NotReady,
                    "LocalVertexFactory vertex buffer expired while building bindings.");
            }
            built_layouts.push_back({component->stream_index,
                component->stride, RHIVertexInputRate::PerVertex});
            built_bindings.push_back({buffer, 0u, component->stride});
            previous_stream = component->stream_index;
            have_previous_stream = true;
        }
        std::sort(built_attributes.begin(), built_attributes.end(),
            [](const RHIGraphicsPipelineDesc::VertexAttribute& left,
               const RHIGraphicsPipelineDesc::VertexAttribute& right)
            {
                return left.location < right.location;
            });
        vertex_layouts = std::move(built_layouts);
        vertex_attributes = std::move(built_attributes);
        vertex_bindings = std::move(built_bindings);
        return RHIStatus::success();
    }
}
