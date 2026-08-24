#include "layout/shader_layout.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace toy3d::shader
{
    // Layout helpers use string_view for stable identity hashing without name
    // copies and optional when a source type has no legal shader mapping.
    namespace
    {
        struct ValueTypeInfo
        {
            std::uint32_t alignment = 4;
            std::uint32_t size = 4;
            std::uint32_t matrix_rows = 0;
            std::uint32_t matrix_columns = 0;
        };

        std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment)
        {
            return (value + alignment - 1u) / alignment * alignment;
        }

        ValueTypeInfo value_type_info(ShaderValueType type)
        {
            switch (type)
            {
            case ShaderValueType::Float32:
            case ShaderValueType::Int32:
            case ShaderValueType::UInt32: return {4, 4, 0, 0};
            case ShaderValueType::Float32x2:
            case ShaderValueType::Int32x2:
            case ShaderValueType::UInt32x2: return {8, 8, 0, 0};
            case ShaderValueType::Float32x3:
            case ShaderValueType::Int32x3:
            case ShaderValueType::UInt32x3: return {4, 12, 0, 0};
            case ShaderValueType::Float32x4:
            case ShaderValueType::Int32x4:
            case ShaderValueType::UInt32x4: return {16, 16, 0, 0};
            case ShaderValueType::Float32x2x2: return {16, 32, 2, 2};
            case ShaderValueType::Float32x2x3: return {16, 48, 2, 3};
            case ShaderValueType::Float32x2x4: return {16, 64, 2, 4};
            case ShaderValueType::Float32x3x2: return {16, 32, 3, 2};
            case ShaderValueType::Float32x3x3: return {16, 48, 3, 3};
            case ShaderValueType::Float32x3x4: return {16, 64, 3, 4};
            case ShaderValueType::Float32x4x2: return {16, 32, 4, 2};
            case ShaderValueType::Float32x4x3: return {16, 48, 4, 3};
            case ShaderValueType::Float32x4x4: return {16, 64, 4, 4};
            }
            return {};
        }

        const char* group_name(BindingGroup group)
        {
            switch (group)
            {
            case BindingGroup::Global: return "Global";
            case BindingGroup::View: return "View";
            case BindingGroup::Pass: return "Pass";
            case BindingGroup::Material: return "Material";
            case BindingGroup::Object: return "Object";
            }
            return "Invalid";
        }

        const char* category_name(ShaderParameterCategory category)
        {
            switch (category)
            {
            case ShaderParameterCategory::Constant: return "Constant";
            case ShaderParameterCategory::SampledTexture: return "SampledTexture";
            case ShaderParameterCategory::Sampler: return "Sampler";
            case ShaderParameterCategory::ReadOnlyBuffer: return "ReadOnlyBuffer";
            case ShaderParameterCategory::StorageBuffer: return "StorageBuffer";
            case ShaderParameterCategory::StorageTexture: return "StorageTexture";
            }
            return "Invalid";
        }

        template<typename T>
        void append_integer(std::vector<std::uint8_t>& bytes, T value)
        {
            using Unsigned = std::make_unsigned_t<T>;
            const Unsigned unsigned_value = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                bytes.push_back(static_cast<std::uint8_t>(unsigned_value >> (index * 8u)));
            }
        }

        template<typename T>
        void append_enum(std::vector<std::uint8_t>& bytes, T value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value));
        }

        void append_string(std::vector<std::uint8_t>& bytes, std::string_view value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        void append_double(std::vector<std::uint8_t>& bytes, double value)
        {
            std::uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value), "double serialization requires 64-bit IEEE storage");
            std::memcpy(&bits, &value, sizeof(bits));
            append_integer(bytes, bits);
        }

        void append_default_value(std::vector<std::uint8_t>& bytes, const DefaultValue& value)
        {
            append_enum(bytes, value.kind);
            append_integer(bytes, static_cast<std::uint32_t>(value.numbers.size()));
            for (double number : value.numbers) append_double(bytes, number);
            append_string(bytes, value.text);
        }

        std::optional<ShaderValueType> property_value_type(PropertyType type)
        {
            switch (type)
            {
            case PropertyType::Float:
            case PropertyType::Range: return ShaderValueType::Float32;
            case PropertyType::Float2: return ShaderValueType::Float32x2;
            case PropertyType::Float3: return ShaderValueType::Float32x3;
            case PropertyType::Float4:
            case PropertyType::Color: return ShaderValueType::Float32x4;
            case PropertyType::Matrix4x4: return ShaderValueType::Float32x4x4;
            default: return std::nullopt;
            }
        }

        std::uint32_t value_component_count(ShaderValueType type)
        {
            const ValueTypeInfo info = value_type_info(type);
            if (info.matrix_columns != 0) return info.matrix_rows * info.matrix_columns;
            return info.size / 4u;
        }

        bool is_sampler_preset(std::string_view value)
        {
            return value == "PointClamp" || value == "PointWrap" || value == "LinearClamp" ||
                value == "LinearWrap" || value == "TrilinearClamp" || value == "TrilinearWrap" ||
                value == "ShadowCompareClamp";
        }

        std::optional<ShaderParameterCategory> resource_category(ResourceKind kind)
        {
            switch (kind)
            {
            case ResourceKind::Texture2D:
            case ResourceKind::Texture2DArray:
            case ResourceKind::Texture3D:
            case ResourceKind::TextureCube:
            case ResourceKind::Texture2DMS:
                return ShaderParameterCategory::SampledTexture;
            case ResourceKind::Sampler:
            case ResourceKind::ComparisonSampler:
                return ShaderParameterCategory::Sampler;
            case ResourceKind::Buffer:
            case ResourceKind::ByteAddressBuffer:
            case ResourceKind::StructuredBuffer:
                return ShaderParameterCategory::ReadOnlyBuffer;
            case ResourceKind::RWBuffer:
            case ResourceKind::RWByteAddressBuffer:
            case ResourceKind::RWStructuredBuffer:
                return ShaderParameterCategory::StorageBuffer;
            case ResourceKind::RWTexture2D:
            case ResourceKind::RWTexture2DArray:
            case ResourceKind::RWTexture3D:
                return ShaderParameterCategory::StorageTexture;
            }
            return std::nullopt;
        }

        ResourceKind property_resource_kind(PropertyType type)
        {
            switch (type)
            {
            case PropertyType::TextureCube: return ResourceKind::TextureCube;
            case PropertyType::Sampler: return ResourceKind::Sampler;
            case PropertyType::ComparisonSampler: return ResourceKind::ComparisonSampler;
            default: return ResourceKind::Texture2D;
            }
        }

        void write_numeric_default(ShaderConstantMember& member, const DefaultValue& value)
        {
            member.default_value.assign(member.size, 0u);
            if (value.kind != DefaultValueKind::Numbers)
            {
                return;
            }
            const ValueTypeInfo info = value_type_info(member.type);
            for (std::size_t index = 0; index < value.numbers.size(); ++index)
            {
                std::uint32_t byte_offset = static_cast<std::uint32_t>(index * 4u);
                if (info.matrix_columns != 0)
                {
                    const std::uint32_t column = static_cast<std::uint32_t>(index) / info.matrix_rows;
                    const std::uint32_t row = static_cast<std::uint32_t>(index) % info.matrix_rows;
                    byte_offset = column * 16u + row * 4u;
                }
                if (byte_offset + sizeof(float) > member.default_value.size())
                {
                    break;
                }
                const float converted = static_cast<float>(value.numbers[index]);
                std::memcpy(member.default_value.data() + byte_offset, &converted, sizeof(converted));
            }
        }

        void append_layout(std::vector<std::uint8_t>& bytes, const LogicalShaderLayout& layout, bool include_schema)
        {
            append_integer(bytes, toy_shader_abi_version);
            append_integer(bytes, shader_parameter_id_version);
            append_integer(bytes, static_cast<std::uint32_t>(layout.constant_buffers.size()));
            for (const ConstantBufferLayout& buffer : layout.constant_buffers)
            {
                append_enum(bytes, buffer.group);
                append_integer(bytes, buffer.size);
                append_integer(bytes, static_cast<std::uint32_t>(buffer.members.size()));
                for (const ShaderConstantMember& member : buffer.members)
                {
                    append_integer(bytes, member.parameter_id);
                    append_string(bytes, member.name);
                    append_enum(bytes, member.type);
                    append_integer(bytes, member.offset);
                    append_integer(bytes, member.size);
                    append_integer(bytes, member.array_count);
                    append_integer(bytes, member.array_stride);
                    append_integer(bytes, member.matrix_stride);
                    if (include_schema)
                    {
                        append_integer(bytes, static_cast<std::uint32_t>(member.default_value.size()));
                        bytes.insert(bytes.end(), member.default_value.begin(), member.default_value.end());
                    }
                }
            }
            append_integer(bytes, static_cast<std::uint32_t>(layout.resources.size()));
            for (const ShaderResourceParameter& resource : layout.resources)
            {
                append_integer(bytes, resource.parameter_id);
                append_string(bytes, resource.name);
                append_enum(bytes, resource.group);
                append_enum(bytes, resource.category);
                append_enum(bytes, resource.resource_kind);
                append_enum(bytes, resource.element_type);
                append_integer(bytes, resource.array_count);
                if (include_schema)
                {
                    append_default_value(bytes, resource.default_value);
                }
            }
        }

        void add_error(std::vector<Diagnostic>& diagnostics, DiagnosticCode code, const SourceLocation& location, std::string message)
        {
            diagnostics.push_back({DiagnosticSeverity::Error, code, location, std::move(message)});
        }
    }

    bool ConstantBufferPackResult::succeeded() const
    {
        return layout.has_value() && diagnostics.empty();
    }

    bool LogicalLayoutResult::succeeded() const
    {
        return layout.has_value() && diagnostics.empty();
    }

    bool ActiveLayoutResult::succeeded() const
    {
        return layout.has_value() && diagnostics.empty();
    }

    std::uint32_t structured_element_stride(ResourceElementType type)
    {
        switch (type)
        {
        case ResourceElementType::Float:
        case ResourceElementType::Int:
        case ResourceElementType::UInt: return 4;
        case ResourceElementType::Float2:
        case ResourceElementType::Int2:
        case ResourceElementType::UInt2: return 8;
        case ResourceElementType::Float3:
        case ResourceElementType::Float4:
        case ResourceElementType::Int3:
        case ResourceElementType::Int4:
        case ResourceElementType::UInt3:
        case ResourceElementType::UInt4: return 16;
        case ResourceElementType::Float2x2:
        case ResourceElementType::Float3x2:
        case ResourceElementType::Float4x2: return 32;
        case ResourceElementType::Float2x3:
        case ResourceElementType::Float3x3:
        case ResourceElementType::Float4x3: return 48;
        case ResourceElementType::Float2x4:
        case ResourceElementType::Float3x4:
        case ResourceElementType::Float4x4: return 64;
        case ResourceElementType::None: return 0;
        }
        return 0;
    }

    ConstantBufferPackResult pack_constant_buffer(
        BindingGroup group,
        const std::vector<ConstantMemberInput>& inputs)
    {
        ConstantBufferPackResult result;
        ConstantBufferLayout layout;
        layout.group = group;
        std::uint32_t offset = 0;
        for (const ConstantMemberInput& input : inputs)
        {
            const ValueTypeInfo info = value_type_info(input.type);
            const std::uint32_t array_count = std::max(input.array_count, 1u);
            ShaderConstantMember member;
            member.parameter_id = make_shader_parameter_id(group, ShaderParameterCategory::Constant, input.name);
            if (member.parameter_id == 0)
            {
                add_error(result.diagnostics, DiagnosticCode::ShaderParameterIdCollision, input.location, "ShaderParameterId 0 is reserved as invalid.");
            }
            const bool duplicate_id = std::any_of(layout.members.begin(), layout.members.end(), [&](const ShaderConstantMember& existing) {
                return existing.parameter_id == member.parameter_id;
            });
            if (duplicate_id)
            {
                add_error(result.diagnostics, DiagnosticCode::ShaderParameterIdCollision, input.location, "Constant member ShaderParameterId collides with an earlier member.");
            }
            member.name = input.name;
            member.type = input.type;
            member.array_count = array_count;
            member.location = input.location;
            if (info.matrix_columns != 0 || array_count > 1u)
            {
                offset = align_up(offset, 16u);
            }
            else
            {
                offset = align_up(offset, info.alignment);
                if ((offset % 16u) + info.size > 16u)
                {
                    offset = align_up(offset, 16u);
                }
            }
            member.offset = offset;
            member.matrix_stride = info.matrix_columns == 0 ? 0u : 16u;
            member.array_stride = array_count > 1u ? align_up(info.size, 16u) : 0u;
            member.size = array_count > 1u ? member.array_stride * array_count : info.size;
            offset += member.size;
            layout.members.push_back(std::move(member));
        }
        layout.size = align_up(offset, 16u);
        if (layout.size > max_constant_buffer_size)
        {
            const SourceLocation location = inputs.empty() ? SourceLocation{} : inputs.back().location;
            add_error(result.diagnostics, DiagnosticCode::ConstantBufferSizeLimitExceeded, location, "Constant buffer exceeds the ToyShaderABI 16 KiB group limit.");
            return result;
        }
        if (!result.diagnostics.empty()) return result;
        result.layout = std::move(layout);
        return result;
    }

    LogicalLayoutResult compile_logical_layout(const ShaderAsset& asset)
    {
        LogicalLayoutResult result;
        LogicalShaderLayout layout;

        // View and Object constants are engine-owned canonical schemas. Keep
        // them in every logical layout so HLSL usage, rather than a Shader-name
        // special case, determines whether either group becomes active.
        const std::vector<ConstantMemberInput> view_inputs = {
            {"toy_view", ShaderValueType::Float32x4x4},
            {"toy_projection", ShaderValueType::Float32x4x4},
            {"toy_view_projection", ShaderValueType::Float32x4x4},
            {"toy_inverse_view", ShaderValueType::Float32x4x4},
            {"toy_inverse_projection", ShaderValueType::Float32x4x4},
            {"toy_inverse_view_projection", ShaderValueType::Float32x4x4},
            {"toy_camera_position", ShaderValueType::Float32x3},
            {"toy_camera_direction", ShaderValueType::Float32x3}};
        ConstantBufferPackResult view_buffer =
            pack_constant_buffer(BindingGroup::View, view_inputs);
        result.diagnostics.insert(
            result.diagnostics.end(),
            view_buffer.diagnostics.begin(), view_buffer.diagnostics.end());
        if (view_buffer.layout)
        {
            layout.constant_buffers.push_back(std::move(*view_buffer.layout));
        }

        std::vector<ConstantMemberInput> material_inputs;
        for (const Property& property : asset.properties)
        {
            const auto value_type = property_value_type(property.type);
            if (value_type)
            {
                const std::uint32_t expected_count = value_component_count(*value_type);
                if (property.default_value.kind != DefaultValueKind::Numbers || property.default_value.numbers.size() != expected_count)
                {
                    add_error(result.diagnostics, DiagnosticCode::InvalidDefaultValue, property.default_value.location, "Property '" + property.name + "' requires exactly " + std::to_string(expected_count) + " numeric default component(s).");
                }
                if (property.type == PropertyType::Range && property.default_value.numbers.size() == 1u &&
                    ((property.range_min && property.default_value.numbers[0] < *property.range_min) ||
                     (property.range_max && property.default_value.numbers[0] > *property.range_max)))
                {
                    add_error(result.diagnostics, DiagnosticCode::InvalidDefaultValue, property.default_value.location, "Range property default must be within its declared bounds.");
                }
                material_inputs.push_back({property.name, *value_type, 1u, property.location});
            }
            else if (property.type == PropertyType::Texture2D || property.type == PropertyType::TextureCube)
            {
                if (property.default_value.kind != DefaultValueKind::String && property.default_value.kind != DefaultValueKind::Identifier)
                {
                    add_error(result.diagnostics, DiagnosticCode::InvalidDefaultValue, property.default_value.location, "Texture property default must be an asset or builtin texture name.");
                }
            }
            else
            {
                const bool valid_preset = property.default_value.kind == DefaultValueKind::Identifier && is_sampler_preset(property.default_value.text);
                const bool comparison_matches = property.type != PropertyType::ComparisonSampler || property.default_value.text == "ShadowCompareClamp";
                const bool regular_matches = property.type != PropertyType::Sampler || property.default_value.text != "ShadowCompareClamp";
                if (!valid_preset || !comparison_matches || !regular_matches)
                {
                    add_error(result.diagnostics, DiagnosticCode::InvalidDefaultValue, property.default_value.location, "Sampler property default is not valid for its sampler type.");
                }
            }
        }
        if (!material_inputs.empty())
        {
            ConstantBufferPackResult packed = pack_constant_buffer(BindingGroup::Material, material_inputs);
            result.diagnostics.insert(result.diagnostics.end(), packed.diagnostics.begin(), packed.diagnostics.end());
            if (packed.layout)
            {
                for (ShaderConstantMember& member : packed.layout->members)
                {
                    const auto property = std::find_if(asset.properties.begin(), asset.properties.end(), [&](const Property& candidate) {
                        return candidate.name == member.name;
                    });
                    if (property != asset.properties.end())
                    {
                        write_numeric_default(member, property->default_value);
                    }
                }
                layout.constant_buffers.push_back(std::move(*packed.layout));
            }
        }

        const std::vector<ConstantMemberInput> object_inputs = {
            {"toy_object_to_world", ShaderValueType::Float32x4x4}};
        ConstantBufferPackResult object_buffer =
            pack_constant_buffer(BindingGroup::Object, object_inputs);
        result.diagnostics.insert(
            result.diagnostics.end(),
            object_buffer.diagnostics.begin(), object_buffer.diagnostics.end());
        if (object_buffer.layout)
        {
            layout.constant_buffers.push_back(std::move(*object_buffer.layout));
        }

        for (const Property& property : asset.properties)
        {
            if (property_value_type(property.type))
            {
                continue;
            }
            ShaderResourceParameter resource;
            resource.name = property.name;
            resource.group = BindingGroup::Material;
            resource.resource_kind = property_resource_kind(property.type);
            resource.category = *resource_category(resource.resource_kind);
            resource.element_type = resource.category == ShaderParameterCategory::SampledTexture ?
                ResourceElementType::Float4 : ResourceElementType::None;
            resource.default_value = property.default_value;
            resource.location = property.location;
            resource.parameter_id = make_shader_parameter_id(resource.group, resource.category, resource.name);
            layout.resources.push_back(std::move(resource));
        }
        for (const Resource& input : asset.resources)
        {
            ShaderResourceParameter resource;
            resource.name = input.name;
            resource.group = input.group;
            resource.resource_kind = input.kind;
            resource.category = *resource_category(input.kind);
            resource.element_type = input.element_type;
            resource.default_value = input.default_value;
            resource.location = input.location;
            resource.parameter_id = make_shader_parameter_id(resource.group, resource.category, resource.name);
            layout.resources.push_back(std::move(resource));
        }
        std::sort(layout.resources.begin(), layout.resources.end(), [](const ShaderResourceParameter& left, const ShaderResourceParameter& right) {
            if (left.group != right.group) return left.group < right.group;
            if (left.category != right.category) return left.category < right.category;
            return left.parameter_id < right.parameter_id;
        });

        std::unordered_map<ShaderParameterId, std::string> identities;
        for (const ConstantBufferLayout& buffer : layout.constant_buffers)
        {
            for (const ShaderConstantMember& member : buffer.members)
            {
                identities.emplace(member.parameter_id, std::string(group_name(buffer.group)) + "/Constant/" + member.name);
            }
        }
        for (const ShaderResourceParameter& resource : layout.resources)
        {
            const std::string identity = std::string(group_name(resource.group)) + "/" + category_name(resource.category) + "/" + resource.name;
            // Structured binding names both map insertion results directly;
            // this keeps the collision branch tied to the returned iterator.
            const auto [found, inserted] = identities.emplace(resource.parameter_id, identity);
            if (!inserted && found->second != identity)
            {
                add_error(result.diagnostics, DiagnosticCode::ShaderParameterIdCollision, resource.location, "ShaderParameterId collision between '" + found->second + "' and '" + identity + "'.");
            }
        }
        if (!result.diagnostics.empty())
        {
            return result;
        }

        std::vector<std::uint8_t> schema_bytes;
        append_layout(schema_bytes, layout, true);
        append_integer(schema_bytes, static_cast<std::uint32_t>(asset.properties.size()));
        for (const Property& property : asset.properties)
        {
            append_string(schema_bytes, property.name);
            append_string(schema_bytes, property.display_name);
            append_enum(schema_bytes, property.type);
            append_integer(schema_bytes, static_cast<std::uint8_t>(property.range_min.has_value()));
            if (property.range_min) append_double(schema_bytes, *property.range_min);
            append_integer(schema_bytes, static_cast<std::uint8_t>(property.range_max.has_value()));
            if (property.range_max) append_double(schema_bytes, *property.range_max);
            append_default_value(schema_bytes, property.default_value);
        }
        layout.parameter_schema_hash = sha256(schema_bytes);
        std::vector<std::uint8_t> logical_bytes;
        append_layout(logical_bytes, layout, false);
        layout.logical_layout_hash = sha256(logical_bytes);
        result.layout = std::move(layout);
        return result;
    }

    ActiveLayoutResult build_active_layout(
        const LogicalShaderLayout& logical_layout,
        const std::vector<ParameterUsage>& usage)
    {
        ActiveLayoutResult result;
        ActiveShaderLayout active;
        active.logical_layout = &logical_layout;
        std::unordered_map<std::string, ShaderStageFlags> usage_by_name;
        for (const ParameterUsage& entry : usage)
        {
            usage_by_name[entry.name] |= entry.stages;
        }
        std::unordered_set<std::string> known_names;
        for (const ConstantBufferLayout& buffer : logical_layout.constant_buffers)
        {
            ShaderStageFlags stages = ShaderStageFlags::None;
            for (const ShaderConstantMember& member : buffer.members)
            {
                known_names.insert(member.name);
                const auto found = usage_by_name.find(member.name);
                if (found != usage_by_name.end()) stages |= found->second;
            }
            if (stages != ShaderStageFlags::None)
            {
                ActiveBinding binding;
                binding.binding_id = make_shader_parameter_id(buffer.group, ShaderParameterCategory::Constant, "");
                binding.name = std::string("toy_") + group_name(buffer.group) + "_data";
                std::transform(binding.name.begin(), binding.name.end(), binding.name.begin(), [](unsigned char value) {
                    return static_cast<char>(value >= 'A' && value <= 'Z' ? value - 'A' + 'a' : value);
                });
                binding.group = buffer.group;
                binding.category = ShaderParameterCategory::Constant;
                binding.stages = stages;
                binding.constant_buffer = &buffer;
                active.bindings.push_back(std::move(binding));
            }
        }
        for (const ShaderResourceParameter& resource : logical_layout.resources)
        {
            known_names.insert(resource.name);
            const auto found = usage_by_name.find(resource.name);
            if (found == usage_by_name.end() || found->second == ShaderStageFlags::None) continue;
            active.bindings.push_back({resource.parameter_id, resource.name, resource.group, resource.category, found->second, nullptr, &resource});
        }
        for (const ParameterUsage& entry : usage)
        {
            if (known_names.find(entry.name) == known_names.end())
            {
                add_error(result.diagnostics, DiagnosticCode::UnknownParameterUsage, {}, "Program usage references unknown Shader parameter '" + entry.name + "'.");
            }
        }
        if (!result.diagnostics.empty()) return result;
        result.layout = std::move(active);
        return result;
    }
}
