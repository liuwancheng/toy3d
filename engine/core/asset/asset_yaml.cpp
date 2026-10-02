#include "asset_yaml.h"
#include "asset/yaml_validation.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "misc/utf8.h"
#include "serialization/reflected_value.h"

namespace toy3d
{
    namespace
    {
        constexpr std::uint32_t k_yaml_version = 2u;

        AssetStatus fail(const AssetId& id, const std::string& path, const std::string& message)
        {
            return {AssetErrorCode::InvalidFormat, id, {}, {}, path, message, {}};
        }

        bool read_reference(const YAML::Node& node, AssetRef& output)
        {
            if (!node.IsMap() || node.size() < 3u || node.size() > 4u || !node["asset_id"] || !node["expected_type"] ||
                !node["strength"])
            {
                return false;
            }
            for (const auto& entry : node)
            {
                const std::string key = entry.first.as<std::string>();
                if (key != "asset_id" && key != "subresource_id" && key != "expected_type" && key != "strength")
                {
                    return false;
                }
            }
            AssetRef candidate;
            if (!AssetId::parse(node["asset_id"].as<std::string>(), candidate.asset_id))
            {
                return false;
            }
            if (node["subresource_id"] &&
                !SubresourceId::parse(node["subresource_id"].as<std::string>(), candidate.subresource_id))
            {
                return false;
            }
            candidate.expected_type = node["expected_type"].as<std::string>();
            const std::string strength = node["strength"].as<std::string>();
            if (strength == "strong")
            {
                candidate.strength = AssetRefStrength::Strong;
            }
            else if (strength == "weak")
            {
                candidate.strength = AssetRefStrength::Weak;
            }
            else if (strength == "deferred")
            {
                candidate.strength = AssetRefStrength::Deferred;
            }
            else
            {
                return false;
            }
            if (candidate.expected_type.empty())
            {
                return false;
            }
            output = std::move(candidate);
            return true;
        }

        YAML::Node write_reference(const AssetRef& reference)
        {
            YAML::Node node(YAML::NodeType::Map);
            node["asset_id"] = reference.asset_id.hex();
            if (reference.subresource_id.valid())
            {
                node["subresource_id"] = reference.subresource_id.hex();
            }
            node["expected_type"] = reference.expected_type;
            switch (reference.strength)
            {
            case AssetRefStrength::Strong:
                node["strength"] = "strong";
                break;
            case AssetRefStrength::Weak:
                node["strength"] = "weak";
                break;
            case AssetRefStrength::Deferred:
                node["strength"] = "deferred";
                break;
            }
            return node;
        }

        const TypeDesc* nested_type(const TypeRegistry& types, const ValueTypeDesc& type)
        {
            return types.find(type.stable_name);
        }

        std::string branch_tag(const ValueTypeDesc& type)
        {
            if (!type.stable_name.empty())
            {
                return type.stable_name;
            }
            switch (type.kind)
            {
            case ValueKind::Bool:
                return "Bool";
            case ValueKind::Float32:
                return "Float32";
            case ValueKind::Vector2:
                return "Vector2";
            case ValueKind::Vector3:
                return "Vector3";
            case ValueKind::Vector4:
                return "Vector4";
            case ValueKind::AssetRef:
                return "AssetRef";
            default:
                return {};
            }
        }

        bool binary_to_yaml(ValueReader& reader, const ValueTypeDesc& type, const TypeRegistry& types,
                            YAML::Node& output, std::size_t depth, const ValueLimits& limits);
        bool yaml_to_binary(const YAML::Node& node, const ValueTypeDesc& type, const TypeRegistry& types,
                            ValueWriter& writer, std::size_t depth, const ValueLimits& limits);

