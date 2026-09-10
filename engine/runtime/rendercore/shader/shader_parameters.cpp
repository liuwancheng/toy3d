#include "rendercore/shader/shader_parameters.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/shader/shader_uniform_buffer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace toy3d
{
    namespace
    {
        constexpr std::uint32_t k_shader_scalar_byte_size = 4u;

        bool hash_is_zero(const shader::Sha256Hash& hash)
        {
            return std::all_of(hash.begin(), hash.end(), [](std::uint8_t byte) { return byte == 0u; });
        }

        bool is_matrix_type(shader::ShaderValueType type)
        {
            return type >= shader::ShaderValueType::Float32x2x2 &&
                   type <= shader::ShaderValueType::Float32x4x4;
        }

        bool resource_kind_matches_category(shader::ResourceKind kind,
                                            shader::ShaderParameterCategory category)
        {
            switch (kind)
            {
            case shader::ResourceKind::Texture2D:
            case shader::ResourceKind::Texture2DArray:
            case shader::ResourceKind::Texture3D:
            case shader::ResourceKind::TextureCube:
            case shader::ResourceKind::Texture2DMS:
                return category == shader::ShaderParameterCategory::SampledTexture;
            case shader::ResourceKind::Sampler:
            case shader::ResourceKind::ComparisonSampler:
                return category == shader::ShaderParameterCategory::Sampler;
            case shader::ResourceKind::Buffer:
            case shader::ResourceKind::ByteAddressBuffer:
            case shader::ResourceKind::StructuredBuffer:
                return category == shader::ShaderParameterCategory::ReadOnlyBuffer;
            case shader::ResourceKind::RWBuffer:
            case shader::ResourceKind::RWByteAddressBuffer:
            case shader::ResourceKind::RWStructuredBuffer:
                return category == shader::ShaderParameterCategory::StorageBuffer;
            case shader::ResourceKind::RWTexture2D:
            case shader::ResourceKind::RWTexture2DArray:
            case shader::ResourceKind::RWTexture3D:
                return category == shader::ShaderParameterCategory::StorageTexture;
            }
            return false;
        }

        bool resource_kind_requires_element_type(shader::ResourceKind kind)
        {
            return kind != shader::ResourceKind::Sampler && kind != shader::ResourceKind::ComparisonSampler &&
                   kind != shader::ResourceKind::ByteAddressBuffer &&
                   kind != shader::ResourceKind::RWByteAddressBuffer;
        }

        RHIStatus validate_metadata(const ShaderParametersMetadata& metadata)
        {
            if (metadata.group > shader::BindingGroup::Object ||
                metadata.generated_format_version != shader::shader_parameters_generated_format_version ||
                metadata.shader_abi_version != shader::toy_shader_abi_version ||
                metadata.parameter_id_version != shader::shader_parameter_id_version ||
                metadata.cpp_identifier_version != shader::shader_parameters_cpp_identifier_version ||
                hash_is_zero(metadata.schema_identity) ||
                hash_is_zero(metadata.group_identity))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Shader parameters metadata has an invalid group, version, or schema identity.");
            }

            std::set<shader::ShaderParameterId> parameter_ids;
            const ShaderParameterConstantBufferMetadata& buffer = metadata.constant_buffer;
            if (buffer.size == 0u)
            {
                if (buffer.binding_id != 0u || buffer.shader_abi_version != 0u ||
                    !hash_is_zero(buffer.data_layout_hash) || !buffer.members.empty())
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Empty Shader constant metadata contains an unexpected ABI declaration.");
                }
            }
            else
            {
                if (buffer.binding_id == 0u || buffer.name.empty() ||
                    buffer.size > shader::max_constant_buffer_size || buffer.members.empty() ||
                    buffer.shader_abi_version != metadata.shader_abi_version ||
                    hash_is_zero(buffer.data_layout_hash) || !parameter_ids.insert(buffer.binding_id).second)
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                              "Shader constant metadata has an invalid ABI declaration.");
                }
                for (const ShaderParameterConstantMemberMetadata& member : buffer.members)
                {
                    const bool array_shape_valid =
                        member.array_count == 1u
                            ? member.array_stride == 0u
                            : member.array_stride != 0u &&
                                  member.array_count <= std::numeric_limits<std::uint32_t>::max() /
                                                            member.array_stride &&
                                  member.size == member.array_count * member.array_stride;
                    if (member.parameter_id == 0u || member.name.empty() || member.size == 0u ||
                        member.array_count == 0u ||
                        member.type > shader::ShaderValueType::Float32x4x4 || !array_shape_valid ||
                        is_matrix_type(member.type) != (member.matrix_stride != 0u) ||
                        member.offset > buffer.size || member.size > buffer.size - member.offset ||
                        (!member.default_value.empty() && member.default_value.size() != member.size) ||
                        !parameter_ids.insert(member.parameter_id).second)
                    {
                        return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                                  "Shader constant metadata contains an invalid member shape.");
                    }
                }
            }

            for (const ShaderParameterResourceMetadata& resource : metadata.resources)
            {
                const bool default_shape_valid =
                    resource.default_value_kind == shader::ShaderParameterDefaultValueKind::None
                        ? resource.default_value.empty()
                        : !resource.default_value.empty();
                const bool element_shape_valid =
                    resource_kind_requires_element_type(resource.resource_kind)
                        ? resource.element_type != shader::ShaderResourceElementType::None
                        : resource.element_type == shader::ShaderResourceElementType::None;
                if (resource.parameter_id == 0u || resource.name.empty() || resource.array_count == 0u ||
                    resource.category == shader::ShaderParameterCategory::Constant ||
                    resource.category > shader::ShaderParameterCategory::StorageTexture ||
                    resource.resource_kind > shader::ResourceKind::RWTexture3D ||
                    resource.element_type > shader::ShaderResourceElementType::Float4x4 ||
                    resource.default_value_kind > shader::ShaderParameterDefaultValueKind::Identifier ||
                    !resource_kind_matches_category(resource.resource_kind, resource.category) ||
                    !element_shape_valid || !default_shape_valid ||
                    !parameter_ids.insert(resource.parameter_id).second)
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                              "Shader resource metadata contains an invalid declaration.");
                }
            }
            if (buffer.size == 0u && metadata.resources.empty())
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Shader parameters metadata does not declare any binding values.");
            }
            shader::ShaderParameterSchema group_schema;
            group_schema.generated_format_version = metadata.generated_format_version;
            group_schema.shader_abi_version = metadata.shader_abi_version;
            group_schema.parameter_id_version = metadata.parameter_id_version;
            if (buffer.size != 0u)
            {
                shader::ShaderParameterConstantBufferSchema schema_buffer;
                schema_buffer.binding_id = buffer.binding_id;
                schema_buffer.name = buffer.name;
                schema_buffer.group = metadata.group;
                schema_buffer.size = buffer.size;
                schema_buffer.data_layout_hash = buffer.data_layout_hash;
                schema_buffer.shader_abi_version = buffer.shader_abi_version;
                std::vector<shader::ReflectedConstantMember> reflected_members;
                for (const ShaderParameterConstantMemberMetadata& member : buffer.members)
                {
                    schema_buffer.members.push_back({member.parameter_id, member.name, member.type, member.offset,
                                                     member.size, member.array_count, member.array_stride,
                                                     member.matrix_stride, member.default_value});
                    reflected_members.push_back({member.parameter_id, member.name, member.type, member.offset,
                                                 member.size, member.array_stride, member.matrix_stride});
                }
                if (shader::calculate_constant_buffer_data_layout_hash(
                        metadata.group, buffer.binding_id, buffer.size, reflected_members,
                        buffer.shader_abi_version) != buffer.data_layout_hash)
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Shader parameters metadata constant ABI identity is inconsistent.");
                }
                group_schema.constant_buffers.push_back(std::move(schema_buffer));
            }
            for (const ShaderParameterResourceMetadata& resource : metadata.resources)
            {
                group_schema.resources.push_back(
                    {resource.parameter_id, resource.name, metadata.group, resource.category,
                     resource.resource_kind, resource.element_type, resource.array_count,
                     resource.default_value_kind, resource.default_value});
            }
            if (shader::calculate_shader_parameter_group_identity(group_schema, metadata.group) !=
                metadata.group_identity)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Shader parameters metadata group identity does not match its declarations.");
            }
            return RHIStatus::success();
        }

        RHIBindingGroup to_rhi_group(shader::BindingGroup group)
        {
            switch (group)
            {
            case shader::BindingGroup::Global: return RHIBindingGroup::Global;
            case shader::BindingGroup::View: return RHIBindingGroup::View;
            case shader::BindingGroup::Pass: return RHIBindingGroup::Pass;
            case shader::BindingGroup::Material: return RHIBindingGroup::Material;
            case shader::BindingGroup::Object: return RHIBindingGroup::Object;
            }
            return RHIBindingGroup::Max;
        }

        bool texture_kind_matches_view(shader::ResourceKind kind, RHITextureViewDimension dimension)
        {
            switch (kind)
            {
            case shader::ResourceKind::Texture2D:
            case shader::ResourceKind::RWTexture2D:
                return dimension == RHITextureViewDimension::Texture2D;
            case shader::ResourceKind::Texture2DArray:
            case shader::ResourceKind::RWTexture2DArray:
                return dimension == RHITextureViewDimension::Texture2DArray;
            case shader::ResourceKind::Texture3D:
            case shader::ResourceKind::RWTexture3D:
                return dimension == RHITextureViewDimension::Texture3D;
            case shader::ResourceKind::TextureCube:
                return dimension == RHITextureViewDimension::TextureCube;
            case shader::ResourceKind::Texture2DMS:
                return dimension == RHITextureViewDimension::Texture2DMS;
            default: return false;
            }
        }

        std::uint32_t structured_element_stride(shader::ShaderResourceElementType type)
        {
            switch (type)
            {
            case shader::ShaderResourceElementType::Float:
            case shader::ShaderResourceElementType::Int:
            case shader::ShaderResourceElementType::UInt: return 4u;
            case shader::ShaderResourceElementType::Float2:
            case shader::ShaderResourceElementType::Int2:
            case shader::ShaderResourceElementType::UInt2: return 8u;
            case shader::ShaderResourceElementType::Float3:
            case shader::ShaderResourceElementType::Float4:
            case shader::ShaderResourceElementType::Int3:
            case shader::ShaderResourceElementType::Int4:
            case shader::ShaderResourceElementType::UInt3:
            case shader::ShaderResourceElementType::UInt4: return 16u;
            case shader::ShaderResourceElementType::Float2x2:
            case shader::ShaderResourceElementType::Float3x2:
            case shader::ShaderResourceElementType::Float4x2: return 32u;
            case shader::ShaderResourceElementType::Float2x3:
            case shader::ShaderResourceElementType::Float3x3:
            case shader::ShaderResourceElementType::Float4x3: return 48u;
            case shader::ShaderResourceElementType::Float2x4:
            case shader::ShaderResourceElementType::Float3x4:
            case shader::ShaderResourceElementType::Float4x4: return 64u;
            case shader::ShaderResourceElementType::None: return 0u;
            }
            return 0u;
        }

        bool buffer_kind_matches_view(const EncodedShaderBufferValue& encoded)
        {
            if (!encoded.buffer_view || encoded.buffer || !encoded.buffer_view->buffer())
                return false;
            const RHIBufferViewDesc& view = encoded.buffer_view->desc();
            const RHIBufferDesc& buffer = encoded.buffer_view->buffer()->desc();
            const RHIResourceViewType expected_view_type =
                encoded.category == shader::ShaderParameterCategory::StorageBuffer
                    ? RHIResourceViewType::UnorderedAccess
                    : RHIResourceViewType::ShaderResource;
            if (view.type != expected_view_type)
                return false;
            switch (encoded.resource_kind)
            {
            case shader::ResourceKind::StructuredBuffer:
            case shader::ResourceKind::RWStructuredBuffer:
                return view.format == PixelFormat::Unknown && buffer.structure_stride != 0u &&
                       buffer.structure_stride == structured_element_stride(encoded.element_type);
            case shader::ResourceKind::ByteAddressBuffer:
            case shader::ResourceKind::RWByteAddressBuffer:
                return view.format == PixelFormat::Unknown && buffer.structure_stride == 0u;
            case shader::ResourceKind::Buffer:
            case shader::ResourceKind::RWBuffer:
                return view.format != PixelFormat::Unknown && buffer.structure_stride == 0u;
            default: return false;
            }
        }

        RHIStatus validate_encoded_resources(const ShaderParametersMetadata& metadata,
                                             const ShaderParameterEncoder& encoder)
        {
            std::size_t expected_texture_count = 0u;
            std::size_t expected_sampler_count = 0u;
            std::size_t expected_buffer_count = 0u;
            for (const ShaderParameterResourceMetadata& resource : metadata.resources)
            {
                if (resource.category == shader::ShaderParameterCategory::SampledTexture ||
                    resource.category == shader::ShaderParameterCategory::StorageTexture)
                {
                    if (resource.array_count > std::numeric_limits<std::size_t>::max() - expected_texture_count)
                        return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                                  "Shader texture array count exceeds host limits.");
                    expected_texture_count += resource.array_count;
                }
                else if (resource.category == shader::ShaderParameterCategory::Sampler)
                {
                    if (resource.array_count > std::numeric_limits<std::size_t>::max() - expected_sampler_count)
                        return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                                  "Shader sampler array count exceeds host limits.");
                    expected_sampler_count += resource.array_count;
                }
                else
                {
                    if (resource.array_count > std::numeric_limits<std::size_t>::max() - expected_buffer_count)
                        return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                                  "Shader buffer array count exceeds host limits.");
                    expected_buffer_count += resource.array_count;
                }

                for (std::uint32_t array_index = 0u; array_index < resource.array_count; ++array_index)
                {
                    if (resource.category == shader::ShaderParameterCategory::SampledTexture ||
                        resource.category == shader::ShaderParameterCategory::StorageTexture)
                    {
                        const auto found = std::find_if(
                            encoder.texture_values().begin(), encoder.texture_values().end(),
                            [&](const EncodedShaderTextureValue& value)
                            {
                                return value.parameter_id == resource.parameter_id &&
                                       value.array_index == array_index;
                            });
                        const RHIResourceViewType expected_view_type =
                            resource.category == shader::ShaderParameterCategory::StorageTexture
                                ? RHIResourceViewType::UnorderedAccess
                                : RHIResourceViewType::ShaderResource;
                        if (found == encoder.texture_values().end() || found->category != resource.category ||
                            found->resource_kind != resource.resource_kind || !found->value ||
                            found->value->desc().type != expected_view_type ||
                            !texture_kind_matches_view(resource.resource_kind, found->value->desc().dimension))
                        {
                            return RHIStatus::failure(
                                RHIErrorCode::InvalidArgument,
                                "Shader texture resource is missing or does not match its metadata and device.");
                        }
                    }
                    else if (resource.category == shader::ShaderParameterCategory::Sampler)
                    {
                        const auto found = std::find_if(
                            encoder.sampler_values().begin(), encoder.sampler_values().end(),
                            [&](const EncodedShaderSamplerValue& value)
                            {
                                return value.parameter_id == resource.parameter_id &&
                                       value.array_index == array_index;
                            });
                        const bool comparison = resource.resource_kind == shader::ResourceKind::ComparisonSampler;
                        if (found == encoder.sampler_values().end() ||
                            found->resource_kind != resource.resource_kind || !found->value ||
                            found->value->desc().compare_enable != comparison)
                        {
                            return RHIStatus::failure(
                                RHIErrorCode::InvalidArgument,
                                "Shader sampler resource is missing or does not match its metadata and device.");
                        }
                    }
                    else
                    {
                        const auto found = std::find_if(
                            encoder.buffer_values().begin(), encoder.buffer_values().end(),
                            [&](const EncodedShaderBufferValue& value)
                            {
                                return value.parameter_id == resource.parameter_id &&
                                       value.array_index == array_index;
                            });
                        if (found == encoder.buffer_values().end() || found->category != resource.category ||
                            found->resource_kind != resource.resource_kind ||
                            found->element_type != resource.element_type || !buffer_kind_matches_view(*found))
                        {
                            return RHIStatus::failure(
                                RHIErrorCode::InvalidArgument,
                                "Shader buffer resource is missing or does not match its metadata and device.");
                        }
                    }
                }
            }
            if (encoder.texture_values().size() != expected_texture_count ||
                encoder.sampler_values().size() != expected_sampler_count ||
                encoder.buffer_values().size() != expected_buffer_count)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Encoded Shader resources do not exactly match the metadata arrays.");
            }
            return RHIStatus::success();
        }

        RHIStatus validate_resource_owners(RHIDevice& device, RHICommandContext& context,
                                           const ShaderParameterEncoder& encoder)
        {
            if (!context.is_owned_by(device))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Typed Shader binding context must belong to the supplied RHI device.");
            }
            for (const EncodedShaderTextureValue& encoded : encoder.texture_values())
            {
                if (!encoded.value->is_owned_by(device))
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Every typed Shader texture must belong to the supplied RHI device.");
                }
            }
            for (const EncodedShaderSamplerValue& encoded : encoder.sampler_values())
            {
                if (!encoded.value->is_owned_by(device))
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Every typed Shader sampler must belong to the supplied RHI device.");
                }
            }
            for (const EncodedShaderBufferValue& encoded : encoder.buffer_values())
            {
                if (!encoded.buffer_view->is_owned_by(device))
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Every typed Shader buffer must belong to the supplied RHI device.");
                }
            }
            return RHIStatus::success();
        }

        RHIResult<RHIBindingSetRef> create_encoded_binding_set(
            RHIDevice& device, const ShaderParametersMetadata& metadata,
            const ShaderParameterEncoder& encoder, const RHIBufferRef& uniform_buffer,
            std::uint64_t uniform_offset, const std::string& debug_name)
        {
            RHIBindingSetDesc binding_desc;
            binding_desc.group = to_rhi_group(metadata.group);
            binding_desc.debug_name = debug_name;
            if (metadata.constant_buffer.size != 0u)
            {
                RHIBindingValue uniform_value;
                uniform_value.binding_id = metadata.constant_buffer.binding_id;
                uniform_value.buffer = uniform_buffer;
                uniform_value.buffer_offset = uniform_offset;
                uniform_value.buffer_size = metadata.constant_buffer.size;
                uniform_value.data_layout_hash = metadata.constant_buffer.data_layout_hash;
                uniform_value.shader_abi_version = metadata.constant_buffer.shader_abi_version;
                binding_desc.bindings.push_back(std::move(uniform_value));
            }
            for (const EncodedShaderTextureValue& encoded : encoder.texture_values())
            {
                RHIBindingValue value;
                value.binding_id = encoded.parameter_id;
                value.array_index = encoded.array_index;
                value.texture_view = encoded.value;
                binding_desc.bindings.push_back(std::move(value));
            }
            for (const EncodedShaderSamplerValue& encoded : encoder.sampler_values())
            {
                RHIBindingValue value;
                value.binding_id = encoded.parameter_id;
                value.array_index = encoded.array_index;
                value.sampler = encoded.value;
                binding_desc.bindings.push_back(std::move(value));
            }
            for (const EncodedShaderBufferValue& encoded : encoder.buffer_values())
            {
                RHIBindingValue value;
                value.binding_id = encoded.parameter_id;
                value.array_index = encoded.array_index;
                value.buffer_view = encoded.buffer_view;
                binding_desc.bindings.push_back(std::move(value));
            }
            return device.create_binding_set(binding_desc);
        }

        RHIStatus validate_encoded_shader_binding(
            RHIDevice& device, RHICommandContext& context,
            const ShaderParametersMetadata& metadata, const ShaderParameterEncoder& encoder)
        {
            const RHIStatus metadata_status = validate_metadata(metadata);
            if (!metadata_status)
                return metadata_status;
            if (!encoder.succeeded())
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, encoder.error());
            if (!encoder.matches_metadata_identity(metadata))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Shader parameter encoder metadata identity changed before binding creation.");
            }
            if (encoder.constant_bytes().size() != metadata.constant_buffer.size)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Encoded Shader constant bytes do not match the metadata buffer size.");
            }

            const RHIStatus resource_status = validate_encoded_resources(metadata, encoder);
            if (!resource_status)
                return resource_status;
            return validate_resource_owners(device, context, encoder);
        }
    } // namespace

    RHIStatus validate_shader_parameters_metadata(const ShaderParametersMetadata& metadata)
    {
        return validate_metadata(metadata);
    }

    RHIStatus validate_shader_parameters_metadata_against_schema(
        const ShaderParametersMetadata& metadata, const shader::ShaderParameterSchema& schema)
    {
        const RHIStatus metadata_status = validate_metadata(metadata);
        if (!metadata_status)
            return metadata_status;
        std::string schema_error;
        if (!shader::validate_shader_parameter_schema(schema, schema_error))
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, std::move(schema_error));
        if (metadata.generated_format_version != schema.generated_format_version ||
            metadata.shader_abi_version != schema.shader_abi_version ||
            metadata.parameter_id_version != schema.parameter_id_version ||
            metadata.schema_identity != schema.schema_identity ||
            metadata.group_identity !=
                shader::calculate_shader_parameter_group_identity(schema, metadata.group))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Generated Shader parameters metadata does not match the complete Shader schema identity.");
        }
        return RHIStatus::success();
    }

    ShaderParameterEncoder::ShaderParameterEncoder(const ShaderParametersMetadata& metadata_value)
        : metadata(metadata_value), source_schema_identity(metadata_value.schema_identity),
          source_group_identity(metadata_value.group_identity),
          source_data_layout_hash(metadata_value.constant_buffer.data_layout_hash),
          source_group(metadata_value.group), source_shader_abi_version(metadata_value.shader_abi_version)
    {
        if (metadata_value.constant_buffer.size > shader::max_constant_buffer_size)
        {
            encoder_error = "Shader constant metadata exceeds the portable maximum buffer size";
            return;
        }
        encoded_constant_bytes.assign(metadata_value.constant_buffer.size, 0u);
    }

    RHIResult<RHIBindingSetRef> create_transient_shader_binding(
        RHIDevice& device, RHICommandContext& context, const ShaderParametersMetadata& metadata,
        const ShaderParameterEncoder& encoder)
    {
        const RHIStatus validation_status = validate_encoded_shader_binding(device, context, metadata, encoder);
        if (!validation_status)
            return RHIResult<RHIBindingSetRef>::failure(validation_status.code(), validation_status.message());

        RHIBufferRef uniform_buffer;
        std::uint64_t uniform_offset = 0u;
        if (metadata.constant_buffer.size != 0u)
        {
            RHITransientUniformDataDesc upload_desc;
            upload_desc.source.data = encoder.constant_bytes().data();
            upload_desc.source.size = encoder.constant_bytes().size();
            upload_desc.source.consumption = RHIInitialData::Consumption::CopiedBeforeReturn;
            upload_desc.debug_name = "Typed shader parameters";
            RHIResult<RHIUniformBufferSlice> upload = context.upload_transient_uniform_data(upload_desc);
            if (!upload)
                return RHIResult<RHIBindingSetRef>::failure(upload.status().code(), upload.status().message());
            uniform_buffer = upload.value().buffer;
            uniform_offset = upload.value().offset;
        }
        return create_encoded_binding_set(device, metadata, encoder, uniform_buffer, uniform_offset,
                                          "Typed shader parameters");
    }

    RHIResult<RHIBindingSetRef> create_persistent_shader_binding(
        RHIDevice& device, RHICommandContext& context, const ShaderParametersMetadata& metadata,
        const ShaderParameterEncoder& encoder, const std::string& debug_name)
    {
        const RHIStatus validation_status = validate_encoded_shader_binding(device, context, metadata, encoder);
        if (!validation_status)
            return RHIResult<RHIBindingSetRef>::failure(validation_status.code(), validation_status.message());

        RHIBufferRef uniform_buffer;
        if (metadata.constant_buffer.size != 0u)
        {
            RHIResult<RHIBufferRef> upload = create_uploaded_shader_uniform_buffer(
                device, context, encoder.constant_bytes(), debug_name + " Constants");
            if (!upload)
                return RHIResult<RHIBindingSetRef>::failure(upload.status().code(), upload.status().message());
            uniform_buffer = std::move(upload).value();
        }
        return create_encoded_binding_set(device, metadata, encoder, uniform_buffer, 0u, debug_name);
    }

    void ShaderParameterEncoder::write_scalar_values(const ShaderParameterConstantMemberMetadata& member,
                                                      shader::ShaderValueType expected_type, const void* values,
                                                      std::uint32_t value_count, std::uint32_t value_size)
    {
        if (!encoder_error.empty())
            return;
        if (value_size != 0u && value_count > std::numeric_limits<std::uint32_t>::max() / value_size)
        {
            fail("Shader scalar or vector metadata byte size overflows");
            return;
        }
        const std::uint32_t byte_size = value_count * value_size;
        if (member.type != expected_type || member.array_count != 1u || member.array_stride != 0u ||
            member.matrix_stride != 0u || member.size < byte_size ||
            member.offset > encoded_constant_bytes.size() ||
            byte_size > encoded_constant_bytes.size() - member.offset)
        {
            fail("Shader constant metadata does not match the generated scalar or vector field");
            return;
        }
        std::memcpy(encoded_constant_bytes.data() + member.offset, values, byte_size);
    }

    void ShaderParameterEncoder::write_matrix_values(const ShaderParameterConstantMemberMetadata& member,
                                                      shader::ShaderValueType expected_type, const float* values,
                                                      std::uint32_t row_count, std::uint32_t column_count)
    {
        if (!encoder_error.empty())
            return;
        if (row_count > std::numeric_limits<std::uint32_t>::max() / k_shader_scalar_byte_size ||
            (column_count != 0u &&
             member.matrix_stride > std::numeric_limits<std::uint32_t>::max() / column_count))
        {
            fail("Shader matrix metadata byte size overflows");
            return;
        }
        const std::uint32_t column_byte_size = row_count * k_shader_scalar_byte_size;
        const std::uint32_t matrix_byte_size = member.matrix_stride * column_count;
        if (member.type != expected_type || member.array_count != 1u || member.array_stride != 0u ||
            member.matrix_stride < column_byte_size || member.size < matrix_byte_size ||
            member.offset > encoded_constant_bytes.size() ||
            matrix_byte_size > encoded_constant_bytes.size() - member.offset)
        {
            fail("Shader constant metadata does not match the generated matrix field");
            return;
        }

        for (std::uint32_t column = 0; column < column_count; ++column)
        {
            std::memcpy(encoded_constant_bytes.data() + member.offset + column * member.matrix_stride,
                        values + column * row_count, column_byte_size);
        }
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member, float value)
    {
        write_scalar_values(member, shader::ShaderValueType::Float32, &value, 1u, sizeof(value));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const Vector2& value)
    {
        const float values[] = {value.x, value.y};
        write_scalar_values(member, shader::ShaderValueType::Float32x2, values, 2u, sizeof(float));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const Vector3& value)
    {
        const float values[] = {value.x, value.y, value.z};
        write_scalar_values(member, shader::ShaderValueType::Float32x3, values, 3u, sizeof(float));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const Vector4& value)
    {
        const float values[] = {value.x, value.y, value.z, value.w};
        write_scalar_values(member, shader::ShaderValueType::Float32x4, values, 4u, sizeof(float));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                std::int32_t value)
    {
        write_scalar_values(member, shader::ShaderValueType::Int32, &value, 1u, sizeof(value));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const std::array<std::int32_t, 2>& value)
    {
        // std::array represents both fixed Shader arrays and the generated
        // integer-vector fields, so metadata selects the canonical path.
        if (member.type == shader::ShaderValueType::Int32)
        {
            write_constant_array(member, value);
            return;
        }
        write_scalar_values(member, shader::ShaderValueType::Int32x2, value.data(), 2u, sizeof(std::int32_t));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const std::array<std::int32_t, 3>& value)
    {
        if (member.type == shader::ShaderValueType::Int32)
        {
            write_constant_array(member, value);
            return;
        }
        write_scalar_values(member, shader::ShaderValueType::Int32x3, value.data(), 3u, sizeof(std::int32_t));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const std::array<std::int32_t, 4>& value)
    {
        if (member.type == shader::ShaderValueType::Int32)
        {
            write_constant_array(member, value);
            return;
        }
        write_scalar_values(member, shader::ShaderValueType::Int32x4, value.data(), 4u, sizeof(std::int32_t));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                std::uint32_t value)
    {
        write_scalar_values(member, shader::ShaderValueType::UInt32, &value, 1u, sizeof(value));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const UIntVector2& value)
    {
        const std::uint32_t values[] = {value.x, value.y};
        write_scalar_values(member, shader::ShaderValueType::UInt32x2, values, 2u, sizeof(std::uint32_t));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const UIntVector3& value)
    {
        const std::uint32_t values[] = {value.x, value.y, value.z};
        write_scalar_values(member, shader::ShaderValueType::UInt32x3, values, 3u, sizeof(std::uint32_t));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const UIntVector4& value)
    {
        const std::uint32_t values[] = {value.x, value.y, value.z, value.w};
        write_scalar_values(member, shader::ShaderValueType::UInt32x4, values, 4u, sizeof(std::uint32_t));
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const Matrix3& value)
    {
        write_matrix_values(member, shader::ShaderValueType::Float32x3x3, value.data(), 3u, 3u);
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const Matrix4& value)
    {
        write_matrix_values(member, shader::ShaderValueType::Float32x4x4, value.data(), 4u, 4u);
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const std::array<float, 4>& value)
    {
        // std::array is also the generated representation for non-square
        // matrices; metadata distinguishes those fields from scalar arrays.
        if (member.type == shader::ShaderValueType::Float32)
        {
            write_constant_array(member, value);
            return;
        }
        write_matrix_values(member, shader::ShaderValueType::Float32x2x2, value.data(), 2u, 2u);
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const std::array<float, 6>& value)
    {
        if (member.type == shader::ShaderValueType::Float32)
        {
            write_constant_array(member, value);
            return;
        }
        const shader::ShaderValueType expected_type = member.type == shader::ShaderValueType::Float32x2x3
                                                          ? shader::ShaderValueType::Float32x2x3
                                                          : shader::ShaderValueType::Float32x3x2;
        const std::uint32_t row_count = expected_type == shader::ShaderValueType::Float32x2x3 ? 2u : 3u;
        const std::uint32_t column_count = expected_type == shader::ShaderValueType::Float32x2x3 ? 3u : 2u;
        write_matrix_values(member, expected_type, value.data(), row_count, column_count);
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const std::array<float, 8>& value)
    {
        if (member.type == shader::ShaderValueType::Float32)
        {
            write_constant_array(member, value);
            return;
        }
        const shader::ShaderValueType expected_type = member.type == shader::ShaderValueType::Float32x2x4
                                                          ? shader::ShaderValueType::Float32x2x4
                                                          : shader::ShaderValueType::Float32x4x2;
        const std::uint32_t row_count = expected_type == shader::ShaderValueType::Float32x2x4 ? 2u : 4u;
        const std::uint32_t column_count = expected_type == shader::ShaderValueType::Float32x2x4 ? 4u : 2u;
        write_matrix_values(member, expected_type, value.data(), row_count, column_count);
    }

    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const std::array<float, 12>& value)
    {
        if (member.type == shader::ShaderValueType::Float32)
            return write_constant_array(member, value);
        const shader::ShaderValueType expected_type = member.type == shader::ShaderValueType::Float32x3x4
                                                          ? shader::ShaderValueType::Float32x3x4
                                                          : shader::ShaderValueType::Float32x4x3;
        const std::uint32_t row_count = expected_type == shader::ShaderValueType::Float32x3x4 ? 3u : 4u;
        const std::uint32_t column_count = expected_type == shader::ShaderValueType::Float32x3x4 ? 4u : 3u;
        write_matrix_values(member, expected_type, value.data(), row_count, column_count);
    }

    void ShaderParameterEncoder::add_resource(const ShaderParameterResourceMetadata& resource,
                                              const RHITextureViewRef& value)
    {
        if (!encoder_error.empty())
            return;
        if (resource.array_count != 1u ||
            (resource.category != shader::ShaderParameterCategory::SampledTexture &&
             resource.category != shader::ShaderParameterCategory::StorageTexture))
        {
            fail("Shader texture metadata does not match the generated resource field");
            return;
        }
        encoded_texture_values.push_back(
            {resource.parameter_id, 0u, resource.category, resource.resource_kind, value});
    }

    void ShaderParameterEncoder::add_resource(const ShaderParameterResourceMetadata& resource,
                                              const RHISamplerRef& value)
    {
        if (!encoder_error.empty())
            return;
        if (resource.array_count != 1u || resource.category != shader::ShaderParameterCategory::Sampler)
        {
            fail("Shader sampler metadata does not match the generated resource field");
            return;
        }
        encoded_sampler_values.push_back({resource.parameter_id, 0u, resource.resource_kind, value});
    }

    void ShaderParameterEncoder::add_resource(const ShaderParameterResourceMetadata& resource,
                                              const RHIBufferRef& value)
    {
        if (!encoder_error.empty())
            return;
        if (resource.array_count != 1u ||
            (resource.category != shader::ShaderParameterCategory::ReadOnlyBuffer &&
             resource.category != shader::ShaderParameterCategory::StorageBuffer))
        {
            fail("Shader buffer metadata does not match the generated resource field");
            return;
        }
        encoded_buffer_values.push_back({resource.parameter_id, 0u, resource.category, resource.resource_kind,
                                         resource.element_type, value, {}});
    }

    void ShaderParameterEncoder::add_resource(const ShaderParameterResourceMetadata& resource,
                                              const RHIBufferViewRef& value)
    {
        if (!encoder_error.empty())
            return;
        if (resource.array_count != 1u ||
            (resource.category != shader::ShaderParameterCategory::ReadOnlyBuffer &&
             resource.category != shader::ShaderParameterCategory::StorageBuffer))
        {
            fail("Shader buffer metadata does not match the generated resource field");
            return;
        }
        encoded_buffer_values.push_back({resource.parameter_id, 0u, resource.category, resource.resource_kind,
                                         resource.element_type, {}, value});
    }

    const std::vector<std::uint8_t>& ShaderParameterEncoder::constant_bytes() const
    {
        return encoded_constant_bytes;
    }

    const std::vector<EncodedShaderTextureValue>& ShaderParameterEncoder::texture_values() const
    {
        return encoded_texture_values;
    }

    const std::vector<EncodedShaderSamplerValue>& ShaderParameterEncoder::sampler_values() const
    {
        return encoded_sampler_values;
    }

    const std::vector<EncodedShaderBufferValue>& ShaderParameterEncoder::buffer_values() const
    {
        return encoded_buffer_values;
    }

    bool ShaderParameterEncoder::succeeded() const
    {
        return encoder_error.empty();
    }

    const std::string& ShaderParameterEncoder::error() const
    {
        return encoder_error;
    }

    bool ShaderParameterEncoder::matches_metadata_identity(const ShaderParametersMetadata& metadata_value) const
    {
        return source_schema_identity == metadata_value.schema_identity &&
               source_group_identity == metadata_value.group_identity &&
               source_data_layout_hash == metadata_value.constant_buffer.data_layout_hash &&
               source_group == metadata_value.group &&
               source_shader_abi_version == metadata_value.shader_abi_version;
    }

    void ShaderParameterEncoder::fail(std::string message)
    {
        if (encoder_error.empty())
            encoder_error = std::move(message);
    }
} // namespace toy3d
