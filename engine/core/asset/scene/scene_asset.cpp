#include "asset/scene/scene_asset.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "serialization/value_codec.h"

namespace toy3d
{
    namespace
    {
        constexpr const char* k_scene_type = "toy3d.SceneAssetData";
        constexpr std::size_t k_max_scene_actors = 100000u;

        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, "data", {}, message, {}};
        }

        bool valid_kind(const std::string& kind)
        {
            return kind == "EmptyActor" || kind == "Cube" || kind == "Plane" || kind == "StaticMesh" ||
                   kind == "DirectionalLight" || kind == "PointLight" || kind == "Camera";
        }

        std::vector<AssetRef> dependencies(const SceneAssetData& data, const TypeRegistry* types = nullptr)
        {
            std::vector<AssetRef> refs;
            const auto append = [&](const AssetRef& reference)
            {
                const auto found = std::find_if(refs.begin(), refs.end(),
                                                [&](const AssetRef& existing)
                                                {
                                                    return existing.asset_id == reference.asset_id;
                                                });
                if (found == refs.end())
                {
                    refs.push_back(reference);
                }
                else if (reference.strength == AssetRefStrength::Strong)
                {
                    *found = reference;
                }
            };
            if (data.environment.environment.asset_id.valid())
            {
                append(data.environment.environment);
            }
            for (const auto& actor : data.actors)
            {
                if (types)
                {
                    const auto owned = reflected_value_references(*types, actor.properties);
                    if (owned.succeeded())
                    {
                        for (const auto& reference : owned.value())
                        {
                            append(reference);
                        }
                    }
                }
                for (const auto& component : actor.components)
                {
                    // C++17 get_if reads resource references only from mesh author data.
                    const auto* mesh = std::get_if<SceneMeshData>(&component.properties);
                    if (!mesh)
                    {
                        continue;
                    }
                    for (const auto& binding : mesh->resources)
                    {
                        append(binding.reference);
                    }
                }
            }
            std::sort(refs.begin(), refs.end(),
                      [](const AssetRef& a, const AssetRef& b)
                      {
                          return a.asset_id < b.asset_id;
                      });
            return refs;
        }
    } // namespace

    bool validate_scene_environment_settings(const SceneEnvironmentSettings& settings)
    {
        Quaternion normalized;
        const auto& reference = settings.environment;
        if (!is_finite(settings.intensity) || settings.intensity < 0.0f ||
            !try_normalize(settings.rotation, normalized))
        {
            return false;
        }
        if (!reference.asset_id.valid())
        {
            return reference.expected_type.empty() && !reference.subresource_id.valid() &&
                   reference.strength == AssetRefStrength::Strong;
        }
        return reference.expected_type == "toy3d.EnvironmentAssetData" && !reference.subresource_id.valid() &&
               reference.strength == AssetRefStrength::Strong;
    }

    bool validate_component_data(const SceneComponentData& component)
    {
        const Transform& transform = component.transform;
        Quaternion normalized;
        if (!is_finite(transform.translation) || !is_finite(transform.scale) ||
            transform.scale.x <= k_default_float_tolerance || transform.scale.y <= k_default_float_tolerance ||
            transform.scale.z <= k_default_float_tolerance || !try_normalize(transform.rotation, normalized))
        {
            return false;
        }
        // C++17 get_if makes the finite schema branches explicit and readable.
        if (std::get_if<SceneNodeData>(&component.properties))
        {
            return component.type == "toy3d.SceneComponent";
        }
        if (const auto* mesh = std::get_if<SceneMeshData>(&component.properties))
        {
            if (component.type != "toy3d.StaticMeshComponent" ||
                (!mesh->builtin_mesh.empty() && mesh->builtin_mesh != "Cube" && mesh->builtin_mesh != "Plane"))
            {
                return false;
            }
            std::set<std::string> roles;
            bool has_mesh = false;
            for (const auto& binding : mesh->resources)
            {
                const AssetRef& ref = binding.reference;
                if (!roles.insert(binding.role).second || !ref.asset_id.valid() || ref.subresource_id.valid() ||
                    ref.strength != AssetRefStrength::Strong)
                {
                    return false;
                }
                if (binding.role == "mesh")
                {
                    has_mesh = true;
                    if (ref.expected_type != "toy3d.StaticMeshAssetData")
                    {
                        return false;
                    }
                }
                else if (binding.role.compare(0, 9, "material:") != 0 || binding.role.size() <= 9 ||
                         (ref.expected_type != "toy3d.MaterialAssetData" &&
                          ref.expected_type != "toy3d.MaterialInstanceAssetData"))
                {
                    return false;
                }
            }
            return !has_mesh || mesh->builtin_mesh.empty();
        }
        if (const auto* light = std::get_if<SceneDirectionalLightData>(&component.properties))
        {
            return component.type == "toy3d.DirectionalLightComponent" && is_valid(light->light) &&
                   is_valid(light->shadow);
        }
        if (const auto* light = std::get_if<ScenePointLightData>(&component.properties))
        {
            return component.type == "toy3d.PointLightComponent" && is_valid(light->light) &&
                   is_valid(light->attenuation);
        }
        if (const auto* camera = std::get_if<CameraSettings>(&component.properties))
        {
            return component.type == "toy3d.CameraComponent" && is_valid(*camera);
        }
        return false;
    }

    AssetStatus validate_scene_asset(const SceneAssetData& data, const AssetIndex* index, const TypeRegistry* types)
    {
        if (!validate_scene_environment_settings(data.environment))
        {
            return invalid("Scene Environment requires finite nonnegative intensity, valid rotation and an optional "
                           "strong Environment root.");
        }
        if (index && data.environment.environment.asset_id.valid())
        {
            const auto resolved = index->resolve(data.environment.environment, "environment");
            if (!resolved.succeeded())
            {
                return resolved;
            }
        }
        if (data.actors.size() > k_max_scene_actors)
        {
            return invalid("scene actor count exceeds limit");
        }
        constexpr std::size_t k_max_components = 1000000u;
        std::set<std::string> ids;
        std::map<std::string, std::string> parents;
        std::map<AssetId, std::string> reference_types;
        if (data.environment.environment.asset_id.valid())
        {
            reference_types.emplace(data.environment.environment.asset_id, data.environment.environment.expected_type);
        }
        const std::map<std::string, std::string> builtin_types = {{"EmptyActor", "toy3d.Actor"},
                                                                  {"Cube", "toy3d.StaticMeshActor"},
                                                                  {"Plane", "toy3d.StaticMeshActor"},
                                                                  {"StaticMesh", "toy3d.StaticMeshActor"},
                                                                  {"DirectionalLight", "toy3d.DirectionalLightActor"},
                                                                  {"PointLight", "toy3d.PointLightActor"},
                                                                  {"Camera", "toy3d.CameraActor"}};
        for (const auto& actor : data.actors)
        {
            const auto expected = builtin_types.find(actor.kind);
            if (expected != builtin_types.end() && expected->second != actor.type)
            {
                return invalid("builtin placement kind and Actor type disagree");
            }

            AssetId id;
            if (!AssetId::parse(actor.id, id) || id.hex() != actor.id || !ids.insert(actor.id).second ||
                (!valid_kind(actor.kind) && actor.kind != "Custom") || actor.type.empty() ||
                actor.properties.type.empty() || actor.properties.schema_version == 0 || actor.components.empty())
            {
                return invalid("invalid scene actor identity or type");
            }
            if (types)
            {
                const auto owned = reflected_value_references(*types, actor.properties);
                if (!owned.succeeded())
                {
                    return owned.status();
                }
                for (const auto& reference : owned.value())
                {
                    const auto existing = reference_types.find(reference.asset_id);
                    if (existing != reference_types.end() && existing->second != reference.expected_type)
                    {
                        return invalid("conflicting actor resource types");
                    }
                    reference_types.emplace(reference.asset_id, reference.expected_type);
                    if (index)
                    {
                        const auto resolved = index->resolve(reference, actor.type);
                        if (!resolved.succeeded())
                        {
                            return resolved;
                        }
                    }
                }
            }
            bool has_root = false;
            for (const auto& component : actor.components)
            {
                if (parents.size() >= k_max_components || !AssetId::parse(component.id, id) ||
                    id.hex() != component.id || !ids.insert(component.id).second || !validate_component_data(component))
                {
                    return invalid("invalid scene component identity, properties or type");
                }
                has_root = has_root || component.id == actor.root_component_id;
                parents.emplace(component.id, component.parent_component_id);
                // C++17 get_if selects the only branch containing resource bindings.
                if (const auto* mesh = std::get_if<SceneMeshData>(&component.properties))
                {
                    bool has_source = !mesh->builtin_mesh.empty();
                    for (const auto& binding : mesh->resources)
                    {
                        has_source = has_source || binding.role == "mesh";
                    }
                    if (!has_source)
                    {
                        return invalid("scene mesh has no restorable geometry source");
                    }
                    for (const auto& binding : mesh->resources)
                    {
                        const auto existing = reference_types.find(binding.reference.asset_id);
                        if (existing != reference_types.end() && existing->second != binding.reference.expected_type)
                        {
                            return invalid("scene references the same asset with conflicting types");
                        }
                        reference_types.emplace(binding.reference.asset_id, binding.reference.expected_type);
                        if (index)
                        {
                            const AssetStatus resolved = index->resolve(binding.reference, binding.role);
                            if (!resolved.succeeded())
                            {
                                return resolved;
                            }
                        }
                    }
                }
            }
            if (!has_root)
            {
                return invalid("scene root component is not owned by its actor");
            }
        }
        std::map<std::string, std::uint8_t> visited;
        for (const auto& entry : parents)
        {
            std::string current = entry.first;
            std::vector<std::string> chain;
            while (!current.empty())
            {
                if (visited[current] == 1u)
                {
                    return invalid("scene attachment cycle");
                }
                if (visited[current] == 2u)
                {
                    break;
                }
                const auto parent = parents.find(current);
                if (parent == parents.end())
                {
                    return invalid("scene attachment parent is absent");
                }
                visited[current] = 1u;
                chain.push_back(current);
                current = parent->second;
            }
            for (const auto& component : chain)
            {
                visited[component] = 2u;
            }
        }
        return AssetStatus::success();
    }

    AssetResult<AssetPairBytes> encode_scene_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                        const SceneAssetData& data, const AssetIndex* index)
    {
        const AssetStatus valid = validate_scene_asset(data, index, &types);
        if (!valid.succeeded())
        {
            return AssetResult<AssetPairBytes>(valid);
        }
        const TypeDesc* type = types.find(k_scene_type);
        if (!types.frozen() || !type || !id.valid())
        {
            return AssetResult<AssetPairBytes>(invalid("Scene type is unavailable or Asset ID is invalid"));
        }
        ValueWriter writer;
        const ValueStatus encoded = encode_value(writer, data);
        if (!encoded.succeeded())
        {
            return AssetResult<AssetPairBytes>(invalid("Scene encoding failed"));
        }
        AssetFileIndex file;
        file.asset_id = id;
        file.root_type = k_scene_type;
        file.schema_version = type->schema_version;
        file.dependencies = dependencies(data, &types);
        return encode_asset_pair(types, std::move(file), writer.bytes(), {});
    }

    AssetStatus read_scene_asset(const TypeRegistry& types, const FileSystem& files, const VirtualPath& path,
                                 SceneAssetData& output, const AssetIndex* index,
                                 std::vector<std::uint8_t>* source_bytes)
    {
        const auto pair = read_asset_pair(types, files, path);
        if (!pair.succeeded())
        {
            return pair.status();
        }
        const AssetYamlDocument& description = pair.value().description;
        const TypeDesc* type = types.find(k_scene_type);
        if (!type || description.index.root_type != k_scene_type ||
            description.index.schema_version != type->schema_version || description.has_meta)
        {
            return {AssetErrorCode::Schema,
                    description.index.asset_id,
                    path.utf8(),
                    {},
                    {},
                    "Scene root type, schema or payload is invalid",
                    {}};
        }
        ValueReader reader(description.type_data);
        SceneAssetData candidate;
        const ValueStatus decoded = decode_value(reader, candidate);
        if (!decoded.succeeded() || !reader.at_end())
        {
            return {AssetErrorCode::Value,
                    description.index.asset_id,
                    path.utf8(),
                    "data",
                    decoded.property_path,
                    decoded.succeeded() ? "trailing scene data" : decoded.message,
                    {}};
        }
        const AssetStatus valid = validate_scene_asset(candidate, index, &types);
        if (!valid.succeeded())
        {
            return valid;
        }
        const auto expected = dependencies(candidate, &types);
        if (expected.size() != description.index.dependencies.size())
        {
            return invalid("Scene dependency index differs from resource bindings");
        }
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const AssetRef& actual = description.index.dependencies[i];
            if (!(expected[i].asset_id == actual.asset_id) || expected[i].expected_type != actual.expected_type ||
                expected[i].strength != actual.strength)
            {
                return invalid("Scene dependency index differs from resource bindings");
            }
        }
        if (source_bytes)
        {
            *source_bytes = pair.value().description_bytes;
        }
        output = std::move(candidate);
        return AssetStatus::success();
    }
} // namespace toy3d
