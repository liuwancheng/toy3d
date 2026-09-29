#include "format/shader_map_entry.h"

#include <algorithm>
#include <set>
#include <type_traits>
#include <vector>

namespace toy3d::shader
{
    namespace
    {
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
            return "Invalid";
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
            return "Invalid";
        }

        template <typename T> void append_integer(std::vector<std::uint8_t>& bytes, T value)
        {
            using Unsigned = std::make_unsigned_t<T>;
            const Unsigned converted = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                bytes.push_back(static_cast<std::uint8_t>(converted >> (index * 8u)));
            }
        }

        template <typename T> void append_enum(std::vector<std::uint8_t>& bytes, T value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value));
        }

        // string_view allows stable hashing of owned strings and reflected
        // names through one byte-serialization path without temporary copies.
        void append_string(std::vector<std::uint8_t>& bytes, std::string_view value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        std::vector<const ShaderCodeEntry*> sorted_stages(const ShaderMapEntry& entry)
        {
            std::vector<const ShaderCodeEntry*> stages;
            for (const ShaderCodeEntry& stage : entry.stages)
            {
                stages.push_back(&stage);
            }
            std::sort(stages.begin(), stages.end(),
                      [](const auto* left, const auto* right)
                      {
                          return static_cast<std::uint32_t>(left->request.stage) <
                                 static_cast<std::uint32_t>(right->request.stage);
                      });
            return stages;
        }

        template <typename Enum> bool enum_at_most(Enum value, Enum maximum)
        {
            return static_cast<std::uint32_t>(value) <= static_cast<std::uint32_t>(maximum);
        }

        void append_stencil_face(std::vector<std::uint8_t>& bytes,
                                 const ShaderGraphicsPassState::StencilFaceState& state)
        {
            append_enum(bytes, state.compare_operation);
            append_enum(bytes, state.fail_operation);
            append_enum(bytes, state.depth_fail_operation);
            append_enum(bytes, state.pass_operation);
        }

        bool same_stencil_face(const ShaderGraphicsPassState::StencilFaceState& left,
                               const ShaderGraphicsPassState::StencilFaceState& right)
        {
            return left.compare_operation == right.compare_operation && left.fail_operation == right.fail_operation &&
                   left.depth_fail_operation == right.depth_fail_operation &&
                   left.pass_operation == right.pass_operation;
        }
    } // namespace

    ShaderParameterId make_shader_parameter_id(BindingGroup group, ShaderParameterCategory category,
                                               std::string_view name)
    {
        std::vector<std::uint8_t> identity;
        append_string(identity, group_name(group));
        append_string(identity, category_name(category));
        append_string(identity, name);
        ShaderParameterId value = 14695981039346656037ull;
        for (std::uint8_t byte : identity)
        {
            value ^= byte;
            value *= 1099511628211ull;
        }
        return value;
    }

    ShaderDataLayoutHash calculate_constant_buffer_data_layout_hash(
        BindingGroup group, ShaderParameterId buffer_binding_id, std::uint32_t data_size,
        const std::vector<ReflectedConstantMember>& members, std::uint32_t abi_version)
    {
        std::vector<const ReflectedConstantMember*> canonical_members;
        canonical_members.reserve(members.size());
        for (const ReflectedConstantMember& member : members)
        {
            canonical_members.push_back(&member);
        }
        std::sort(canonical_members.begin(), canonical_members.end(),
                  [](const ReflectedConstantMember* left, const ReflectedConstantMember* right)
                  { return left->parameter_id < right->parameter_id; });

        std::vector<std::uint8_t> bytes;
        append_integer(bytes, abi_version);
        append_enum(bytes, group);
        append_integer(bytes, buffer_binding_id);
        append_integer(bytes, data_size);
        append_integer(bytes, static_cast<std::uint32_t>(canonical_members.size()));
        for (const ReflectedConstantMember* member : canonical_members)
        {
            append_integer(bytes, member->parameter_id);
            append_enum(bytes, member->type);
            append_integer(bytes, member->offset);
            append_integer(bytes, member->size);
            append_integer(bytes, member->array_stride);
            append_integer(bytes, member->matrix_stride);
        }
        return sha256(bytes);
    }

    namespace
    {
        void append_shader_parameter_schema(std::vector<std::uint8_t>& bytes, const ShaderParameterSchema& schema,
                                            bool include_defaults)
        {
            if (include_defaults)
            {
                append_integer(bytes, schema.generated_format_version);
                bytes.insert(bytes.end(), schema.editor_properties_hash.begin(), schema.editor_properties_hash.end());
            }
            append_integer(bytes, schema.shader_abi_version);
            append_integer(bytes, schema.parameter_id_version);
            append_integer(bytes, static_cast<std::uint32_t>(schema.constant_buffers.size()));
            for (const ShaderParameterConstantBufferSchema& buffer : schema.constant_buffers)
            {
                append_enum(bytes, buffer.group);
                if (include_defaults)
                {
                    append_integer(bytes, buffer.binding_id);
                    append_string(bytes, buffer.name);
                    bytes.insert(bytes.end(), buffer.data_layout_hash.begin(), buffer.data_layout_hash.end());
                    append_integer(bytes, buffer.shader_abi_version);
                }
                append_integer(bytes, buffer.size);
                append_integer(bytes, static_cast<std::uint32_t>(buffer.members.size()));
                for (const ShaderParameterConstantMemberSchema& member : buffer.members)
                {
                    append_integer(bytes, member.parameter_id);
                    append_string(bytes, member.name);
                    append_enum(bytes, member.type);
                    append_integer(bytes, member.offset);
                    append_integer(bytes, member.size);
                    append_integer(bytes, member.array_count);
                    append_integer(bytes, member.array_stride);
                    append_integer(bytes, member.matrix_stride);
                    if (include_defaults)
                    {
                        append_integer(bytes, static_cast<std::uint32_t>(member.default_value.size()));
                        bytes.insert(bytes.end(), member.default_value.begin(), member.default_value.end());
                    }
                }
            }
            append_integer(bytes, static_cast<std::uint32_t>(schema.resources.size()));
            for (const ShaderParameterResourceSchema& resource : schema.resources)
            {
                append_integer(bytes, resource.parameter_id);
                append_string(bytes, resource.name);
                append_enum(bytes, resource.group);
                append_enum(bytes, resource.category);
                append_enum(bytes, resource.resource_kind);
                append_enum(bytes, resource.element_type);
                append_integer(bytes, resource.array_count);
                if (include_defaults)
                {
                    append_enum(bytes, resource.default_value_kind);
                    append_string(bytes, resource.default_value);
                }
            }
        }

        const ShaderParameterConstantBufferSchema* find_schema_buffer(const ShaderParameterSchema& schema,
                                                                       ShaderParameterId id)
        {
            const auto found = std::find_if(schema.constant_buffers.begin(), schema.constant_buffers.end(),
                                            [&](const ShaderParameterConstantBufferSchema& buffer)
                                            { return buffer.binding_id == id; });
            return found == schema.constant_buffers.end() ? nullptr : &*found;
        }

        const ShaderParameterResourceSchema* find_schema_resource(const ShaderParameterSchema& schema,
                                                                   ShaderParameterId id)
        {
            const auto found = std::find_if(schema.resources.begin(), schema.resources.end(),
                                            [&](const ShaderParameterResourceSchema& resource)
                                            { return resource.parameter_id == id; });
            return found == schema.resources.end() ? nullptr : &*found;
        }

        bool resource_kind_matches_category(ResourceKind kind, ShaderParameterCategory category)
        {
            switch (kind)
            {
            case ResourceKind::Texture2D:
            case ResourceKind::Texture2DArray:
            case ResourceKind::Texture3D:
            case ResourceKind::TextureCube:
            case ResourceKind::Texture2DMS:
                return category == ShaderParameterCategory::SampledTexture;
            case ResourceKind::Sampler:
            case ResourceKind::ComparisonSampler:
                return category == ShaderParameterCategory::Sampler;
            case ResourceKind::Buffer:
            case ResourceKind::ByteAddressBuffer:
            case ResourceKind::StructuredBuffer:
                return category == ShaderParameterCategory::ReadOnlyBuffer;
            case ResourceKind::RWBuffer:
            case ResourceKind::RWByteAddressBuffer:
            case ResourceKind::RWStructuredBuffer:
                return category == ShaderParameterCategory::StorageBuffer;
            case ResourceKind::RWTexture2D:
            case ResourceKind::RWTexture2DArray:
            case ResourceKind::RWTexture3D:
                return category == ShaderParameterCategory::StorageTexture;
            }
            return false;
        }
    } // namespace

    Sha256Hash calculate_shader_parameter_schema_identity(const ShaderParameterSchema& schema)
    {
        std::vector<std::uint8_t> bytes;
        append_shader_parameter_schema(bytes, schema, true);
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_parameter_logical_layout_hash(const ShaderParameterSchema& schema)
    {
        std::vector<std::uint8_t> bytes;
        append_shader_parameter_schema(bytes, schema, false);
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_parameter_group_identity(const ShaderParameterSchema& schema, BindingGroup group)
    {
        ShaderParameterSchema group_schema;
        group_schema.generated_format_version = schema.generated_format_version;
        group_schema.shader_abi_version = schema.shader_abi_version;
        group_schema.parameter_id_version = schema.parameter_id_version;
        if (group == BindingGroup::Material)
            group_schema.editor_properties_hash = schema.editor_properties_hash;
        for (const ShaderParameterConstantBufferSchema& buffer : schema.constant_buffers)
        {
            if (buffer.group == group)
                group_schema.constant_buffers.push_back(buffer);
        }
        for (const ShaderParameterResourceSchema& resource : schema.resources)
        {
            if (resource.group == group)
                group_schema.resources.push_back(resource);
        }
        std::vector<std::uint8_t> bytes;
        append_enum(bytes, group);
        append_shader_parameter_schema(bytes, group_schema, true);
        return sha256(bytes);
    }

    bool validate_shader_parameter_schema(const ShaderParameterSchema& schema, std::string& error)
    {
        if (schema.generated_format_version != shader_parameters_generated_format_version ||
            schema.shader_abi_version != toy_shader_abi_version ||
            schema.parameter_id_version != shader_parameter_id_version ||
            schema.schema_identity != calculate_shader_parameter_schema_identity(schema) ||
            schema.logical_layout_hash != calculate_shader_parameter_logical_layout_hash(schema))
        {
            error = "Shader parameter schema has an unsupported version or mismatched identity.";
            return false;
        }
        std::set<ShaderParameterId> identities;
        std::set<BindingGroup> constant_groups;
        for (const ShaderParameterConstantBufferSchema& buffer : schema.constant_buffers)
        {
            if (buffer.binding_id == 0u || buffer.name.empty() || buffer.size == 0u ||
                buffer.shader_abi_version != schema.shader_abi_version || buffer.members.empty() ||
                !identities.insert(buffer.binding_id).second || !constant_groups.insert(buffer.group).second)
            {
                error = "Shader parameter schema contains an invalid constant buffer.";
                return false;
            }
            std::vector<ReflectedConstantMember> reflected_members;
            for (const ShaderParameterConstantMemberSchema& member : buffer.members)
            {
                if (member.parameter_id == 0u || member.name.empty() || member.size == 0u || member.array_count == 0u ||
                    member.offset > buffer.size || member.size > buffer.size - member.offset ||
                    (!member.default_value.empty() && member.default_value.size() != member.size) ||
                    !identities.insert(member.parameter_id).second)
                {
                    error = "Shader parameter schema contains an invalid constant member.";
                    return false;
                }
                reflected_members.push_back({member.parameter_id, member.name, member.type, member.offset, member.size,
                                             member.array_stride, member.matrix_stride});
            }
            if (calculate_constant_buffer_data_layout_hash(buffer.group, buffer.binding_id, buffer.size,
                                                           reflected_members, buffer.shader_abi_version) !=
                buffer.data_layout_hash)
            {
                error = "Shader parameter schema constant ABI identity is inconsistent.";
                return false;
            }
        }
        for (const ShaderParameterResourceSchema& resource : schema.resources)
        {
            if (resource.parameter_id == 0u || resource.name.empty() || resource.array_count == 0u ||
                resource.category == ShaderParameterCategory::Constant ||
                !resource_kind_matches_category(resource.resource_kind, resource.category) ||
                static_cast<std::uint32_t>(resource.default_value_kind) >
                    static_cast<std::uint32_t>(ShaderParameterDefaultValueKind::Identifier) ||
                (resource.default_value_kind == ShaderParameterDefaultValueKind::None &&
                 !resource.default_value.empty()) ||
                (resource.default_value_kind != ShaderParameterDefaultValueKind::None &&
                 resource.default_value.empty()) ||
                !identities.insert(resource.parameter_id).second)
            {
                error = "Shader parameter schema contains an invalid resource.";
                return false;
            }
        }
        return true;
    }

    bool validate_reflected_bindings_are_schema_subset(const ShaderParameterSchema& schema,
                                                       const std::vector<ReflectedBinding>& bindings,
                                                       std::string& error)
    {
        for (const ReflectedBinding& binding : bindings)
        {
            if (binding.category == ShaderParameterCategory::Constant)
            {
                const ShaderParameterConstantBufferSchema* buffer = find_schema_buffer(schema, binding.parameter_id);
                if (buffer == nullptr || buffer->name != binding.name || buffer->group != binding.group ||
                    buffer->size != binding.constant_buffer_size ||
                    buffer->data_layout_hash != binding.data_layout_hash ||
                    buffer->shader_abi_version != binding.shader_abi_version ||
                    buffer->members.size() != binding.constant_members.size())
                {
                    error = "Program reflected constant binding is not part of its complete parameter schema.";
                    return false;
                }
                for (std::size_t index = 0; index < buffer->members.size(); ++index)
                {
                    const ShaderParameterConstantMemberSchema& expected = buffer->members[index];
                    const ReflectedConstantMember& actual = binding.constant_members[index];
                    if (expected.parameter_id != actual.parameter_id || expected.name != actual.name ||
                        expected.type != actual.type || expected.offset != actual.offset || expected.size != actual.size ||
                        expected.array_stride != actual.array_stride ||
                        expected.matrix_stride != actual.matrix_stride)
                    {
                        error = "Program reflected constant layout differs from its complete parameter schema.";
                        return false;
                    }
                }
            }
            else
            {
                const ShaderParameterResourceSchema* resource = find_schema_resource(schema, binding.parameter_id);
                if (resource == nullptr || resource->name != binding.name || resource->group != binding.group ||
                    resource->category != binding.category || !binding.resource_kind ||
                    resource->resource_kind != *binding.resource_kind || resource->array_count != binding.array_count)
                {
                    error = "Program reflected resource binding is not part of its complete parameter schema.";
                    return false;
                }
            }
        }
        return true;
    }

    bool validate_active_bindings_are_schema_subset(const ShaderParameterSchema& schema,
                                                    const std::vector<ShaderMapBinding>& bindings,
                                                    std::string& error)
    {
        for (const ShaderMapBinding& binding : bindings)
        {
            if (binding.category == ShaderParameterCategory::Constant)
            {
                const ShaderParameterConstantBufferSchema* buffer = find_schema_buffer(schema, binding.binding_id);
                if (buffer == nullptr || buffer->name != binding.name || buffer->group != binding.group ||
                    buffer->size != binding.data_size || buffer->data_layout_hash != binding.data_layout_hash ||
                    buffer->shader_abi_version != binding.shader_abi_version)
                {
                    error = "Program active constant binding is not part of its complete parameter schema.";
                    return false;
                }
            }
            else
            {
                const ShaderParameterResourceSchema* resource = find_schema_resource(schema, binding.binding_id);
                if (resource == nullptr || resource->name != binding.name || resource->group != binding.group ||
                    resource->category != binding.category)
                {
                    error = "Program active resource binding is not part of its complete parameter schema.";
                    return false;
                }
            }
        }
        return true;
    }

    ShaderStageFlags operator|(ShaderStageFlags left, ShaderStageFlags right)
    {
        return static_cast<ShaderStageFlags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    ShaderStageFlags& operator|=(ShaderStageFlags& left, ShaderStageFlags right)
    {
        left = left | right;
        return left;
    }

    bool has_stage(ShaderStageFlags flags, ShaderStageFlags stage)
    {
        return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(stage)) != 0;
    }

    Sha256Hash calculate_target_binding_hash(ShaderTarget target, std::uint32_t mapping_version,
                                             const std::vector<ShaderMapBinding>& bindings)
    {
        std::vector<std::uint8_t> bytes;
        append_enum(bytes, target);
        append_integer(bytes, mapping_version);
        append_integer(bytes, static_cast<std::uint32_t>(bindings.size()));
        for (const ShaderMapBinding& binding : bindings)
        {
            append_integer(bytes, binding.binding_id);
            append_enum(bytes, binding.group);
            append_enum(bytes, binding.category);
            append_enum(bytes, binding.stages);
            append_enum(bytes, binding.register_class);
            append_integer(bytes, binding.register_index);
            append_integer(bytes, binding.descriptor_set);
            append_integer(bytes, binding.descriptor_binding);
        }
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_stage_reflection_hash(const ShaderStageReflection& reflection)
    {
        std::vector<std::uint8_t> bytes;
        append_enum(bytes, reflection.stage);
        append_string(bytes, reflection.entry_point);
        append_integer(bytes, static_cast<std::uint32_t>(reflection.bindings.size()));
        for (const ReflectedBinding& binding : reflection.bindings)
        {
            append_integer(bytes, binding.parameter_id);
            append_string(bytes, binding.name);
            append_enum(bytes, binding.group);
            append_enum(bytes, binding.category);
            append_integer(bytes,
                           binding.resource_kind ? static_cast<std::uint32_t>(*binding.resource_kind) : 0xffffffffu);
            append_enum(bytes, binding.stages);
            append_integer(bytes, binding.array_count);
            append_integer(bytes, binding.descriptor_set);
            append_integer(bytes, binding.descriptor_binding);
            append_integer(bytes, binding.constant_buffer_size);
            bytes.insert(bytes.end(), binding.data_layout_hash.begin(), binding.data_layout_hash.end());
            append_integer(bytes, binding.shader_abi_version);
            append_integer(bytes, static_cast<std::uint32_t>(binding.constant_members.size()));
            for (const ReflectedConstantMember& member : binding.constant_members)
            {
                append_integer(bytes, member.parameter_id);
                append_string(bytes, member.name);
                append_enum(bytes, member.type);
                append_integer(bytes, member.offset);
                append_integer(bytes, member.size);
                append_integer(bytes, member.array_stride);
                append_integer(bytes, member.matrix_stride);
            }
        }
        append_integer(bytes, static_cast<std::uint32_t>(reflection.interface_variables.size()));
        for (const ReflectedInterfaceVariable& variable : reflection.interface_variables)
        {
            append_string(bytes, variable.name);
            append_string(bytes, variable.semantic);
            append_integer(bytes, variable.location);
            append_integer(bytes, variable.input ? 1u : 0u);
            append_enum(bytes, variable.scalar_type);
            append_integer(bytes, variable.component_count);
        }
        append_integer(bytes, reflection.thread_group_size_x);
        append_integer(bytes, reflection.thread_group_size_y);
        append_integer(bytes, reflection.thread_group_size_z);
        return sha256(bytes);
    }

    bool is_valid_shader_graphics_pass_state(const ShaderGraphicsPassState& state)
    {
        const auto valid_stencil_face = [](const ShaderGraphicsPassState::StencilFaceState& face)
        {
            return enum_at_most(face.compare_operation, ShaderGraphicsPassState::CompareOperation::Always) &&
                   enum_at_most(face.fail_operation, ShaderGraphicsPassState::StencilOperation::DecrementWrap) &&
                   enum_at_most(face.depth_fail_operation, ShaderGraphicsPassState::StencilOperation::DecrementWrap) &&
                   enum_at_most(face.pass_operation, ShaderGraphicsPassState::StencilOperation::DecrementWrap);
        };
        const auto valid_color_mask = [](ShaderGraphicsPassState::ColorWriteMask mask)
        {
            switch (mask)
            {
            case ShaderGraphicsPassState::ColorWriteMask::None:
            case ShaderGraphicsPassState::ColorWriteMask::Red:
            case ShaderGraphicsPassState::ColorWriteMask::Green:
            case ShaderGraphicsPassState::ColorWriteMask::Blue:
            case ShaderGraphicsPassState::ColorWriteMask::Alpha:
            case ShaderGraphicsPassState::ColorWriteMask::RedGreen:
            case ShaderGraphicsPassState::ColorWriteMask::RedGreenBlue:
            case ShaderGraphicsPassState::ColorWriteMask::All:
                return true;
            }
            return false;
        };
        const ShaderGraphicsPassState::StencilFaceState default_stencil_face;
        const ShaderGraphicsPassState::BlendState default_blend;
        const bool canonical_stencil =
            (state.stencil.mode == ShaderGraphicsPassState::StencilMode::Off && state.stencil.read_mask == 0xffu &&
             state.stencil.write_mask == 0xffu && same_stencil_face(state.stencil.front, default_stencil_face) &&
             same_stencil_face(state.stencil.back, default_stencil_face)) ||
            (state.stencil.mode == ShaderGraphicsPassState::StencilMode::FrontAndBack &&
             same_stencil_face(state.stencil.front, state.stencil.back)) ||
            state.stencil.mode == ShaderGraphicsPassState::StencilMode::SeparateFaces;
        const bool canonical_blend =
            state.blend.enabled || (state.blend.source_color_factor == default_blend.source_color_factor &&
                                    state.blend.destination_color_factor == default_blend.destination_color_factor &&
                                    state.blend.color_operation == default_blend.color_operation &&
                                    state.blend.source_alpha_factor == default_blend.source_alpha_factor &&
                                    state.blend.destination_alpha_factor == default_blend.destination_alpha_factor &&
                                    state.blend.alpha_operation == default_blend.alpha_operation);

        return enum_at_most(state.primitive_topology, ShaderGraphicsPassState::PrimitiveTopology::TriangleStrip) &&
               enum_at_most(state.cull_mode, ShaderGraphicsPassState::CullMode::Back) &&
               enum_at_most(state.front_face, ShaderGraphicsPassState::FrontFace::CounterClockwise) &&
               enum_at_most(state.fill_mode, ShaderGraphicsPassState::FillMode::Wireframe) &&
               enum_at_most(state.depth_compare_operation, ShaderGraphicsPassState::CompareOperation::Always) &&
               enum_at_most(state.stencil.mode, ShaderGraphicsPassState::StencilMode::SeparateFaces) &&
               valid_stencil_face(state.stencil.front) && valid_stencil_face(state.stencil.back) &&
               enum_at_most(state.blend.source_color_factor,
                            ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate) &&
               enum_at_most(state.blend.destination_color_factor,
                            ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate) &&
               enum_at_most(state.blend.color_operation, ShaderGraphicsPassState::BlendOperation::Maximum) &&
               enum_at_most(state.blend.source_alpha_factor,
                            ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate) &&
               enum_at_most(state.blend.destination_alpha_factor,
                            ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate) &&
               enum_at_most(state.blend.alpha_operation, ShaderGraphicsPassState::BlendOperation::Maximum) &&
               valid_color_mask(state.color_write_mask) && canonical_stencil && canonical_blend &&
               (state.depth_test_enable ||
                state.depth_compare_operation == ShaderGraphicsPassState::CompareOperation::GreaterEqual);
    }

    Sha256Hash calculate_shader_graphics_pass_state_hash(const ShaderGraphicsPassState& state)
    {
        std::vector<std::uint8_t> bytes;
        append_enum(bytes, state.primitive_topology);
        append_enum(bytes, state.cull_mode);
        append_enum(bytes, state.front_face);
        append_enum(bytes, state.fill_mode);
        append_integer(bytes, state.depth_test_enable ? 1u : 0u);
        append_enum(bytes, state.depth_compare_operation);
        append_integer(bytes, state.depth_write_enable ? 1u : 0u);
        append_enum(bytes, state.stencil.mode);
        append_integer(bytes, state.stencil.read_mask);
        append_integer(bytes, state.stencil.write_mask);
        append_stencil_face(bytes, state.stencil.front);
        append_stencil_face(bytes, state.stencil.back);
        append_integer(bytes, state.blend.enabled ? 1u : 0u);
        append_enum(bytes, state.blend.source_color_factor);
        append_enum(bytes, state.blend.destination_color_factor);
        append_enum(bytes, state.blend.color_operation);
        append_enum(bytes, state.blend.source_alpha_factor);
        append_enum(bytes, state.blend.destination_alpha_factor);
        append_enum(bytes, state.blend.alpha_operation);
        append_enum(bytes, state.color_write_mask);
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_map_key(const ShaderMapEntry& entry)
    {
        std::vector<std::uint8_t> bytes;
        append_integer(bytes, shader_map_entry_version);
        append_string(bytes, entry.shader_name);
        append_string(bytes, entry.pass_name);
        append_enum(bytes, entry.target);
        append_enum(bytes, entry.profile);
        append_integer(bytes, entry.mapping_version);
        bytes.insert(bytes.end(), entry.logical_layout_hash.begin(), entry.logical_layout_hash.end());
        bytes.insert(bytes.end(), entry.parameter_schema.schema_identity.begin(),
                     entry.parameter_schema.schema_identity.end());
        bytes.insert(bytes.end(), entry.target_binding_hash.begin(), entry.target_binding_hash.end());
        const Sha256Hash pass_state_hash = calculate_shader_graphics_pass_state_hash(entry.graphics_pass_state);
        bytes.insert(bytes.end(), pass_state_hash.begin(), pass_state_hash.end());
        bytes.insert(bytes.end(), entry.pass_template_hash.begin(), entry.pass_template_hash.end());
        append_integer(bytes, entry.variant_id_version);
        append_integer(bytes, entry.permutation_version);
        bytes.insert(bytes.end(), entry.permutation_key.begin(), entry.permutation_key.end());
        for (const ShaderCodeEntry* stage : sorted_stages(entry))
        {
            append_enum(bytes, stage->request.stage);
            bytes.insert(bytes.end(), stage->request.compile_key.begin(), stage->request.compile_key.end());
            bytes.insert(bytes.end(), stage->reflection.reflection_hash.begin(),
                         stage->reflection.reflection_hash.end());
            const Sha256Hash binary_hash = sha256(stage->binary);
            bytes.insert(bytes.end(), binary_hash.begin(), binary_hash.end());
        }
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_map_entry_content_hash(const ShaderMapEntry& entry)
    {
        std::vector<std::uint8_t> bytes;
        const Sha256Hash key = calculate_shader_map_key(entry);
        bytes.insert(bytes.end(), key.begin(), key.end());
        append_integer(bytes, static_cast<std::uint32_t>(entry.bindings.size()));
        for (const ShaderMapBinding& binding : entry.bindings)
        {
            append_integer(bytes, binding.binding_id);
            append_string(bytes, binding.name);
            append_enum(bytes, binding.group);
            append_enum(bytes, binding.category);
            append_enum(bytes, binding.stages);
            append_enum(bytes, binding.register_class);
            append_integer(bytes, binding.register_index);
            append_integer(bytes, binding.descriptor_set);
            append_integer(bytes, binding.descriptor_binding);
            append_integer(bytes, binding.data_size);
            bytes.insert(bytes.end(), binding.data_layout_hash.begin(), binding.data_layout_hash.end());
            append_integer(bytes, binding.shader_abi_version);
        }
        for (const ShaderCodeEntry* stage : sorted_stages(entry))
        {
            append_integer(bytes, static_cast<std::uint32_t>(stage->request.dependencies.size()));
            for (const ShaderDependency& dependency : stage->request.dependencies)
            {
                append_string(bytes, dependency.virtual_path);
                bytes.insert(bytes.end(), dependency.content_hash.begin(), dependency.content_hash.end());
            }
        }
        return sha256(bytes);
    }
} // namespace toy3d::shader