        bool read_struct(ValueReader& reader, const TypeDesc& type, const TypeRegistry& types, YAML::Node& output,
                         std::size_t depth, const ValueLimits& limits)
        {
            std::uint32_t count = 0;
            if (!reader.read_array_length(count).succeeded())
            {
                return false;
            }
            std::map<std::string, YAML::Node> sorted;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                std::string name;
                std::uint8_t flags = 0;
                std::vector<std::uint8_t> bytes;
                if (!reader.read_utf8(name).succeeded() || !reader.read_uint8(flags).succeeded() ||
                    !reader.read_blob(bytes).succeeded() || flags != 1u || sorted.count(name) != 0u)
                {
                    return false;
                }
                const auto found = std::find_if(type.properties.begin(), type.properties.end(),
                                                [&name](const PropertyDesc& property)
                                                {
                                                    return property.name == name;
                                                });
                if (found == type.properties.end() || !property_is_persisted(*found))
                {
                    return false;
                }
                ValueReader field(bytes, limits);
                YAML::Node value;
                if (!binary_to_yaml(field, found->value_type, types, value, depth + 1u, limits) || !field.at_end())
                {
                    return false;
                }
                sorted.emplace(name, value);
            }
            YAML::Node result(YAML::NodeType::Map);
            for (const PropertyDesc& property : type.properties)
            {
                if (property_is_persisted(property) && sorted.count(property.name) == 0u)
                {
                    return false;
                }
            }
            for (const auto& entry : sorted)
            {
                result[entry.first] = entry.second;
            }
            output = result;
            return true;
        }

        bool write_struct(const YAML::Node& node, const TypeDesc& type, const TypeRegistry& types, ValueWriter& writer,
                          std::size_t depth, const ValueLimits& limits)
        {
            if (!node.IsMap())
            {
                return false;
            }
            std::vector<const PropertyDesc*> fields;
            for (const PropertyDesc& property : type.properties)
            {
                if (property_is_persisted(property))
                {
                    fields.push_back(&property);
                }
            }
            if (node.size() != fields.size() ||
                !writer.write_array_length(static_cast<std::uint32_t>(fields.size())).succeeded())
            {
                return false;
            }
            std::sort(fields.begin(), fields.end(),
                      [](const PropertyDesc* left, const PropertyDesc* right)
                      {
                          return left->name < right->name;
                      });
            for (const PropertyDesc* property : fields)
            {
                if (!node[property->name])
                {
                    return false;
                }
                ValueWriter field(limits);
                if (!yaml_to_binary(node[property->name], property->value_type, types, field, depth + 1u, limits) ||
                    !writer.write_utf8(property->name).succeeded() || !writer.write_uint8(1u).succeeded() ||
                    !writer.write_blob(field.bytes()).succeeded())
                {
                    return false;
                }
            }
            return true;
        }

        bool read_floats(ValueReader& reader, YAML::Node& node, std::size_t count)
        {
            YAML::Node values(YAML::NodeType::Sequence);
            for (std::size_t index = 0; index < count; ++index)
            {
                float value = 0.0f;
                if (!reader.read_float32(value).succeeded())
                {
                    return false;
                }
                values.push_back(value);
            }
            node = values;
            return true;
        }

        bool write_floats(const YAML::Node& node, ValueWriter& writer, std::size_t count)
        {
            if (!node.IsSequence() || node.size() != count)
            {
                return false;
            }
            for (const auto& item : node)
            {
                const float value = item.as<float>();
                if (!std::isfinite(value) || !writer.write_float32(value).succeeded())
                {
                    return false;
                }
            }
            return true;
        }

