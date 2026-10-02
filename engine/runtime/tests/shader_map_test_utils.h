#pragma once

#include "rendercore/shader/shader_parameters.h"
#include "rendercore/shader/shader_map_program.h"

#include <algorithm>

namespace toy3d::tests
{
    inline void append_shader_parameters_metadata(const ShaderParametersMetadata& metadata,
                                                  shader::ShaderParameterSchema& schema)
    {
        if (metadata.constant_buffer.size != 0u)
        {
            shader::ShaderParameterConstantBufferSchema buffer;
            buffer.binding_id = metadata.constant_buffer.binding_id;
            buffer.name = metadata.constant_buffer.name;
            buffer.group = metadata.group;
            buffer.size = metadata.constant_buffer.size;
            buffer.data_layout_hash = metadata.constant_buffer.data_layout_hash;
            buffer.shader_abi_version = metadata.constant_buffer.shader_abi_version;
            for (const ShaderParameterConstantMemberMetadata& member : metadata.constant_buffer.members)
            {
                buffer.members.push_back({member.parameter_id, member.name, member.type, member.offset, member.size,
                                          member.array_count, member.array_stride, member.matrix_stride,
                                          member.default_value});
            }
            schema.constant_buffers.push_back(std::move(buffer));
        }
        for (const ShaderParameterResourceMetadata& resource : metadata.resources)
        {
            schema.resources.push_back({resource.parameter_id, resource.name, metadata.group, resource.category,
                                        resource.resource_kind, resource.element_type, resource.array_count,
                                        resource.default_value_kind, resource.default_value});
        }
    }

    inline shader::BindingGroup to_schema_group(RHIBindingGroup group)
    {
        switch (group)
        {
        case RHIBindingGroup::Global:
            return shader::BindingGroup::Global;
        case RHIBindingGroup::View:
            return shader::BindingGroup::View;
        case RHIBindingGroup::Pass:
            return shader::BindingGroup::Pass;
        case RHIBindingGroup::Material:
            return shader::BindingGroup::Material;
        case RHIBindingGroup::Object:
            return shader::BindingGroup::Object;
        case RHIBindingGroup::Max:
            break;
        }
        return shader::BindingGroup::Material;
    }

    inline shader::ShaderParameterCategory to_schema_category(RHIResourceBindingType type)
    {
        switch (type)
        {
        case RHIResourceBindingType::UniformBuffer:
            return shader::ShaderParameterCategory::Constant;
        case RHIResourceBindingType::SampledTexture:
            return shader::ShaderParameterCategory::SampledTexture;
        case RHIResourceBindingType::Sampler:
            return shader::ShaderParameterCategory::Sampler;
        case RHIResourceBindingType::ReadOnlyBuffer:
            return shader::ShaderParameterCategory::ReadOnlyBuffer;
        case RHIResourceBindingType::StorageBuffer:
            return shader::ShaderParameterCategory::StorageBuffer;
        }
        return shader::ShaderParameterCategory::Constant;
    }

    inline shader::ResourceKind to_schema_resource_kind(RHIResourceBindingType type)
    {
        switch (type)
        {
        case RHIResourceBindingType::SampledTexture:
            return shader::ResourceKind::Texture2D;
        case RHIResourceBindingType::Sampler:
            return shader::ResourceKind::Sampler;
        case RHIResourceBindingType::ReadOnlyBuffer:
            return shader::ResourceKind::Buffer;
        case RHIResourceBindingType::StorageBuffer:
            return shader::ResourceKind::RWBuffer;
        case RHIResourceBindingType::UniformBuffer:
            break;
        }
        return shader::ResourceKind::Buffer;
    }

    inline void finalize_test_program_parameter_schema(ShaderMapProgramData& program)
    {
        shader::ShaderParameterSchema schema;
        for (ShaderMapBinding& binding : program.bindings)
        {
            if (binding.type == RHIResourceBindingType::UniformBuffer)
            {
                shader::ShaderParameterConstantBufferSchema buffer;
                buffer.binding_id = binding.parameter_id;
                buffer.name = binding.name;
                buffer.group = to_schema_group(binding.group);
                buffer.size = binding.constant_buffer_size;
                std::vector<shader::ReflectedConstantMember> reflected_members;
                for (const ShaderMapBinding::ConstantMember& member : binding.constant_members)
                {
                    const std::uint32_t array_count =
                        member.array_stride == 0u ? 1u : std::max(member.size / member.array_stride, 1u);
                    buffer.members.push_back({member.parameter_id,
                                              member.name,
                                              static_cast<shader::ShaderValueType>(member.type),
                                              member.offset,
                                              member.size,
                                              array_count,
                                              member.array_stride,
                                              member.matrix_stride,
                                              {}});
                    reflected_members.push_back({member.parameter_id, member.name,
                                                 static_cast<shader::ShaderValueType>(member.type), member.offset,
                                                 member.size, member.array_stride, member.matrix_stride});
                }
                buffer.data_layout_hash = shader::calculate_constant_buffer_data_layout_hash(
                    buffer.group, buffer.binding_id, buffer.size, reflected_members);
                binding.data_layout_hash = buffer.data_layout_hash;
                binding.shader_abi_version = shader::toy_shader_abi_version;
                schema.constant_buffers.push_back(std::move(buffer));
            }
            else
            {
                shader::ShaderParameterResourceSchema resource;
                resource.parameter_id = binding.parameter_id;
                resource.name = binding.name;
                resource.group = to_schema_group(binding.group);
                resource.category = to_schema_category(binding.type);
                resource.resource_kind = to_schema_resource_kind(binding.type);
                if (binding.type != RHIResourceBindingType::Sampler)
                {
                    resource.element_type = shader::ShaderResourceElementType::Float4;
                }
                resource.array_count = binding.array_count;
                schema.resources.push_back(std::move(resource));
            }
        }
        schema.logical_layout_hash = shader::calculate_shader_parameter_logical_layout_hash(schema);
        schema.schema_identity = shader::calculate_shader_parameter_schema_identity(schema);
        program.parameter_schema = std::move(schema);
        program.logical_layout_hash = program.parameter_schema.logical_layout_hash;
        for (ShaderMapStage& stage : program.stages)
        {
            for (ShaderMapBinding& reflected : stage.reflection)
            {
                const auto binding = std::find_if(program.bindings.begin(), program.bindings.end(),
                                                  [&](const ShaderMapBinding& candidate)
                                                  {
                                                      return candidate.parameter_id == reflected.parameter_id;
                                                  });
                if (binding != program.bindings.end())
                {
                    reflected = *binding;
                }
            }
        }
    }
} // namespace toy3d::tests
