#include "scene_asset/scene_asset.h"

#include "serialization/value_codec.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

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
            return kind == "EmptyActor" || kind == "Cube" || kind == "Plane" ||
                kind == "StaticMesh" || kind == "DirectionalLight" ||
                kind == "PointLight" || kind == "Camera";
        }

        std::vector<AssetRef> dependencies(const SceneAssetData& data)
        {
            std::vector<AssetRef> refs;
            for (const SceneActorData& actor : data.actors)
                for (const SceneResourceBinding& binding : actor.resources)
                    if (std::none_of(refs.begin(), refs.end(), [&](const AssetRef& ref)
                        { return ref.asset_id == binding.reference.asset_id; }))
                        refs.push_back(binding.reference);
            std::sort(refs.begin(), refs.end(), [](const AssetRef& a, const AssetRef& b)
                { return a.asset_id < b.asset_id; });
            return refs;
        }
    }

    const char* scene_root_component_type(const std::string& kind)
    {
        if (kind == "EmptyActor") return "toy3d.SceneComponent";
        if (kind == "Cube" || kind == "Plane" || kind == "StaticMesh")
            return "toy3d.StaticMeshComponent";
        if (kind == "DirectionalLight") return "toy3d.DirectionalLightComponent";
        if (kind == "PointLight") return "toy3d.PointLightComponent";
        if (kind == "Camera") return "toy3d.CameraComponent";
        return "";
    }

    AssetStatus validate_scene_asset(const SceneAssetData& data, const AssetIndex* index)
    {
        if (data.actors.size() > k_max_scene_actors) return invalid("scene actor count exceeds limit");
        std::set<std::string> actor_ids;
        std::set<std::string> object_ids;
        std::map<std::string, std::string> component_owner;
        std::map<std::string, std::string> component_parent;
        for (const SceneActorData& actor : data.actors)
        {
            AssetId id;
            AssetId component_id;
            if (!AssetId::parse(actor.id, id) || !AssetId::parse(actor.root_component_id, component_id) ||
                !actor_ids.insert(actor.id).second ||
                !object_ids.insert(actor.id).second ||
                !object_ids.insert(actor.root_component_id).second ||
                !component_owner.emplace(actor.root_component_id, actor.id).second ||
                !valid_kind(actor.kind) || actor.root_component_type != scene_root_component_type(actor.kind))
                return invalid("invalid or duplicate scene object identity or type");
            component_parent.emplace(actor.root_component_id, actor.parent_component_id);
            if (!is_finite(actor.transform.translation) || !is_finite(actor.transform.rotation) ||
                !is_finite(actor.transform.scale) || actor.transform.scale.x <= 0 ||
                actor.transform.scale.y <= 0 || actor.transform.scale.z <= 0 ||
                !is_finite(actor.light_color) || actor.light_color.x < 0 ||
                actor.light_color.y < 0 || actor.light_color.z < 0 ||
                !std::isfinite(actor.light_intensity) || actor.light_intensity < 0 ||
                !std::isfinite(actor.light_range) || actor.light_range <= 0 ||
                !std::isfinite(actor.camera_vertical_fov) || actor.camera_vertical_fov <= 0 ||
                actor.camera_vertical_fov >= 180 || !std::isfinite(actor.camera_near_clip) ||
                actor.camera_near_clip <= 0 || !std::isfinite(actor.camera_far_clip) ||
                actor.camera_far_clip <= actor.camera_near_clip)
                return invalid("invalid scene transform, light or camera property");
            std::set<std::string> roles;
            bool has_mesh = false;
            for (const SceneResourceBinding& binding : actor.resources)
            {
                if (!roles.insert(binding.role).second || !binding.reference.asset_id.valid() ||
                    binding.reference.subresource_id.valid() ||
                    binding.reference.strength != AssetRefStrength::Strong)
                    return invalid("invalid or duplicate scene resource binding");
                if (binding.role == "mesh")
                {
                    has_mesh = true;
                    if (actor.kind != "StaticMesh" ||
                        binding.reference.expected_type != "toy3d.StaticMeshAssetData")
                        return invalid("invalid StaticMesh reference");
                }
                else if (binding.role.compare(0u, 9u, "material:") != 0 ||
                    binding.role.size() == 9u ||
                    (actor.kind != "Cube" && actor.kind != "Plane" && actor.kind != "StaticMesh") ||
                    (binding.reference.expected_type != "toy3d.MaterialAssetData" &&
                     binding.reference.expected_type != "toy3d.MaterialInstanceAssetData"))
                    return invalid("invalid material slot reference");
                if (index)
                {
                    const AssetStatus resolved = index->resolve(binding.reference, binding.role);
                    if (!resolved.succeeded()) return resolved;
                }
            }
            if (has_mesh != (actor.kind == "StaticMesh"))
                return invalid("StaticMesh actor must have one mesh reference");
        }
        std::map<std::string, std::uint8_t> attachment_state;
        for (const SceneActorData& actor : data.actors)
        {
            std::string component = actor.root_component_id;
            std::vector<std::string> chain;
            while (!component.empty())
            {
                if (attachment_state[component] == 1u) return invalid("scene attachment cycle");
                if (attachment_state[component] == 2u) break;
                attachment_state[component] = 1u;
                chain.push_back(component);
                const auto found = component_parent.find(component);
                if (found == component_parent.end()) return invalid("scene attachment parent is absent");
                component = found->second;
            }
            for (const std::string& visited : chain) attachment_state[visited] = 2u;
        }
        return AssetStatus::success();
    }

    AssetResult<AssetPairBytes> encode_scene_asset_pair(const TypeRegistry& types,
        const AssetId& id, const SceneAssetData& data, const AssetIndex* index)
    {
        const AssetStatus valid = validate_scene_asset(data, index);
        if (!valid.succeeded()) return AssetResult<AssetPairBytes>(valid);
        const TypeDesc* type = types.find(k_scene_type);
        if (!types.frozen() || !type || !id.valid())
            return AssetResult<AssetPairBytes>(invalid("Scene type is unavailable or Asset ID is invalid"));
        ValueWriter writer;
        const ValueStatus encoded = encode_value(writer, data);
        if (!encoded.succeeded()) return AssetResult<AssetPairBytes>(invalid("Scene encoding failed"));
        AssetFileIndex file;
        file.asset_id = id;
        file.root_type = k_scene_type;
        file.schema_version = type->schema_version;
        file.dependencies = dependencies(data);
        return encode_asset_pair(types, std::move(file), writer.bytes(), {});
    }

    AssetStatus read_scene_asset(const TypeRegistry& types, const FileSystem& files,
        const VirtualPath& path, SceneAssetData& output, const AssetIndex* index)
    {
        const auto pair = read_asset_pair(types, files, path);
        if (!pair.succeeded()) return pair.status();
        const AssetYamlDocument& description = pair.value().description;
        const TypeDesc* type = types.find(k_scene_type);
        if (!type || description.index.root_type != k_scene_type ||
            description.index.schema_version != type->schema_version || description.has_meta)
            return {AssetErrorCode::Schema, description.index.asset_id, path.utf8(), {}, {},
                "Scene root type, schema or payload is invalid", {}};
        ValueReader reader(description.type_data);
        SceneAssetData candidate;
        const ValueStatus decoded = decode_value(reader, candidate);
        if (!decoded.succeeded() || !reader.at_end())
            return {AssetErrorCode::Value, description.index.asset_id, path.utf8(), "data",
                decoded.property_path, decoded.succeeded() ? "trailing scene data" : decoded.message, {}};
        const AssetStatus valid = validate_scene_asset(candidate, index);
        if (!valid.succeeded()) return valid;
        const auto expected = dependencies(candidate);
        if (expected.size() != description.index.dependencies.size())
            return invalid("Scene dependency index differs from resource bindings");
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const AssetRef& actual = description.index.dependencies[i];
            if (!(expected[i].asset_id == actual.asset_id) ||
                expected[i].expected_type != actual.expected_type ||
                expected[i].strength != actual.strength)
                return invalid("Scene dependency index differs from resource bindings");
        }
        output = std::move(candidate);
        return AssetStatus::success();
    }
}