        bool binary_to_yaml(ValueReader& reader, const ValueTypeDesc& type, const TypeRegistry& types,
                            YAML::Node& output, std::size_t depth, const ValueLimits& limits)
        {
            if (depth > limits.max_depth)
            {
                return false;
            }
            switch (type.kind)
            {
            case ValueKind::Bool:
            {
                bool value = false;
                if (!reader.read_bool(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::Int8:
            {
                std::int8_t value = 0;
                if (!reader.read_int8(value).succeeded())
                {
                    return false;
                }
                output = static_cast<int>(value);
                return true;
            }
            case ValueKind::UInt8:
            {
                std::uint8_t value = 0;
                if (!reader.read_uint8(value).succeeded())
                {
                    return false;
                }
                output = static_cast<unsigned>(value);
                return true;
            }
            case ValueKind::Int16:
            {
                std::int16_t value = 0;
                if (!reader.read_int16(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::UInt16:
            {
                std::uint16_t value = 0;
                if (!reader.read_uint16(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::Int32:
            {
                std::int32_t value = 0;
                if (!reader.read_int32(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::UInt32:
            {
                std::uint32_t value = 0;
                if (!reader.read_uint32(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::Int64:
            {
                std::int64_t value = 0;
                if (!reader.read_int64(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::UInt64:
            {
                std::uint64_t value = 0;
                if (!reader.read_uint64(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::Float32:
            {
                float value = 0.0f;
                if (!reader.read_float32(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::Float64:
            {
                double value = 0.0;
                if (!reader.read_float64(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::Utf8:
            {
                std::string value;
                if (!reader.read_utf8(value).succeeded())
                {
                    return false;
                }
                output = value;
                return true;
            }
            case ValueKind::Enum:
            {
                std::int64_t value = 0;
                const TypeDesc* description = nested_type(types, type);
                if (!description || !reader.read_int64(value).succeeded())
                {
                    return false;
                }
                for (const auto& item : description->enum_values)
                {
                    if (item.value == value)
                    {
                        output = item.name;
                        return true;
                    }
                }
                return false;
            }
            case ValueKind::ReflectedStruct:
            {
                ReflectedValue value;
                if (!decode_value(reader, value).succeeded())
                {
                    return false;
                }
                const auto* description = types.find(value.type);
                if (!description || description->schema_version != value.schema_version ||
                    !description->enum_values.empty())
                {
                    return false;
                }
                ValueReader fields(value.bytes, reader.child_limits());
                YAML::Node properties;
                if (!read_struct(fields, *description, types, properties, depth + 1u, limits) || !fields.at_end())
                {
                    return false;
                }
                output = YAML::Node(YAML::NodeType::Map);
                output["type"] = value.type;
                output["schema_version"] = value.schema_version;
                output["value"] = properties;
                return true;
            }
            case ValueKind::Struct:
            {
                const TypeDesc* description = nested_type(types, type);
                return description && read_struct(reader, *description, types, output, depth, limits);
            }
            case ValueKind::AssetRef:
            {
                AssetRef reference;
                if (!decode_value(reader, reference).succeeded())
                {
                    return false;
                }
                output = write_reference(reference);
                return true;
            }
            case ValueKind::Vector2:
                return read_floats(reader, output, 2u);
            case ValueKind::Vector3:
                return read_floats(reader, output, 3u);
            case ValueKind::Vector4:
            case ValueKind::Quaternion:
                return read_floats(reader, output, 4u);
            case ValueKind::Matrix3:
                return read_floats(reader, output, 9u);
            case ValueKind::Matrix4:
                return read_floats(reader, output, 16u);
            case ValueKind::Transform:
            {
                YAML::Node translation, rotation, scale;
                if (!read_floats(reader, translation, 3u) || !read_floats(reader, rotation, 4u) ||
                    !read_floats(reader, scale, 3u))
                {
                    return false;
                }
                output = YAML::Node(YAML::NodeType::Map);
                output["translation"] = translation;
                output["rotation"] = rotation;
                output["scale"] = scale;
                return true;
            }
            case ValueKind::Array:
            {
                if (type.arguments.size() != 1u)
                {
                    return false;
                }
                std::uint32_t count = 0;
                if (!reader.read_array_length(count).succeeded())
                {
                    return false;
                }
                YAML::Node values(YAML::NodeType::Sequence);
                for (std::uint32_t index = 0; index < count; ++index)
                {
                    YAML::Node item;
                    if (!binary_to_yaml(reader, type.arguments[0], types, item, depth + 1u, limits))
                    {
                        return false;
                    }
                    values.push_back(item);
                }
                output = values;
                return true;
            }
            case ValueKind::Variant:
            {
                std::string tag;
                if (!reader.read_utf8(tag).succeeded())
                {
                    return false;
                }
                for (const ValueTypeDesc& branch : type.arguments)
                {
                    if (branch_tag(branch) != tag)
                    {
                        continue;
                    }
                    YAML::Node value;
                    if (!binary_to_yaml(reader, branch, types, value, depth + 1u, limits))
                    {
                        return false;
                    }
                    output = YAML::Node(YAML::NodeType::Map);
                    output["type"] = tag;
                    output["value"] = value;
                    return true;
                }
                return false;
            }
            default:
                return false;
            }
        }

        bool yaml_to_binary(const YAML::Node& node, const ValueTypeDesc& type, const TypeRegistry& types,
                            ValueWriter& writer, std::size_t depth, const ValueLimits& limits)
        {
            if (depth > limits.max_depth)
            {
                return false;
            }
            switch (type.kind)
            {
            case ValueKind::Bool:
                return node.IsScalar() && writer.write_bool(node.as<bool>()).succeeded();
            case ValueKind::Int8:
            {
                if (!node.IsScalar())
                {
                    return false;
                }
                const int value = node.as<int>();
                return value >= std::numeric_limits<std::int8_t>::min() &&
                       value <= std::numeric_limits<std::int8_t>::max() &&
                       writer.write_int8(static_cast<std::int8_t>(value)).succeeded();
            }
            case ValueKind::UInt8:
            {
                if (!node.IsScalar())
                {
                    return false;
                }
                const unsigned value = node.as<unsigned>();
                return value <= std::numeric_limits<std::uint8_t>::max() &&
                       writer.write_uint8(static_cast<std::uint8_t>(value)).succeeded();
            }
            case ValueKind::Int16:
                return node.IsScalar() && writer.write_int16(node.as<std::int16_t>()).succeeded();
            case ValueKind::UInt16:
                return node.IsScalar() && writer.write_uint16(node.as<std::uint16_t>()).succeeded();
            case ValueKind::Int32:
                return node.IsScalar() && writer.write_int32(node.as<std::int32_t>()).succeeded();
            case ValueKind::UInt32:
                return node.IsScalar() && writer.write_uint32(node.as<std::uint32_t>()).succeeded();
            case ValueKind::Int64:
                return node.IsScalar() && writer.write_int64(node.as<std::int64_t>()).succeeded();
            case ValueKind::UInt64:
                return node.IsScalar() && writer.write_uint64(node.as<std::uint64_t>()).succeeded();
            case ValueKind::Float32:
            {
                if (!node.IsScalar())
                {
                    return false;
                }
                const float value = node.as<float>();
                return std::isfinite(value) && writer.write_float32(value).succeeded();
            }
            case ValueKind::Float64:
            {
                if (!node.IsScalar())
                {
                    return false;
                }
                const double value = node.as<double>();
                return std::isfinite(value) && writer.write_float64(value).succeeded();
            }
            case ValueKind::Utf8:
                return node.IsScalar() && writer.write_utf8(node.as<std::string>()).succeeded();
            case ValueKind::Enum:
            {
                if (!node.IsScalar())
                {
                    return false;
                }
                const TypeDesc* description = nested_type(types, type);
                if (!description)
                {
                    return false;
                }
                const std::string name = node.as<std::string>();
                for (const auto& item : description->enum_values)
                {
                    if (item.name == name)
                    {
                        return writer.write_int64(item.value).succeeded();
                    }
                }
                return false;
            }
            case ValueKind::ReflectedStruct:
            {
                if (!node.IsMap() || node.size() != 3u || !node["type"] || !node["schema_version"] || !node["value"])
                {
                    return false;
                }
                ReflectedValue value;
                value.type = node["type"].as<std::string>();
                value.schema_version = node["schema_version"].as<std::uint32_t>();
                const auto* description = types.find(value.type);
                if (!description || description->schema_version != value.schema_version ||
                    !description->enum_values.empty())
                {
                    return false;
                }
                ValueWriter fields(writer.child_limits());
                if (!write_struct(node["value"], *description, types, fields, depth + 1u, limits))
                {
                    return false;
                }
                value.bytes = fields.bytes();
                return encode_value(writer, value).succeeded();
            }
            case ValueKind::Struct:
            {
                const TypeDesc* description = nested_type(types, type);
                return description && write_struct(node, *description, types, writer, depth, limits);
            }
            case ValueKind::AssetRef:
            {
                AssetRef reference;
                return read_reference(node, reference) && encode_value(writer, reference).succeeded();
            }
            case ValueKind::Vector2:
                return write_floats(node, writer, 2u);
            case ValueKind::Vector3:
                return write_floats(node, writer, 3u);
            case ValueKind::Vector4:
            case ValueKind::Quaternion:
                return write_floats(node, writer, 4u);
            case ValueKind::Matrix3:
                return write_floats(node, writer, 9u);
            case ValueKind::Matrix4:
                return write_floats(node, writer, 16u);
            case ValueKind::Transform:
                return node.IsMap() && node.size() == 3u && write_floats(node["translation"], writer, 3u) &&
                       write_floats(node["rotation"], writer, 4u) && write_floats(node["scale"], writer, 3u);
            case ValueKind::Array:
            {
                if (!node.IsSequence() || type.arguments.size() != 1u || node.size() > limits.max_array_elements ||
                    !writer.write_array_length(static_cast<std::uint32_t>(node.size())).succeeded())
                {
                    return false;
                }
                for (const auto& item : node)
                {
                    if (!yaml_to_binary(item, type.arguments[0], types, writer, depth + 1u, limits))
                    {
                        return false;
                    }
                }
                return true;
            }
            case ValueKind::Variant:
            {
                if (!node.IsMap() || node.size() != 2u || !node["type"] || !node["value"])
                {
                    return false;
                }
                const std::string tag = node["type"].as<std::string>();
                for (const ValueTypeDesc& branch : type.arguments)
                {
                    if (branch_tag(branch) == tag)
                    {
                        return writer.write_utf8(tag).succeeded() &&
                               yaml_to_binary(node["value"], branch, types, writer, depth + 1u, limits);
                    }
                }
                return false;
            }
            default:
                return false;
            }
        }
    } // namespace

    namespace
    {
        bool collect_references(const YAML::Node& node, const ValueTypeDesc& shape, const TypeRegistry& types,
                                std::vector<AssetRef>& references, std::uint32_t depth)
        {
            if (depth > 64u || references.size() > 1000000u)
            {
                return false;
            }
            if (shape.kind == ValueKind::AssetRef)
            {
                AssetRef reference;
                if (!read_reference(node, reference))
                {
                    return false;
                }
                references.push_back(std::move(reference));
            }
            else if (shape.kind == ValueKind::Struct)
            {
                const auto* type = types.find(shape.stable_name);
                if (!type)
                {
                    return false;
                }
                for (const auto& property : type->properties)
                {
                    if (property_is_persisted(property) &&
                        !collect_references(node[property.name], property.value_type, types, references, depth + 1u))
                    {
                        return false;
                    }
                }
            }
            else if (shape.kind == ValueKind::ReflectedStruct)
            {
                return collect_references(node["value"], {ValueKind::Struct, node["type"].as<std::string>(), {}}, types,
                                          references, depth + 1u);
            }
            else if (shape.kind == ValueKind::Array)
            {
                for (const auto& element : node)
                {
                    if (!collect_references(element, shape.arguments.front(), types, references, depth + 1u))
                    {
                        return false;
                    }
                }
            }
            else if (shape.kind == ValueKind::Variant)
            {
                for (const auto& branch : shape.arguments)
                {
                    if (branch_tag(branch) == node["type"].as<std::string>())
                    {
                        return collect_references(node["value"], branch, types, references, depth + 1u);
                    }
                }
                return false;
            }
            return true;
        }
    } // namespace

    AssetResult<std::vector<AssetRef>> reflected_value_references(const TypeRegistry& types,
                                                                  const ReflectedValue& value)
    {
        ValueWriter writer;
        YAML::Node node;
        std::vector<AssetRef> references;
        if (!types.frozen() || !encode_value(writer, value).succeeded())
        {
            return AssetResult<std::vector<AssetRef>>(fail({}, {}, "Invalid reflected property value."));
        }
        ValueReader reader(writer.bytes());
        if (!binary_to_yaml(reader, {ValueKind::ReflectedStruct, {}, {}}, types, node, 0, {}) || !reader.at_end() ||
            !collect_references(node, {ValueKind::ReflectedStruct, {}, {}}, types, references, 0))
        {
            return AssetResult<std::vector<AssetRef>>(
                fail({}, {}, "Reflected properties do not match their registered schema."));
        }
        return AssetResult<std::vector<AssetRef>>(std::move(references));
    }

    AssetResult<std::vector<std::uint8_t>> encode_asset_yaml(const TypeRegistry& types,
                                                             const AssetYamlDocument& document, AssetFileLimits limits,
                                                             ValueLimits value_limits)
    {
        const AssetFileIndex& index = document.index;
        const TypeDesc* type = types.find(index.root_type);
        if (!types.frozen() || !type || !index.asset_id.valid() || index.schema_version != type->schema_version ||
            document.type_data.size() > value_limits.max_bytes)
        {
            return AssetResult<std::vector<std::uint8_t>>(fail(index.asset_id, {}, "invalid YAML asset input"));
        }
        ValueReader reader(document.type_data, value_limits);
        YAML::Node data;
        ValueTypeDesc root{ValueKind::Struct, index.root_type, {}};
        if (!binary_to_yaml(reader, root, types, data, 0u, value_limits) || !reader.at_end())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(index.asset_id, "data", "type data does not match schema"));
        }
        YAML::Node node(YAML::NodeType::Map);
        node["format_version"] = k_yaml_version;
        node["asset_id"] = index.asset_id.hex();
        node["root_type"] = index.root_type;
        node["schema_version"] = index.schema_version;
        YAML::Node dependencies(YAML::NodeType::Sequence);
        std::vector<AssetRef> sorted_dependencies = index.dependencies;
        std::sort(sorted_dependencies.begin(), sorted_dependencies.end(),
                  [](const AssetRef& left, const AssetRef& right)
                  {
                      return left.asset_id < right.asset_id;
                  });
        for (const AssetRef& reference : sorted_dependencies)
        {
            dependencies.push_back(write_reference(reference));
        }
        node["dependencies"] = dependencies;
        YAML::Node subresources(YAML::NodeType::Sequence);
        std::vector<AssetSubresource> sorted_subresources = index.subresources;
        std::sort(sorted_subresources.begin(), sorted_subresources.end(),
                  [](const AssetSubresource& left, const AssetSubresource& right)
                  {
                      return left.id < right.id;
                  });
        for (const AssetSubresource& item : sorted_subresources)
        {
            YAML::Node value(YAML::NodeType::Map);
            value["id"] = item.id.hex();
            value["type"] = item.type_name;
            subresources.push_back(value);
        }
        node["subresources"] = subresources;
        node["data"] = data;
        if (document.has_meta)
        {
            YAML::Node meta(YAML::NodeType::Map);
            meta["format_version"] = 1u;
            meta["size"] = document.meta_size;
            meta["sha256"] = sha256_to_hex(document.meta_hash);
            std::vector<std::string> names = document.meta_segments;
            std::sort(names.begin(), names.end());
            if (std::adjacent_find(names.begin(), names.end()) != names.end())
            {
                return AssetResult<std::vector<std::uint8_t>>(
                    fail(index.asset_id, "meta", "invalid meta segment names"));
            }
            YAML::Node sections(YAML::NodeType::Sequence);
            for (const std::string& name : names)
            {
                sections.push_back(name);
            }
            meta["required_segments"] = sections;
            node["meta"] = meta;
        }
        YAML::Emitter emitter;
        emitter.SetFloatPrecision(std::numeric_limits<float>::max_digits10);
        emitter.SetDoublePrecision(std::numeric_limits<double>::max_digits10);
        emitter << node;
        if (!emitter.good() || emitter.size() + 1u > limits.max_index_bytes)
        {
            return AssetResult<std::vector<std::uint8_t>>(fail(index.asset_id, {}, "YAML asset exceeds limit"));
        }
        std::vector<std::uint8_t> bytes(emitter.c_str(), emitter.c_str() + emitter.size());
        bytes.push_back('\n');
        return AssetResult<std::vector<std::uint8_t>>(std::move(bytes));
    }

    AssetResult<AssetYamlDocument> decode_asset_yaml(const TypeRegistry& types, const std::vector<std::uint8_t>& bytes,
                                                     AssetFileLimits limits, ValueLimits value_limits)
    {
        if (bytes.empty() || bytes.size() > limits.max_index_bytes ||
            !is_valid_utf8(std::string(bytes.begin(), bytes.end())))
        {
            return AssetResult<AssetYamlDocument>(fail({}, {}, "invalid YAML asset text"));
        }
        try
        {
            const std::vector<YAML::Node> documents = YAML::LoadAll(std::string(bytes.begin(), bytes.end()));
            if (documents.size() != 1u)
            {
                return AssetResult<AssetYamlDocument>(fail({}, {}, "asset must contain one YAML document"));
            }
            YAML::Node node = documents.front();
            std::size_t count = 0;
            std::set<std::pair<int, int>> seen_marks;
            std::string error;
            if (!node.IsMap() || !check_yaml_tree(node, 0u, count, seen_marks, value_limits, error) ||
                (node.size() != 7u && node.size() != 8u) || (node.size() == 8u && !node["meta"]) ||
                !node["format_version"] || node["format_version"].as<std::uint32_t>() != k_yaml_version ||
                !node["asset_id"] || !node["root_type"] || !node["schema_version"] || !node["dependencies"] ||
                !node["subresources"] || !node["data"])
            {
                return AssetResult<AssetYamlDocument>(
                    fail({}, {}, error.empty() ? "invalid YAML asset structure" : error));
            }
            AssetYamlDocument result;
            if (!AssetId::parse(node["asset_id"].as<std::string>(), result.index.asset_id))
            {
                return AssetResult<AssetYamlDocument>(fail({}, "asset_id", "invalid Asset ID"));
            }
            result.index.root_type = node["root_type"].as<std::string>();
            result.index.schema_version = node["schema_version"].as<std::uint32_t>();
            const TypeDesc* type = types.find(result.index.root_type);
            // Explicit Scene 5 -> 6 migration: this changes only the decoded candidate.
            // The original bytes remain available for Editor save conflict checks.
            if (type && result.index.root_type == "toy3d.SceneAssetData" && result.index.schema_version == 5u &&
                type->schema_version == 6u)
            {
                if (!node["data"].IsMap() || !node["data"]["actors"].IsSequence())
                {
                    return AssetResult<AssetYamlDocument>(fail(result.index.asset_id, "data", "Invalid legacy Scene."));
                }
                const std::map<std::string, std::string> classes = {{"EmptyActor", "toy3d.Actor"},
                                                                    {"Cube", "toy3d.StaticMeshActor"},
                                                                    {"Plane", "toy3d.StaticMeshActor"},
                                                                    {"StaticMesh", "toy3d.StaticMeshActor"},
                                                                    {"DirectionalLight", "toy3d.DirectionalLightActor"},
                                                                    {"PointLight", "toy3d.PointLightActor"},
                                                                    {"Camera", "toy3d.CameraActor"}};
                for (auto actor : node["data"]["actors"])
                {
                    if (!actor.IsMap() || actor.size() != 4u || !actor["kind"])
                    {
                        return AssetResult<AssetYamlDocument>(
                            fail(result.index.asset_id, "actors", "Invalid legacy Actor."));
                    }
                    const auto found = classes.find(actor["kind"].as<std::string>());
                    if (found == classes.end())
                    {
                        return AssetResult<AssetYamlDocument>(
                            fail(result.index.asset_id, "kind", "Unknown legacy Actor kind."));
                    }
                    actor["type"] = found->second;
                    actor["properties"]["type"] = "toy3d.ActorSettings";
                    actor["properties"]["schema_version"] = 1;
                    actor["properties"]["value"] = YAML::Node(YAML::NodeType::Map);
                }
                result.index.schema_version = 6u;
            }
            if (!types.frozen() || !type || result.index.schema_version != type->schema_version)
            {
                return AssetResult<AssetYamlDocument>(
                    fail(result.index.asset_id, "schema_version", "unsupported type schema"));
            }
            if (!node["dependencies"].IsSequence() || !node["subresources"].IsSequence() ||
                node["dependencies"].size() > limits.max_entries || node["subresources"].size() > limits.max_entries)
            {
                return AssetResult<AssetYamlDocument>(fail(result.index.asset_id, {}, "invalid asset lists"));
            }
            for (const auto& item : node["dependencies"])
            {
                AssetRef reference;
                if (!read_reference(item, reference))
                {
                    return AssetResult<AssetYamlDocument>(
                        fail(result.index.asset_id, "dependencies", "invalid reference"));
                }
                result.index.dependencies.push_back(std::move(reference));
            }
            std::set<AssetId> dependency_ids;
            for (const AssetRef& reference : result.index.dependencies)
            {
                if (!dependency_ids.insert(reference.asset_id).second)
                {
                    return AssetResult<AssetYamlDocument>(
                        fail(result.index.asset_id, "dependencies", "duplicate dependency Asset ID"));
                }
            }
            for (const auto& item : node["subresources"])
            {
                AssetSubresource subresource;
                if (!item.IsMap() || item.size() != 2u || !item["id"] || !item["type"] ||
                    !SubresourceId::parse(item["id"].as<std::string>(), subresource.id))
                {
                    return AssetResult<AssetYamlDocument>(
                        fail(result.index.asset_id, "subresources", "invalid subresource"));
                }
                subresource.type_name = item["type"].as<std::string>();
                result.index.subresources.push_back(std::move(subresource));
            }
            std::set<SubresourceId> subresource_ids;
            for (const AssetSubresource& subresource : result.index.subresources)
            {
                if (subresource.type_name.empty() || !subresource_ids.insert(subresource.id).second)
                {
                    return AssetResult<AssetYamlDocument>(
                        fail(result.index.asset_id, "subresources", "invalid or duplicate subresource"));
                }
            }
            ValueWriter writer(value_limits);
            ValueTypeDesc root{ValueKind::Struct, result.index.root_type, {}};
            if (!yaml_to_binary(node["data"], root, types, writer, 0u, value_limits))
            {
                return AssetResult<AssetYamlDocument>(
                    fail(result.index.asset_id, "data", "data does not match schema"));
            }
            result.type_data = writer.bytes();
            if (node["meta"])
            {
                const YAML::Node meta = node["meta"];
                if (!meta.IsMap() || meta.size() != 4u || !meta["format_version"] ||
                    meta["format_version"].as<std::uint32_t>() != 1u || !meta["size"] || !meta["sha256"] ||
                    !meta["required_segments"] || !meta["required_segments"].IsSequence())
                {
                    return AssetResult<AssetYamlDocument>(
                        fail(result.index.asset_id, "meta", "invalid meta declaration"));
                }
                result.meta_size = meta["size"].as<std::uint64_t>();
                const auto hash = sha256_from_hex(meta["sha256"].as<std::string>());
                // C++17 optional keeps an invalid digest distinct from an all-zero digest.
                if (!hash || result.meta_size == 0u || result.meta_size > limits.max_file_bytes)
                {
                    return AssetResult<AssetYamlDocument>(
                        fail(result.index.asset_id, "meta", "invalid meta digest or size"));
                }
                result.meta_hash = *hash;
                result.has_meta = true;
                std::set<std::string> sections;
                for (const auto& item : meta["required_segments"])
                {
                    if (!item.IsScalar() || !sections.insert(item.as<std::string>()).second)
                    {
                        return AssetResult<AssetYamlDocument>(
                            fail(result.index.asset_id, "meta", "invalid required segment"));
                    }
                }
                result.meta_segments.assign(sections.begin(), sections.end());
            }
            return AssetResult<AssetYamlDocument>(std::move(result));
        }
        catch (const YAML::Exception& error)
        {
            return AssetResult<AssetYamlDocument>(fail({}, {}, error.what()));
        }
        catch (const std::exception& error)
        {
            return AssetResult<AssetYamlDocument>(fail({}, {}, error.what()));
        }
    }
} // namespace toy3d
