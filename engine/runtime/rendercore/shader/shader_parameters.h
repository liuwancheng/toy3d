#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "shader/shader_format_types.h"
#include "math/integer_vector.h"
#include "math/matrix3.h"
#include "math/matrix4.h"
#include "math/vector2.h"
#include "math/vector3.h"
#include "math/vector4.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace toy3d
{
    class RHICommandContext;

    struct ShaderParameterConstantMemberMetadata
    {
        shader::ShaderParameterId parameter_id = 0;
        shader::ShaderValueType type = shader::ShaderValueType::Float32;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t array_count = 1;
        std::uint32_t array_stride = 0;
        std::uint32_t matrix_stride = 0;
        std::vector<std::uint8_t> default_value;
        std::string name;
    };

    struct ShaderParameterConstantBufferMetadata
    {
        shader::ShaderParameterId binding_id = 0;
        std::uint32_t size = 0;
        shader::ShaderDataLayoutHash data_layout_hash{};
        std::uint32_t shader_abi_version = 0;
        std::vector<ShaderParameterConstantMemberMetadata> members;
        std::string name;
    };

    struct ShaderParameterResourceMetadata
    {
        shader::ShaderParameterId parameter_id = 0;
        shader::ShaderParameterCategory category = shader::ShaderParameterCategory::SampledTexture;
        shader::ResourceKind resource_kind = shader::ResourceKind::Texture2D;
        shader::ShaderResourceElementType element_type = shader::ShaderResourceElementType::None;
        std::uint32_t array_count = 1;
        shader::ShaderParameterDefaultValueKind default_value_kind = shader::ShaderParameterDefaultValueKind::None;
        std::string default_value;
        std::string name;
    };

    struct ShaderParametersMetadata
    {
        shader::BindingGroup group = shader::BindingGroup::Global;
        std::uint32_t generated_format_version = 0;
        std::uint32_t shader_abi_version = 0;
        std::uint32_t parameter_id_version = 0;
        std::uint32_t cpp_identifier_version = 0;
        Sha256Hash schema_identity{};
        Sha256Hash group_identity{};
        ShaderParameterConstantBufferMetadata constant_buffer;
        std::vector<ShaderParameterResourceMetadata> resources;
        Sha256Hash editor_properties_hash{};
    };

    struct EncodedShaderTextureValue
    {
        shader::ShaderParameterId parameter_id = 0;
        std::uint32_t array_index = 0;
        shader::ShaderParameterCategory category = shader::ShaderParameterCategory::SampledTexture;
        shader::ResourceKind resource_kind = shader::ResourceKind::Texture2D;
        RHITextureViewRef value;
    };

    struct EncodedShaderSamplerValue
    {
        shader::ShaderParameterId parameter_id = 0;
        std::uint32_t array_index = 0;
        shader::ResourceKind resource_kind = shader::ResourceKind::Sampler;
        RHISamplerRef value;
    };

    struct EncodedShaderBufferValue
    {
        shader::ShaderParameterId parameter_id = 0;
        std::uint32_t array_index = 0;
        shader::ShaderParameterCategory category = shader::ShaderParameterCategory::ReadOnlyBuffer;
        shader::ResourceKind resource_kind = shader::ResourceKind::Buffer;
        shader::ShaderResourceElementType element_type = shader::ShaderResourceElementType::None;
        RHIBufferRef buffer;
        RHIBufferViewRef buffer_view;
    };

    // Generated overloads call this internal encoder surface directly. Its
    // implementation remains in RenderCore; generated headers only perform
    // explicit field reads and typed overload selection.
    class ShaderParameterEncoder
    {
      public:
        explicit ShaderParameterEncoder(const ShaderParametersMetadata& metadata);
        ShaderParameterEncoder(ShaderParametersMetadata&&) = delete;

        void write_constant(const ShaderParameterConstantMemberMetadata& member, float value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const Vector2& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const Vector3& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const Vector4& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, std::int32_t value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member,
                            const std::array<std::int32_t, 2>& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member,
                            const std::array<std::int32_t, 3>& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member,
                            const std::array<std::int32_t, 4>& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, std::uint32_t value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const UIntVector2& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const UIntVector3& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const UIntVector4& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const Matrix3& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const Matrix4& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const std::array<float, 4>& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const std::array<float, 6>& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const std::array<float, 8>& value);
        void write_constant(const ShaderParameterConstantMemberMetadata& member, const std::array<float, 12>& value);

        void add_resource(const ShaderParameterResourceMetadata& resource, const RHITextureViewRef& value);
        void add_resource(const ShaderParameterResourceMetadata& resource, const RHISamplerRef& value);
        void add_resource(const ShaderParameterResourceMetadata& resource, const RHIBufferRef& value);
        void add_resource(const ShaderParameterResourceMetadata& resource, const RHIBufferViewRef& value);

        const std::vector<std::uint8_t>& constant_bytes() const;
        const std::vector<EncodedShaderTextureValue>& texture_values() const;
        const std::vector<EncodedShaderSamplerValue>& sampler_values() const;
        const std::vector<EncodedShaderBufferValue>& buffer_values() const;
        bool succeeded() const;
        const std::string& error() const;
        bool matches_metadata_identity(const ShaderParametersMetadata& metadata) const;

        // These direct array overloads preserve the generated field's element
        // type and fixed count for the canonical array-stride encoder path.
        template <typename Value, std::size_t Count>
        void write_constant(const ShaderParameterConstantMemberMetadata& member,
                            const std::array<Value, Count>& values);

        template <typename Value, std::size_t Count>
        void add_resource(const ShaderParameterResourceMetadata& resource, const std::array<Value, Count>& values);

      private:
        template <typename Value, std::size_t Count>
        void write_constant_array(const ShaderParameterConstantMemberMetadata& member,
                                  const std::array<Value, Count>& values);

        void write_scalar_values(const ShaderParameterConstantMemberMetadata& member,
                                 shader::ShaderValueType expected_type, const void* values, std::uint32_t value_count,
                                 std::uint32_t value_size);
        void write_matrix_values(const ShaderParameterConstantMemberMetadata& member,
                                 shader::ShaderValueType expected_type, const float* values, std::uint32_t row_count,
                                 std::uint32_t column_count);
        void fail(std::string message);

        const ShaderParametersMetadata& metadata;
        std::vector<std::uint8_t> encoded_constant_bytes;
        std::vector<EncodedShaderTextureValue> encoded_texture_values;
        std::vector<EncodedShaderSamplerValue> encoded_sampler_values;
        std::vector<EncodedShaderBufferValue> encoded_buffer_values;
        std::string encoder_error;
        Sha256Hash source_schema_identity{};
        Sha256Hash source_group_identity{};
        shader::ShaderDataLayoutHash source_data_layout_hash{};
        shader::BindingGroup source_group = shader::BindingGroup::Global;
        std::uint32_t source_shader_abi_version = 0u;
    };

    RHIStatus validate_shader_parameters_metadata(const ShaderParametersMetadata& metadata);
    // Engine-owned groups can be shared across sources with different Material/other groups.
    RHIStatus validate_shader_parameters_group_against_schema(const ShaderParametersMetadata& metadata,
                                                              const shader::ShaderParameterSchema& schema);
    RHIStatus validate_shader_parameters_metadata_against_schema(const ShaderParametersMetadata& metadata,
                                                                 const shader::ShaderParameterSchema& schema);

    template <typename Value, std::size_t Count>
    void ShaderParameterEncoder::write_constant(const ShaderParameterConstantMemberMetadata& member,
                                                const std::array<Value, Count>& values)
    {
        write_constant_array(member, values);
    }

    template <typename Value, std::size_t Count>
    void ShaderParameterEncoder::write_constant_array(const ShaderParameterConstantMemberMetadata& member,
                                                      const std::array<Value, Count>& values)
    {
        constexpr std::size_t k_max_metadata_count = std::numeric_limits<std::uint32_t>::max();
        if (Count > k_max_metadata_count || member.array_count != Count || member.array_stride == 0u ||
            member.array_count > std::numeric_limits<std::uint32_t>::max() / member.array_stride ||
            member.size != member.array_count * member.array_stride || member.offset > encoded_constant_bytes.size() ||
            member.size > encoded_constant_bytes.size() - member.offset)
        {
            fail("Shader constant array metadata does not describe a bounded canonical range");
            return;
        }

        for (std::size_t index = 0; index < Count; ++index)
        {
            ShaderParameterConstantMemberMetadata element = member;
            element.offset += static_cast<std::uint32_t>(index) * member.array_stride;
            element.size = member.array_stride;
            element.array_count = 1u;
            element.array_stride = 0u;
            write_constant(element, values[index]);
        }
    }

    template <typename Value, std::size_t Count>
    void ShaderParameterEncoder::add_resource(const ShaderParameterResourceMetadata& resource,
                                              const std::array<Value, Count>& values)
    {
        constexpr std::size_t k_max_metadata_count = std::numeric_limits<std::uint32_t>::max();
        if (Count > k_max_metadata_count || resource.array_count != Count)
        {
            fail("Shader resource array metadata does not match the generated field count");
            return;
        }

        for (std::size_t index = 0; index < Count; ++index)
        {
            ShaderParameterResourceMetadata element = resource;
            element.array_count = 1u;
            const std::size_t texture_count = encoded_texture_values.size();
            const std::size_t sampler_count = encoded_sampler_values.size();
            const std::size_t buffer_count = encoded_buffer_values.size();
            add_resource(element, values[index]);
            if (encoded_texture_values.size() != texture_count)
            {
                encoded_texture_values.back().array_index = static_cast<std::uint32_t>(index);
            }
            if (encoded_sampler_values.size() != sampler_count)
            {
                encoded_sampler_values.back().array_index = static_cast<std::uint32_t>(index);
            }
            if (encoded_buffer_values.size() != buffer_count)
            {
                encoded_buffer_values.back().array_index = static_cast<std::uint32_t>(index);
            }
        }
    }

    RHIResult<RHIBindingSetRef> create_transient_shader_binding(RHIDevice& device, RHICommandContext& context,
                                                                const ShaderParametersMetadata& metadata,
                                                                const ShaderParameterEncoder& encoder);

    RHIResult<RHIBindingSetRef> create_persistent_shader_binding(RHIDevice& device, RHICommandContext& context,
                                                                 const ShaderParametersMetadata& metadata,
                                                                 const ShaderParameterEncoder& encoder,
                                                                 const std::string& debug_name);

    template <typename Parameters>
    RHIResult<RHIBindingSetRef> create_transient_shader_binding(RHIDevice& device, RHICommandContext& context,
                                                                const Parameters& parameters)
    {
        // The generated ordinary overloads participate through dependent
        // unqualified lookup, keeping each parameters type directly callable
        // without traits, adapters, or a runtime registry.
        const ShaderParametersMetadata& metadata = shader_parameters_metadata(parameters);
        ShaderParameterEncoder encoder(metadata);
        encode_shader_parameters(parameters, encoder);
        return create_transient_shader_binding(device, context, metadata, encoder);
    }
} // namespace toy3d
