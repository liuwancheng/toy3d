#include "codegen/shader_parameters_codegen.h"

#include "codegen/cpp_identifier.h"
#include "misc/sha256.h"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <string_view>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        // string_view keeps the fixed generated spellings allocation-free while
        // assembling one deterministic output buffer.
        const char* group_name(BindingGroup group)
        {
            switch (group)
            {
            case BindingGroup::Global:
                return "Global";
            case BindingGroup::View:
                return "View";
            case BindingGroup::Pass:
                return "Pass";
            case BindingGroup::Material:
                return "Material";
            case BindingGroup::Object:
                return "Object";
            }
            return "Global";
        }

        const char* value_type_name(ShaderValueType type)
        {
            switch (type)
            {
            case ShaderValueType::Float32:
                return "float";
            case ShaderValueType::Float32x2:
                return "Vector2";
            case ShaderValueType::Float32x3:
                return "Vector3";
            case ShaderValueType::Float32x4:
                return "Vector4";
            case ShaderValueType::Int32:
                return "std::int32_t";
            case ShaderValueType::Int32x2:
                return "std::array<std::int32_t, 2>";
            case ShaderValueType::Int32x3:
                return "std::array<std::int32_t, 3>";
            case ShaderValueType::Int32x4:
                return "std::array<std::int32_t, 4>";
            case ShaderValueType::UInt32:
                return "std::uint32_t";
            case ShaderValueType::UInt32x2:
                return "UIntVector2";
            case ShaderValueType::UInt32x3:
                return "UIntVector3";
            case ShaderValueType::UInt32x4:
                return "UIntVector4";
            case ShaderValueType::Float32x3x3:
                return "Matrix3";
            case ShaderValueType::Float32x4x4:
                return "Matrix4";
            case ShaderValueType::Float32x2x2:
                return "std::array<float, 4>";
            case ShaderValueType::Float32x2x3:
            case ShaderValueType::Float32x3x2:
                return "std::array<float, 6>";
            case ShaderValueType::Float32x2x4:
            case ShaderValueType::Float32x4x2:
                return "std::array<float, 8>";
            case ShaderValueType::Float32x3x4:
            case ShaderValueType::Float32x4x3:
                return "std::array<float, 12>";
            }
            return "float";
        }

        const char* value_type_enum_name(ShaderValueType type)
        {
            switch (type)
            {
            case ShaderValueType::Float32:
                return "Float32";
            case ShaderValueType::Float32x2:
                return "Float32x2";
            case ShaderValueType::Float32x3:
                return "Float32x3";
            case ShaderValueType::Float32x4:
                return "Float32x4";
            case ShaderValueType::Int32:
                return "Int32";
            case ShaderValueType::Int32x2:
                return "Int32x2";
            case ShaderValueType::Int32x3:
                return "Int32x3";
            case ShaderValueType::Int32x4:
                return "Int32x4";
            case ShaderValueType::UInt32:
                return "UInt32";
            case ShaderValueType::UInt32x2:
                return "UInt32x2";
            case ShaderValueType::UInt32x3:
                return "UInt32x3";
            case ShaderValueType::UInt32x4:
                return "UInt32x4";
            case ShaderValueType::Float32x2x2:
                return "Float32x2x2";
            case ShaderValueType::Float32x2x3:
                return "Float32x2x3";
            case ShaderValueType::Float32x2x4:
                return "Float32x2x4";
            case ShaderValueType::Float32x3x2:
                return "Float32x3x2";
            case ShaderValueType::Float32x3x3:
                return "Float32x3x3";
            case ShaderValueType::Float32x3x4:
                return "Float32x3x4";
            case ShaderValueType::Float32x4x2:
                return "Float32x4x2";
            case ShaderValueType::Float32x4x3:
                return "Float32x4x3";
            case ShaderValueType::Float32x4x4:
                return "Float32x4x4";
            }
            return "Float32";
        }

        const char* resource_cpp_type(ShaderParameterCategory category)
        {
            switch (category)
            {
            case ShaderParameterCategory::SampledTexture:
            case ShaderParameterCategory::StorageTexture:
                return "RHITextureViewRef";
            case ShaderParameterCategory::Sampler:
                return "RHISamplerRef";
            case ShaderParameterCategory::ReadOnlyBuffer:
            case ShaderParameterCategory::StorageBuffer:
                return "RHIBufferViewRef";
            case ShaderParameterCategory::Constant:
                break;
            }
            return "RHIResourceRef";
        }

        const char* category_name(ShaderParameterCategory category)
        {
            switch (category)
            {
            case ShaderParameterCategory::Constant:
                return "Constant";
            case ShaderParameterCategory::SampledTexture:
                return "SampledTexture";
            case ShaderParameterCategory::Sampler:
                return "Sampler";
            case ShaderParameterCategory::ReadOnlyBuffer:
                return "ReadOnlyBuffer";
            case ShaderParameterCategory::StorageBuffer:
                return "StorageBuffer";
            case ShaderParameterCategory::StorageTexture:
                return "StorageTexture";
            }
            return "Constant";
        }

        const char* resource_kind_name(ResourceKind kind)
        {
            switch (kind)
            {
            case ResourceKind::Texture2D:
                return "Texture2D";
            case ResourceKind::Texture2DArray:
                return "Texture2DArray";
            case ResourceKind::Texture3D:
                return "Texture3D";
            case ResourceKind::TextureCube:
                return "TextureCube";
            case ResourceKind::Texture2DMS:
                return "Texture2DMS";
            case ResourceKind::Sampler:
                return "Sampler";
            case ResourceKind::ComparisonSampler:
                return "ComparisonSampler";
            case ResourceKind::Buffer:
                return "Buffer";
            case ResourceKind::ByteAddressBuffer:
                return "ByteAddressBuffer";
            case ResourceKind::StructuredBuffer:
                return "StructuredBuffer";
            case ResourceKind::RWBuffer:
                return "RWBuffer";
            case ResourceKind::RWByteAddressBuffer:
                return "RWByteAddressBuffer";
            case ResourceKind::RWStructuredBuffer:
                return "RWStructuredBuffer";
            case ResourceKind::RWTexture2D:
                return "RWTexture2D";
            case ResourceKind::RWTexture2DArray:
                return "RWTexture2DArray";
            case ResourceKind::RWTexture3D:
                return "RWTexture3D";
            }
            return "Texture2D";
        }

        const char* element_type_name(ShaderResourceElementType type)
        {
            switch (type)
            {
            case ShaderResourceElementType::None:
                return "None";
            case ShaderResourceElementType::Float:
                return "Float";
            case ShaderResourceElementType::Float2:
                return "Float2";
            case ShaderResourceElementType::Float3:
                return "Float3";
            case ShaderResourceElementType::Float4:
                return "Float4";
            case ShaderResourceElementType::Int:
                return "Int";
            case ShaderResourceElementType::Int2:
                return "Int2";
            case ShaderResourceElementType::Int3:
                return "Int3";
            case ShaderResourceElementType::Int4:
                return "Int4";
            case ShaderResourceElementType::UInt:
                return "UInt";
            case ShaderResourceElementType::UInt2:
                return "UInt2";
            case ShaderResourceElementType::UInt3:
                return "UInt3";
            case ShaderResourceElementType::UInt4:
                return "UInt4";
            case ShaderResourceElementType::Float2x2:
                return "Float2x2";
            case ShaderResourceElementType::Float2x3:
                return "Float2x3";
            case ShaderResourceElementType::Float2x4:
                return "Float2x4";
            case ShaderResourceElementType::Float3x2:
                return "Float3x2";
            case ShaderResourceElementType::Float3x3:
                return "Float3x3";
            case ShaderResourceElementType::Float3x4:
                return "Float3x4";
            case ShaderResourceElementType::Float4x2:
                return "Float4x2";
            case ShaderResourceElementType::Float4x3:
                return "Float4x3";
            case ShaderResourceElementType::Float4x4:
                return "Float4x4";
            }
            return "None";
        }

        const char* default_value_kind_name(ShaderParameterDefaultValueKind kind)
        {
            switch (kind)
            {
            case ShaderParameterDefaultValueKind::None:
                return "None";
            case ShaderParameterDefaultValueKind::String:
                return "String";
            case ShaderParameterDefaultValueKind::Identifier:
                return "Identifier";
            }
            return "None";
        }

        std::string cpp_string_literal(std::string_view value)
        {
            std::string result = "\"";
            for (char character : value)
            {
                if (character == '\\' || character == '"')
                {
                    result += '\\';
                }
                if (character == '\n')
                {
                    result += "\\n";
                }
                else if (character == '\r')
                {
                    result += "\\r";
                }
                else if (character == '\t')
                {
                    result += "\\t";
                }
                else
                {
                    result += character;
                }
            }
            result += '"';
            return result;
        }

        std::string field_name(std::string_view name)
        {
            std::string result(name);
            std::transform(result.begin(), result.end(), result.begin(),
                           [](char value)
                           {
                               return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
                           });
            return result;
        }

        void append_hash(std::ostringstream& output, const Sha256Hash& hash)
        {
            output << "{{";
            for (std::size_t index = 0; index < hash.size(); ++index)
            {
                if (index != 0u)
                {
                    output << ", ";
                }
                output << static_cast<unsigned int>(hash[index]) << 'u';
            }
            output << "}}";
        }

        void append_preamble(std::ostringstream& output)
        {
            output << "#pragma once\n\n"
                      "#include \"drivers/rhi/rhi_resource.h\"\n"
                      "#include \"math/integer_vector.h\"\n"
                      "#include \"math/matrix3.h\"\n"
                      "#include \"math/matrix4.h\"\n"
                      "#include \"math/vector2.h\"\n"
                      "#include \"math/vector3.h\"\n"
                      "#include \"math/vector4.h\"\n"
                      "#include \"rendercore/shader/shader_parameters.h\"\n\n"
                      "#include <array>\n#include <cstdint>\n\n"
                      "namespace toy3d\n{\n";
        }

        void append_struct(std::ostringstream& output, std::string_view type_name,
                           const ShaderParameterConstantBufferSchema* buffer,
                           const std::vector<ShaderParameterResourceSchema>& resources)
        {
            output << "    struct " << type_name << "\n    {\n";
            if (buffer)
            {
                for (const ShaderParameterConstantMemberSchema& member : buffer->members)
                {
                    output << "        ";
                    if (member.array_count > 1u)
                    {
                        output << "std::array<" << value_type_name(member.type) << ", " << member.array_count << ">";
                    }
                    else
                    {
                        output << value_type_name(member.type);
                    }
                    output << ' ' << field_name(member.name);
                    if (member.array_count == 1u && member.type == ShaderValueType::Float32x3x3)
                    {
                        output << " = Matrix3::zero();\n";
                    }
                    else if (member.array_count == 1u && member.type == ShaderValueType::Float32x4x4)
                    {
                        output << " = Matrix4::zero();\n";
                    }
                    else
                    {
                        output << "{};\n";
                    }
                }
            }
            for (const ShaderParameterResourceSchema& resource : resources)
            {
                output << "        ";
                if (resource.array_count > 1u)
                {
                    output << "std::array<" << resource_cpp_type(resource.category) << ", " << resource.array_count
                           << ">";
                }
                else
                {
                    output << resource_cpp_type(resource.category);
                }
                output << ' ' << field_name(resource.name) << "{};\n";
            }
            output << "    };\n\n";
        }

        void append_metadata(std::ostringstream& output, std::string_view type_name, BindingGroup group,
                             const ShaderParameterSchema& schema, const ShaderParameterConstantBufferSchema* buffer,
                             const std::vector<ShaderParameterResourceSchema>& resources)
        {
            output << "    // Full schema identity: " << sha256_to_hex(schema.schema_identity) << "\n"
                   << "    // " << group_name(group)
                   << " group identity: " << sha256_to_hex(calculate_shader_parameter_group_identity(schema, group))
                   << "\n"
                   << "    inline const ShaderParametersMetadata& shader_parameters_metadata(const " << type_name
                   << "&)\n    {\n        static const ShaderParametersMetadata metadata = {\n"
                   << "            shader::BindingGroup::" << group_name(group) << ",\n            "
                   << schema.generated_format_version << "u,\n            " << schema.shader_abi_version
                   << "u,\n            " << schema.parameter_id_version << "u,\n            "
                   << shader_parameters_cpp_identifier_version << "u,\n            ";
            append_hash(output, schema.schema_identity);
            output << ",\n            ";
            append_hash(output, calculate_shader_parameter_group_identity(schema, group));
            output << ",\n            {";
            if (buffer)
            {
                output << buffer->binding_id << "ull, " << buffer->size << "u, ";
                append_hash(output, buffer->data_layout_hash);
                output << ", " << buffer->shader_abi_version << "u, {";
                for (std::size_t index = 0; index < buffer->members.size(); ++index)
                {
                    const ShaderParameterConstantMemberSchema& member = buffer->members[index];
                    if (index != 0u)
                    {
                        output << ", ";
                    }
                    output << '{' << member.parameter_id
                           << "ull, shader::ShaderValueType::" << value_type_enum_name(member.type) << ", "
                           << member.offset << "u, " << member.size << "u, " << member.array_count << "u, "
                           << member.array_stride << "u, " << member.matrix_stride << "u, {";
                    for (std::size_t byte_index = 0; byte_index < member.default_value.size(); ++byte_index)
                    {
                        if (byte_index != 0u)
                        {
                            output << ", ";
                        }
                        output << static_cast<unsigned int>(member.default_value[byte_index]) << 'u';
                    }
                    output << "}, " << cpp_string_literal(member.name) << '}';
                }
                output << "}, " << cpp_string_literal(buffer->name);
            }
            output << "},\n            {";
            for (std::size_t index = 0; index < resources.size(); ++index)
            {
                const ShaderParameterResourceSchema& resource = resources[index];
                if (index != 0u)
                {
                    output << ", ";
                }
                output << '{' << resource.parameter_id
                       << "ull, shader::ShaderParameterCategory::" << category_name(resource.category)
                       << ", shader::ResourceKind::" << resource_kind_name(resource.resource_kind)
                       << ", shader::ShaderResourceElementType::" << element_type_name(resource.element_type) << ", "
                       << resource.array_count << "u, shader::ShaderParameterDefaultValueKind::"
                       << default_value_kind_name(resource.default_value_kind) << ", "
                       << cpp_string_literal(resource.default_value) << ", " << cpp_string_literal(resource.name)
                       << '}';
            }
            output << "},\n            ";
            append_hash(output, group == BindingGroup::Material ? schema.editor_properties_hash : Sha256Hash{});
            output << "\n        };\n        return metadata;\n    }\n\n";
        }

        void append_encoder(std::ostringstream& output, std::string_view type_name,
                            const ShaderParameterConstantBufferSchema* buffer,
                            const std::vector<ShaderParameterResourceSchema>& resources)
        {
            output << "    inline void encode_shader_parameters(const " << type_name
                   << "& parameters, ShaderParameterEncoder& encoder)\n    {\n"
                      "        const ShaderParametersMetadata& metadata = shader_parameters_metadata(parameters);\n";
            std::size_t member_index = 0;
            if (buffer)
            {
                for (const ShaderParameterConstantMemberSchema& member : buffer->members)
                {
                    output << "        encoder.write_constant(metadata.constant_buffer.members[" << member_index
                           << "u], parameters." << field_name(member.name) << ");\n";
                    ++member_index;
                }
            }
            for (std::size_t index = 0; index < resources.size(); ++index)
            {
                output << "        encoder.add_resource(metadata.resources[" << index << "u], parameters."
                       << field_name(resources[index].name) << ");\n";
            }
            output << "    }\n\n";
        }

        const ShaderParameterConstantBufferSchema* find_buffer(const ShaderParameterSchema& schema, BindingGroup group)
        {
            const auto found = std::find_if(schema.constant_buffers.begin(), schema.constant_buffers.end(),
                                            [group](const ShaderParameterConstantBufferSchema& buffer)
                                            {
                                                return buffer.group == group;
                                            });
            return found == schema.constant_buffers.end() ? nullptr : &*found;
        }

        std::vector<ShaderParameterResourceSchema> group_resources(const ShaderParameterSchema& schema,
                                                                   BindingGroup group)
        {
            std::vector<ShaderParameterResourceSchema> result;
            std::copy_if(schema.resources.begin(), schema.resources.end(), std::back_inserter(result),
                         [group](const ShaderParameterResourceSchema& resource)
                         {
                             return resource.group == group;
                         });
            return result;
        }

        void append_group(std::ostringstream& output, std::string_view type_name, BindingGroup group,
                          const ShaderParameterSchema& schema)
        {
            const ShaderParameterConstantBufferSchema* buffer = find_buffer(schema, group);
            const std::vector<ShaderParameterResourceSchema> resources = group_resources(schema, group);
            append_struct(output, type_name, buffer, resources);
            append_metadata(output, type_name, group, schema, buffer, resources);
            append_encoder(output, type_name, buffer, resources);
        }

        ShaderParameterSchema make_builtin_schema(MeshVertexFactoryType factory = MeshVertexFactoryType::Local)
        {
            LogicalShaderLayout layout;
            for (BindingGroup group : {BindingGroup::Global, BindingGroup::View, BindingGroup::Object})
            {
                const ShaderParameterGroupInput input = builtin_shader_parameter_input(group);
                if (input.constant_members.empty())
                {
                    continue;
                }
                ConstantBufferPackResult packed = pack_constant_buffer(group, input.constant_members);
                if (packed.layout)
                {
                    layout.constant_buffers.push_back(std::move(*packed.layout));
                }
            }
            if (factory == MeshVertexFactoryType::GPUSkin)
            {
                layout.resources.push_back(builtin_gpu_skin_resource());
            }
            return make_shader_parameter_schema(layout);
        }
    } // namespace

    bool ShaderParametersCodegenResult::succeeded() const
    {
        return source.has_value() && diagnostics.empty();
    }

    ShaderParametersCodegenResult generate_shader_parameters_header(const ShaderAsset& asset,
                                                                    const LogicalShaderLayout& layout)
    {
        ShaderParametersCodegenResult result;
        const CppIdentifierMappingResult identifiers = map_shader_parameter_cpp_identifiers(asset);
        result.diagnostics = identifiers.diagnostics;
        if (!identifiers.succeeded())
        {
            return result;
        }

        const ShaderParameterSchema schema = make_shader_parameter_schema(layout);
        if (schema.schema_identity != layout.parameter_schema_hash ||
            schema.logical_layout_hash != layout.logical_layout_hash)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error,
                                          DiagnosticCode::InvalidCompileRequest,
                                          {},
                                          "Shader parameters codegen requires the canonical logical schema."});
            return result;
        }

        result.output_name = identifiers.identifiers->header_stem + ".generated.h";
        std::ostringstream output;
        append_preamble(output);
        for (const CppShaderPassIdentifiers& pass : identifiers.identifiers->passes)
        {
            append_group(output, pass.parameters_type, BindingGroup::Pass, schema);
        }
        output << "} // namespace toy3d\n";
        result.source = output.str();
        return result;
    }

    ShaderParametersCodegenResult generate_builtin_shader_parameters_header()
    {
        ShaderParametersCodegenResult result;
        result.output_name = "builtin_shader_parameters.generated.h";
        const ShaderParameterSchema schema = make_builtin_schema();
        std::ostringstream output;
        append_preamble(output);
        append_group(output, "GlobalShaderParameters", BindingGroup::Global, schema);
        append_group(output, "ViewShaderParameters", BindingGroup::View, schema);
        append_group(output, "ObjectShaderParameters", BindingGroup::Object, schema);
        append_group(output, "GPUSkinObjectShaderParameters", BindingGroup::Object,
                     make_builtin_schema(MeshVertexFactoryType::GPUSkin));
        output << "} // namespace toy3d\n";
        result.source = output.str();
        return result;
    }
} // namespace toy3d::shader
