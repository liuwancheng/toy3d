#include "property_path.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace toy3d
{
    PropertyPathPart PropertyPathPart::field(std::string name)
    {
        return {PropertyPathKind::Field, std::move(name), 0, {}};
    }

    PropertyPathPart PropertyPathPart::index_at(std::size_t index)
    {
        return {PropertyPathKind::Index, {}, index, {}};
    }

    PropertyPathPart PropertyPathPart::element_id(std::string identity_property, std::string identity)
    {
        return {PropertyPathKind::ElementId, std::move(identity), 0, std::move(identity_property)};
    }

    PropertyPathPart PropertyPathPart::variant_branch(std::string stable_tag)
    {
        return {PropertyPathKind::VariantBranch, std::move(stable_tag), 0, {}};
    }

    namespace
    {
        struct FieldFrame
        {
            std::string name;
            std::uint8_t flags = 0;
            std::vector<std::uint8_t> bytes;
        };

        AssetStatus error(const std::string& path, const char* message,
                          AssetErrorCode code = AssetErrorCode::Value)
        {
            return {code, {}, {}, {}, path, message, {}};
        }

        AssetStatus value_error(const ValueStatus& status)
        {
            return error(status.property_path, status.message.c_str());
        }

        std::string append_path(const std::string& base, const PropertyPathPart& part)
        {
            if (part.kind == PropertyPathKind::Field)
                return base.empty() ? part.name : base + '.' + part.name;
            if (part.kind == PropertyPathKind::Index)
                return base + '[' + std::to_string(part.index) + ']';
            return base + '[' + part.name + ']';
        }

        ValueStatus parse_fields(const std::vector<std::uint8_t>& bytes, ValueLimits limits,
                                 std::vector<FieldFrame>& fields)
        {
            ValueReader reader(bytes, limits);
            std::uint32_t count = 0;
            ValueStatus status = reader.read_array_length(count);
            if (!status.succeeded()) return status;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                FieldFrame field;
                status = reader.read_utf8(field.name);
                if (!status.succeeded()) return status;
                reader.set_property_path(field.name);
                status = reader.read_uint8(field.flags);
                if (!status.succeeded()) return status;
                if (field.flags > 1)
                    return reader.failure(ValueErrorCode::InvalidValue, "unknown field flags");
                status = reader.read_blob(field.bytes);
                if (!status.succeeded()) return status;
                fields.push_back(std::move(field));
            }
            if (!reader.at_end())
                return reader.failure(ValueErrorCode::InvalidValue, "trailing struct bytes");
            return ValueStatus::success();
        }

        ValueStatus write_fields(std::vector<FieldFrame> fields, ValueLimits limits,
                                 std::vector<std::uint8_t>& output)
        {
            std::sort(fields.begin(), fields.end(), [](const FieldFrame& left, const FieldFrame& right)
                      { return left.name < right.name; });
            ValueWriter writer(limits);
            ValueStatus status = writer.write_array_length(static_cast<std::uint32_t>(fields.size()));
            if (!status.succeeded()) return status;
            for (const FieldFrame& field : fields)
            {
                writer.set_property_path(field.name);
                status = writer.write_utf8(field.name);
                if (!status.succeeded()) return status;
                status = writer.write_uint8(field.flags);
                if (!status.succeeded()) return status;
                status = writer.write_blob(field.bytes);
                if (!status.succeeded()) return status;
            }
            output = writer.bytes();
            return ValueStatus::success();
        }

        std::string branch_tag(const ValueTypeDesc& type)
        {
            if (!type.stable_name.empty()) return type.stable_name;
            switch (type.kind)
            {
                case ValueKind::Bool: return "Bool";
                case ValueKind::Int8: return "Int8";
                case ValueKind::UInt8: return "UInt8";
                case ValueKind::Int16: return "Int16";
                case ValueKind::UInt16: return "UInt16";
                case ValueKind::Int32: return "Int32";
                case ValueKind::UInt32: return "UInt32";
                case ValueKind::Int64: return "Int64";
                case ValueKind::UInt64: return "UInt64";
                case ValueKind::Float32: return "Float32";
                case ValueKind::Float64: return "Float64";
                case ValueKind::Utf8: return "Utf8";
                default: return {};
            }
        }

        ValueStatus skip_value(ValueReader& reader, const ValueTypeDesc& type)
        {
            switch (type.kind)
            {
                case ValueKind::Bool: { bool value = false; return reader.read_bool(value); }
                case ValueKind::Int8: { std::int8_t value = 0; return reader.read_int8(value); }
                case ValueKind::UInt8: { std::uint8_t value = 0; return reader.read_uint8(value); }
                case ValueKind::Int16: { std::int16_t value = 0; return reader.read_int16(value); }
                case ValueKind::UInt16: { std::uint16_t value = 0; return reader.read_uint16(value); }
                case ValueKind::Int32: { std::int32_t value = 0; return reader.read_int32(value); }
                case ValueKind::UInt32: { std::uint32_t value = 0; return reader.read_uint32(value); }
                case ValueKind::Int64:
                case ValueKind::Enum: { std::int64_t value = 0; return reader.read_int64(value); }
                case ValueKind::UInt64: { std::uint64_t value = 0; return reader.read_uint64(value); }
                case ValueKind::Float64: { double value = 0; return reader.read_float64(value); }
                case ValueKind::Utf8: { std::string value; return reader.read_utf8(value); }
                case ValueKind::AssetRef: { AssetRef value; return decode_value(reader, value); }
                case ValueKind::Struct:
                {
                    std::uint32_t count = 0;
                    ValueStatus status = reader.read_array_length(count);
                    if (!status.succeeded()) return status;
                    for (std::uint32_t index = 0; index < count; ++index)
                    {
                        std::string name;
                        std::uint8_t flags = 0;
                        std::vector<std::uint8_t> payload;
                        status = reader.read_utf8(name);
                        if (!status.succeeded()) return status;
                        status = reader.read_uint8(flags);
                        if (!status.succeeded()) return status;
                        if (flags > 1) return reader.failure(ValueErrorCode::InvalidValue, "unknown field flags");
                        status = reader.read_blob(payload);
                        if (!status.succeeded()) return status;
                    }
                    return ValueStatus::success();
                }
                case ValueKind::Array:
                {
                    if (type.arguments.size() != 1)
                        return reader.failure(ValueErrorCode::InvalidValue, "array element schema is absent");
                    ValueDepthScope<ValueReader> depth(reader);
                    if (!depth.status().succeeded()) return depth.status();
                    std::uint32_t count = 0;
                    ValueStatus status = reader.read_array_length(count);
                    if (!status.succeeded()) return status;
                    for (std::uint32_t index = 0; index < count; ++index)
                    {
                        status = skip_value(reader, type.arguments[0]);
                        if (!status.succeeded()) return status;
                    }
                    return ValueStatus::success();
                }
                case ValueKind::Variant:
                {
                    ValueDepthScope<ValueReader> depth(reader);
                    if (!depth.status().succeeded()) return depth.status();
                    std::string tag;
                    ValueStatus status = reader.read_utf8(tag);
                    if (!status.succeeded()) return status;
                    for (const ValueTypeDesc& branch : type.arguments)
                        if (tag == branch_tag(branch)) return skip_value(reader, branch);
                    return reader.failure(ValueErrorCode::InvalidValue, "unknown variant branch");
                }
                default: break;
            }
            std::size_t count = 0;
            switch (type.kind)
            {
                case ValueKind::Float32: count = 1; break;
                case ValueKind::Vector2: count = 2; break;
                case ValueKind::Vector3: count = 3; break;
                case ValueKind::Vector4:
                case ValueKind::Quaternion: count = 4; break;
                case ValueKind::Matrix3: count = 9; break;
                case ValueKind::Transform: count = 10; break;
                case ValueKind::Matrix4: count = 16; break;
                default: return reader.failure(ValueErrorCode::InvalidValue, "unsupported property value type");
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                float value = 0.0f;
                ValueStatus status = reader.read_float32(value);
                if (!status.succeeded()) return status;
            }
            return ValueStatus::success();
        }

        ValueStatus split_array(const std::vector<std::uint8_t>& bytes, const ValueTypeDesc& element,
                                ValueLimits limits, std::vector<std::vector<std::uint8_t>>& elements)
        {
            ValueReader reader(bytes, limits);
            std::uint32_t count = 0;
            ValueStatus status = reader.read_array_length(count);
            if (!status.succeeded()) return status;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const std::size_t start = reader.offset();
                status = skip_value(reader, element);
                if (!status.succeeded()) return status;
                elements.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                                      bytes.begin() + static_cast<std::ptrdiff_t>(reader.offset()));
            }
            if (!reader.at_end()) return reader.failure(ValueErrorCode::InvalidValue, "trailing array bytes");
            return ValueStatus::success();
        }

        bool matches_element_id(const std::vector<std::uint8_t>& bytes, const TypeRegistry& types,
                                const ValueTypeDesc& element, const PropertyPathPart& selector,
                                ValueLimits limits)
        {
            const TypeDesc* description = types.find(element.stable_name);
            if (description == nullptr) return false;
            const auto property = std::find_if(description->properties.begin(), description->properties.end(),
                [&selector](const PropertyDesc& item) { return item.name == selector.identity_property; });
            if (property == description->properties.end() || property->value_type.kind != ValueKind::Utf8)
                return false;
            std::vector<FieldFrame> fields;
            if (!parse_fields(bytes, limits, fields).succeeded()) return false;
            const auto found = std::find_if(fields.begin(), fields.end(),
                [&selector](const FieldFrame& item) { return item.name == selector.identity_property; });
            if (found == fields.end()) return false;
            ValueReader reader(found->bytes, limits);
            std::string value;
            return reader.read_utf8(value).succeeded() && reader.at_end() && value == selector.name;
        }

        AssetStatus walk(const TypeRegistry& types, const ValueTypeDesc& type,
                         const std::vector<std::uint8_t>& bytes, const PropertyPath& path,
                         std::size_t position, const std::vector<std::uint8_t>* replacement,
                         ValueLimits limits, const std::string& location,
                         PropertyAccess& access, std::vector<std::uint8_t>& output)
        {
            if (position >= path.size()) return error(location, "property path ends before a value");
            const PropertyPathPart& part = path[position];
            const std::string next_location = append_path(location, part);
            if (type.kind == ValueKind::Struct)
            {
                if (part.kind != PropertyPathKind::Field)
                    return error(next_location, "expected a reflected field");
                const TypeDesc* description = types.find(type.stable_name);
                if (description == nullptr) return error(next_location, "reflected struct schema is absent");
                const auto property = std::find_if(description->properties.begin(), description->properties.end(),
                    [&part](const PropertyDesc& item) { return item.name == part.name; });
                if (property == description->properties.end())
                    return error(next_location, "property is not in the frozen schema");
                if (replacement != nullptr && !property_is_editable(*property))
                    return error(next_location, "property is read-only", AssetErrorCode::ReadOnly);
                std::vector<FieldFrame> fields;
                const ValueStatus parsed = parse_fields(bytes, limits, fields);
                if (!parsed.succeeded()) return value_error(parsed);
                const auto field = std::find_if(fields.begin(), fields.end(),
                    [&part](const FieldFrame& item) { return item.name == part.name; });
                if (field == fields.end()) return error(next_location, "property data is missing");
                access.property = *property;
                if (position + 1 == path.size())
                {
                    access.value_type = property->value_type;
                    access.value_bytes = field->bytes;
                    if (replacement != nullptr) field->bytes = *replacement;
                }
                else
                {
                    std::vector<std::uint8_t> nested;
                    AssetStatus status = walk(types, property->value_type, field->bytes, path, position + 1,
                                              replacement, limits, next_location, access, nested);
                    if (!status.succeeded()) return status;
                    field->bytes = std::move(nested);
                }
                const ValueStatus written = write_fields(std::move(fields), limits, output);
                return written.succeeded() ? AssetStatus::success() : value_error(written);
            }
            if (type.kind == ValueKind::Array)
            {
                if (type.arguments.size() != 1 || (part.kind != PropertyPathKind::Index &&
                    part.kind != PropertyPathKind::ElementId))
                    return error(next_location, "expected an array index or stable element ID");
                std::vector<std::vector<std::uint8_t>> elements;
                const ValueStatus split = split_array(bytes, type.arguments[0], limits, elements);
                if (!split.succeeded()) return value_error(split);
                std::size_t selected = part.index;
                if (part.kind == PropertyPathKind::ElementId)
                {
                    selected = elements.size();
                    for (std::size_t index = 0; index < elements.size(); ++index)
                        if (matches_element_id(elements[index], types, type.arguments[0], part, limits))
                        { selected = index; break; }
                }
                if (selected >= elements.size()) return error(next_location, "array element is missing");
                if (position + 1 == path.size())
                {
                    access.value_type = type.arguments[0];
                    access.value_bytes = elements[selected];
                    if (replacement != nullptr) elements[selected] = *replacement;
                }
                else
                {
                    std::vector<std::uint8_t> nested;
                    AssetStatus status = walk(types, type.arguments[0], elements[selected], path,
                        position + 1, replacement, limits, next_location, access, nested);
                    if (!status.succeeded()) return status;
                    elements[selected] = std::move(nested);
                }
                ValueWriter writer(limits);
                ValueStatus status = writer.write_array_length(static_cast<std::uint32_t>(elements.size()));
                if (!status.succeeded()) return value_error(status);
                for (const auto& element : elements)
                    for (std::uint8_t byte : element)
                    {
                        status = writer.write_uint8(byte);
                        if (!status.succeeded()) return value_error(status);
                    }
                output = writer.bytes();
                return AssetStatus::success();
            }
            if (type.kind == ValueKind::Variant)
            {
                if (part.kind != PropertyPathKind::VariantBranch)
                    return error(next_location, "expected a variant branch tag");
                ValueReader reader(bytes, limits);
                std::string tag;
                const ValueStatus tag_status = reader.read_utf8(tag);
                if (!tag_status.succeeded()) return value_error(tag_status);
                if (tag != part.name) return error(next_location, "variant branch is not active");
                const auto branch = std::find_if(type.arguments.begin(), type.arguments.end(),
                    [&tag](const ValueTypeDesc& item) { return branch_tag(item) == tag; });
                if (branch == type.arguments.end()) return error(next_location, "unknown variant branch");
                std::vector<std::uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(reader.offset()),
                                                  bytes.end());
                if (position + 1 == path.size())
                {
                    access.value_type = *branch;
                    access.value_bytes = payload;
                    if (replacement != nullptr) payload = *replacement;
                }
                else
                {
                    std::vector<std::uint8_t> nested;
                    AssetStatus status = walk(types, *branch, payload, path, position + 1,
                        replacement, limits, next_location, access, nested);
                    if (!status.succeeded()) return status;
                    payload = std::move(nested);
                }
                ValueWriter writer(limits);
                ValueStatus status = writer.write_utf8(tag);
                if (!status.succeeded()) return value_error(status);
                for (std::uint8_t byte : payload)
                {
                    status = writer.write_uint8(byte);
                    if (!status.succeeded()) return value_error(status);
                }
                output = writer.bytes();
                return AssetStatus::success();
            }
            return error(next_location, "property path cannot descend into a scalar value");
        }
    } // namespace

    AssetResult<PropertyAccess> access_property(const TypeRegistry& types, const TypeDesc& root_type,
        const std::vector<std::uint8_t>& root_bytes, const PropertyPath& path,
        const std::vector<std::uint8_t>* replacement, ValueLimits limits)
    {
        if (!types.frozen() || path.empty())
            return AssetResult<PropertyAccess>(error({}, "frozen schema and nonempty property path required"));
        PropertyAccess result;
        ValueTypeDesc root;
        root.kind = ValueKind::Struct;
        root.stable_name = root_type.name;
        AssetStatus status = walk(types, root, root_bytes, path, 0, replacement, limits, {},
                                  result, result.root_bytes);
        return status.succeeded() ? AssetResult<PropertyAccess>(std::move(result)) :
                                    AssetResult<PropertyAccess>(status);
    }

    std::string format_property_path(const PropertyPath& path)
    {
        std::string result;
        for (const PropertyPathPart& part : path) result = append_path(result, part);
        return result;
    }
} // namespace toy3d
