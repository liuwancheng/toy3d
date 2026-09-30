#include "material/material_asset.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <map>

#include "text/utf8.h"

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const std::string& path, const std::string& message)
        {
            return {AssetErrorCode::Value, {}, {}, "type_data", path, message, {}};
        }

        bool canonical_name(const std::string& name)
        {
            if (name.empty() || name.size() > 256u) return false;
            for (std::size_t i = 0; i < name.size(); ++i)
            {
                const char c = name[i];
                const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
                if (!letter && !(i > 0 && c >= '0' && c <= '9')) return false;
            }
            return true;
        }

        AssetStatus validate_reference(const AssetRef& reference, const char* expected,
            const std::string& path, const AssetIndex* index)
        {
            if (!reference.asset_id.valid() || reference.subresource_id.valid() ||
                reference.expected_type != expected || reference.strength != AssetRefStrength::Strong)
                return invalid(path, "Expected a strong root asset reference of type " + std::string(expected));
            return index ? index->resolve(reference, path) : AssetStatus::success();
        }

        AssetStatus validate_overrides(const std::vector<MaterialParameterOverride>& values, const AssetIndex* index)
        {
            if (values.size() > maximum_material_overrides) return invalid("overrides", "Too many material overrides.");
            std::set<std::string> names;
            for (const auto& item : values)
            {
                const std::string path = "overrides." + item.name;
                if (!canonical_name(item.name) || !names.insert(item.name).second)
                    return invalid(path, "Invalid or duplicate canonical parameter name.");
                // C++17 get_if makes validation of each persisted value branch explicit.
                bool finite = true;
                if (const auto* value = std::get_if<float>(&item.value)) finite = std::isfinite(*value);
                else if (const auto* value = std::get_if<Vector2>(&item.value)) finite = is_finite(*value);
                else if (const auto* value = std::get_if<Vector3>(&item.value)) finite = is_finite(*value);
                else if (const auto* value = std::get_if<Vector4>(&item.value)) finite = is_finite(*value);
                else if (const auto* value = std::get_if<AssetRef>(&item.value))
                {
                    const AssetStatus valid = validate_reference(*value, "toy3d.Texture2DAssetData", path, index);
                    if (!valid.succeeded()) return valid;
                }
                else if (const auto* value = std::get_if<MaterialSamplerPreset>(&item.value))
                {
                    if (*value < MaterialSamplerPreset::PointClamp || *value > MaterialSamplerPreset::ShadowCompareClamp)
                        return invalid(path, "Unknown material sampler preset.");
                }
                else return invalid(path, "Material parameter has no supported value.");
                if (!finite) return invalid(path, "Material values must be finite.");
            }
            return AssetStatus::success();
        }

        void append_texture_dependencies(const std::vector<MaterialParameterOverride>& values, std::vector<AssetRef>& result)
        {
            for (const auto& item : values)
            {
                // get_if extracts strong references without interpreting numeric branches.
                const auto* reference = std::get_if<AssetRef>(&item.value);
                if (reference && std::none_of(result.begin(), result.end(), [&](const AssetRef& existing)
                    { return existing.asset_id == reference->asset_id; })) result.push_back(*reference);
            }
            std::sort(result.begin(), result.end(), [](const AssetRef& a, const AssetRef& b) { return a.asset_id < b.asset_id; });
        }

        template <typename T>
        AssetResult<std::vector<std::uint8_t>> encode(const TypeRegistry& types, const AssetId& id,
            const std::string& type_name, const T& data)
        {
            const TypeDesc* type = types.find(type_name);
            if (!types.frozen() || !type)
                return AssetResult<std::vector<std::uint8_t>>(invalid({}, "Material types must be registered and frozen."));
            T sorted = data;
            std::sort(sorted.overrides.begin(), sorted.overrides.end(),
                [](const MaterialParameterOverride& a, const MaterialParameterOverride& b) { return a.name < b.name; });
            ValueWriter writer;
            const ValueStatus encoded = encode_value(writer, sorted);
            if (!encoded.succeeded()) return AssetResult<std::vector<std::uint8_t>>(invalid(encoded.property_path, encoded.message));
            AssetFileIndex file;
            file.asset_id = id;
            file.root_type = type_name;
            file.schema_version = type->schema_version;
            file.dependencies = material_asset_dependencies(sorted);
            return encode_asset_file(std::move(file), {{"type_data", 1, true, writer.bytes()}});
        }

        template <typename T>
        AssetStatus check_dependencies(const FileSystem& files, const VirtualPath& path, const T& data)
        {
            const auto inspected = inspect_asset(files, path);
            if (!inspected.succeeded()) return inspected.status();
            const auto expected = material_asset_dependencies(data);
            auto actual = inspected.value().dependencies;
            std::sort(actual.begin(), actual.end(), [](const AssetRef& a, const AssetRef& b) { return a.asset_id < b.asset_id; });
            bool matches = expected.size() == actual.size();
            for (std::size_t i = 0; i < actual.size(); ++i)
            {
                if (!matches) break;
                if (!(actual[i].asset_id == expected[i].asset_id) || actual[i].subresource_id.valid() ||
                    actual[i].expected_type != expected[i].expected_type || actual[i].strength != expected[i].strength)
                    matches = false;
            }
            if (!matches)
                return {AssetErrorCode::Value, inspected.value().asset_id, path.utf8(), "type_data", "dependencies",
                    "Material dependency index differs from typed references.", {}};
            return AssetStatus::success();
        }
    }

    bool is_material_asset_type(const std::string& type)
    {
        return type == "toy3d.MaterialAssetData" || type == "toy3d.MaterialInstanceAssetData";
    }

    bool parse_material_sampler_preset(const std::string& name, MaterialSamplerPreset& output)
    {
        for (std::uint32_t i = 0; i < shader::sampler_preset_count; ++i)
            if (name == shader::sampler_preset_name(i))
            { output = static_cast<MaterialSamplerPreset>(i); return true; }
        return false;
    }

    // --------------------------------------------------------------------------
    // MaterialAssetHierarchy: Bounded root-to-leaf authoring value resolution
    // --------------------------------------------------------------------------
    std::vector<MaterialParameterOverride> MaterialAssetHierarchy::effective_overrides(
        const shader::ShaderParameterSchema& schema) const
    {
        std::map<std::string, MaterialParameterOverride> values;
        for (const auto& layer : layers)
            for (const auto& value : layer.overrides)
                if (material_override_matches_schema(value, schema)) values[value.name] = value;
        std::vector<MaterialParameterOverride> result;
        for (const auto& value : values) result.push_back(value.second);
        return result;
    }

    AssetResult<MaterialAssetHierarchy> read_material_hierarchy(const TypeRegistry& types,
        const FileSystem& files, const AssetIndex& index, const AssetRef& leaf)
    {
        MaterialAssetHierarchy result;
        AssetRef current = leaf;
        std::set<AssetId> visited;
        while (result.layers.size() < maximum_material_parent_depth)
        {
            if (!is_material_asset_type(current.expected_type) || current.subresource_id.valid() ||
                current.strength != AssetRefStrength::Strong)
                return AssetResult<MaterialAssetHierarchy>(invalid("parent", "Expected a strong root Material/Instance reference."));
            const auto resolved = index.resolve(current, "parent");
            if (!resolved.succeeded()) return AssetResult<MaterialAssetHierarchy>(resolved);
            if (!visited.insert(current.asset_id).second)
                return AssetResult<MaterialAssetHierarchy>(AssetStatus{AssetErrorCode::DependencyCycle,
                    current.asset_id, {}, {}, "parent", "Material Parent chain contains a cycle.", {}});
            const auto* location = index.find(current.asset_id);
            const auto published = inspect_asset(files, location->path);
            if (!published.succeeded()) return AssetResult<MaterialAssetHierarchy>(published.status());
            if (!(published.value().asset_id == current.asset_id) || published.value().root_type != current.expected_type)
                return AssetResult<MaterialAssetHierarchy>(invalid("parent", "Published Material identity differs from the catalog."));
            if (current.expected_type == "toy3d.MaterialAssetData")
            {
                const auto read = read_material_asset(types, files, location->path, result.root, &index);
                if (!read.succeeded()) return AssetResult<MaterialAssetHierarchy>(read);
                result.layers.push_back({current, result.root.overrides});
                std::reverse(result.layers.begin(), result.layers.end());
                return AssetResult<MaterialAssetHierarchy>(std::move(result));
            }
            MaterialInstanceAssetData child;
            const auto read = read_material_instance_asset(types, files, location->path, child, &index);
            if (!read.succeeded()) return AssetResult<MaterialAssetHierarchy>(read);
            result.layers.push_back({current, std::move(child.overrides)});
            current = child.parent;
        }
        return AssetResult<MaterialAssetHierarchy>(invalid("parent", "Material Parent chain exceeds the depth limit (64)."));
    }

    AssetStatus validate_material_asset(const MaterialAssetData& data, const AssetIndex* index)
    {
        if (data.shader_name.empty() || data.shader_name.size() > 1024u || !is_valid_utf8(data.shader_name) ||
            data.shader_name.front() == '/' || data.shader_name.back() == '/' ||
            data.shader_name.find("..") != std::string::npos || data.shader_name.find('\\') != std::string::npos ||
            std::any_of(data.shader_name.begin(), data.shader_name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            return invalid("shader_name", "Enter a valid logical Shader name.");
        return validate_overrides(data.overrides, index);
    }

    AssetStatus validate_material_instance_asset(const MaterialInstanceAssetData& data, const AssetIndex* index)
    {
        if (!is_material_asset_type(data.parent.expected_type))
            return invalid("parent", "Parent must be a Material or Material Instance asset.");
        const AssetStatus parent = validate_reference(data.parent, data.parent.expected_type.c_str(), "parent", index);
        return parent.succeeded() ? validate_overrides(data.overrides, index) : parent;
    }

    bool material_override_matches_schema(const MaterialParameterOverride& item, const shader::ShaderParameterSchema& schema)
    {
        for (const auto& buffer : schema.constant_buffers)
        {
            if (buffer.group != shader::BindingGroup::Material) continue;
            for (const auto& member : buffer.members)
            {
                if (member.name != item.name || member.array_count != 1u || member.matrix_stride != 0u) continue;
                // holds_alternative tests the closed C++17 value shape against the author schema.
                return (member.type == shader::ShaderValueType::Float32 && std::holds_alternative<float>(item.value)) ||
                    (member.type == shader::ShaderValueType::Float32x2 && std::holds_alternative<Vector2>(item.value)) ||
                    (member.type == shader::ShaderValueType::Float32x3 && std::holds_alternative<Vector3>(item.value)) ||
                    (member.type == shader::ShaderValueType::Float32x4 && std::holds_alternative<Vector4>(item.value));
            }
        }
        for (const auto& resource : schema.resources)
        {
            if (resource.group != shader::BindingGroup::Material || resource.name != item.name || resource.array_count != 1u) continue;
            // Resource variants are validated against their logical resource kind, never native slots.
            return (resource.resource_kind == shader::ResourceKind::Texture2D && std::holds_alternative<AssetRef>(item.value)) ||
                (resource.resource_kind == shader::ResourceKind::Sampler && std::holds_alternative<MaterialSamplerPreset>(item.value));
        }
        return false;
    }

    AssetStatus validate_material_overrides_schema(const std::vector<MaterialParameterOverride>& values,
        const shader::ShaderParameterSchema& schema)
    {
        std::string error;
        if (!shader::validate_shader_parameter_schema(schema, error)) return invalid("shader", error);
        const AssetStatus valid = validate_overrides(values, nullptr);
        if (!valid.succeeded()) return valid;
        for (const auto& item : values)
            if (!material_override_matches_schema(item, schema))
                return invalid("overrides." + item.name, "Unknown parameter or incompatible override type.");
        return AssetStatus::success();
    }

    std::vector<AssetRef> material_asset_dependencies(const MaterialAssetData& data)
    {
        std::vector<AssetRef> result;
        append_texture_dependencies(data.overrides, result);
        return result;
    }

    std::vector<AssetRef> material_asset_dependencies(const MaterialInstanceAssetData& data)
    {
        std::vector<AssetRef> result{data.parent};
        append_texture_dependencies(data.overrides, result);
        return result;
    }

    AssetResult<std::vector<std::uint8_t>> encode_material_asset(const TypeRegistry& types,
        const AssetId& id, const MaterialAssetData& data, const AssetIndex* index)
    {
        const AssetStatus valid = validate_material_asset(data, index);
        if (!valid.succeeded()) return AssetResult<std::vector<std::uint8_t>>(valid);
        return encode(types, id, "toy3d.MaterialAssetData", data);
    }

    AssetResult<std::vector<std::uint8_t>> encode_material_instance_asset(const TypeRegistry& types,
        const AssetId& id, const MaterialInstanceAssetData& data, const AssetIndex* index)
    {
        const AssetStatus valid = validate_material_instance_asset(data, index);
        if (!valid.succeeded()) return AssetResult<std::vector<std::uint8_t>>(valid);
        return encode(types, id, "toy3d.MaterialInstanceAssetData", data);
    }

    AssetStatus read_material_asset(const TypeRegistry& types, const FileSystem& files,
        const VirtualPath& path, MaterialAssetData& output, const AssetIndex* index)
    {
        MaterialAssetData candidate;
        const AssetStatus loaded = load_asset(types, SchemaMigrationRegistry{}, files, path, "toy3d.MaterialAssetData", candidate,
            [&](const MaterialAssetData& value) { return validate_material_asset(value, index); });
        if (!loaded.succeeded()) return loaded;
        const AssetStatus dependencies = check_dependencies(files, path, candidate);
        if (!dependencies.succeeded()) return dependencies;
        output = std::move(candidate);
        return AssetStatus::success();
    }

    AssetStatus read_material_instance_asset(const TypeRegistry& types, const FileSystem& files,
        const VirtualPath& path, MaterialInstanceAssetData& output, const AssetIndex* index)
    {
        MaterialInstanceAssetData candidate;
        const AssetStatus loaded = load_asset(types, SchemaMigrationRegistry{}, files, path, "toy3d.MaterialInstanceAssetData", candidate,
            [&](const MaterialInstanceAssetData& value) { return validate_material_instance_asset(value, index); });
        if (!loaded.succeeded()) return loaded;
        const AssetStatus dependencies = check_dependencies(files, path, candidate);
        if (!dependencies.succeeded()) return dependencies;
        output = std::move(candidate);
        return AssetStatus::success();
    }
}
