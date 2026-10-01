#pragma once

#include "asset/asset_identity.h"
#include "math/transform.h"
#include "reflection/reflection_macros.h"

#include <string>
#include <cstdint>
#include <variant>
#include <vector>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.ModelImportSettings", 1)
    struct ModelImportSettings
    {
        TOY3D_PROPERTY("unit_scale", Edit)
        float unit_scale = 1.0f;
        TOY3D_PROPERTY("generate_normals", Edit)
        bool generate_normals = true;
    };

    TOY3D_REFLECT_TYPE("toy3d.ModelNodeData", 1)
    struct ModelNodeData
    {
        TOY3D_PROPERTY("id")
        std::string id;
        TOY3D_PROPERTY("source_key")
        std::string source_key;
        TOY3D_PROPERTY("mesh_subresource")
        std::string mesh_subresource;
        TOY3D_PROPERTY("material_override", Edit)
        AssetRef material_override;
    };

    TOY3D_REFLECT_TYPE("toy3d.ModelAssetData", 1)
    struct ModelAssetData
    {
        TOY3D_PROPERTY("source_uri", Edit)
        std::string source_uri;
        TOY3D_PROPERTY("import_settings", Edit)
        ModelImportSettings import_settings;
        TOY3D_PROPERTY("skeleton", Edit)
        AssetRef skeleton;
        TOY3D_PROPERTY("nodes", Edit)
        std::vector<ModelNodeData> nodes;
        TOY3D_PROPERTY("geometry_segment")
        std::string geometry_segment;
    };

    TOY3D_REFLECT_ENUM("toy3d.InterpolationMode")
    enum class InterpolationMode
    {
        Step = 1,
        Linear = 2
    };

    TOY3D_REFLECT_TYPE("toy3d.AnimationKey", 1)
    struct AnimationKey
    {
        TOY3D_PROPERTY("time", Edit)
        float time = 0.0f;
        TOY3D_PROPERTY("translation", Edit)
        Vector3 translation;
    };

    TOY3D_REFLECT_TYPE("toy3d.AnimationTrack", 1)
    struct AnimationTrack
    {
        TOY3D_PROPERTY("id")
        std::string id;
        TOY3D_PROPERTY("target_id")
        std::string target_id;
        TOY3D_PROPERTY("interpolation", Edit)
        InterpolationMode interpolation = InterpolationMode::Linear;
        TOY3D_PROPERTY("keys", Edit)
        std::vector<AnimationKey> keys;
    };

    TOY3D_REFLECT_TYPE("toy3d.AnimationEventData", 1)
    struct AnimationEventData
    {
        TOY3D_PROPERTY("id")
        std::string id;
        TOY3D_PROPERTY("time", Edit)
        float time = 0.0f;
        TOY3D_PROPERTY("name", Edit)
        std::string name;
    };

    TOY3D_REFLECT_TYPE("toy3d.AnimationAssetData", 1)
    struct AnimationAssetData
    {
        TOY3D_PROPERTY("skeleton", Edit)
        AssetRef skeleton;
        TOY3D_PROPERTY("duration", Edit)
        float duration = 0.0f;
        TOY3D_PROPERTY("tracks", Edit)
        std::vector<AnimationTrack> tracks;
        TOY3D_PROPERTY("events", Edit)
        std::vector<AnimationEventData> events;
    };

    TOY3D_REFLECT_TYPE("toy3d.BoxShape", 1)
    struct BoxShape
    {
        TOY3D_PROPERTY("half_extents", Edit)
        Vector3 half_extents;
        TOY3D_PROPERTY("local", Edit)
        Transform local;
    };

    TOY3D_REFLECT_TYPE("toy3d.SphereShape", 1)
    struct SphereShape
    {
        TOY3D_PROPERTY("radius", Edit)
        float radius = 0.0f;
        TOY3D_PROPERTY("local", Edit)
        Transform local;
    };

    TOY3D_REFLECT_TYPE("toy3d.CapsuleShape", 1)
    struct CapsuleShape
    {
        TOY3D_PROPERTY("radius", Edit)
        float radius = 0.0f;
        TOY3D_PROPERTY("half_height", Edit)
        float half_height = 0.0f;
        TOY3D_PROPERTY("local", Edit)
        Transform local;
    };

    TOY3D_REFLECT_TYPE("toy3d.MeshShape", 1)
    struct MeshShape
    {
        TOY3D_PROPERTY("mesh", Edit)
        AssetRef mesh;
        TOY3D_PROPERTY("convex", Edit)
        bool convex = false;
        TOY3D_PROPERTY("local", Edit)
        Transform local;
    };

    TOY3D_REFLECT_TYPE("toy3d.CollisionShapeData", 1)
    struct CollisionShapeData
    {
        TOY3D_PROPERTY("id")
        std::string id;
        // C++17 variant keeps the supported shape branches finite.
        TOY3D_PROPERTY("shape", Edit)
        std::variant<BoxShape, SphereShape, CapsuleShape, MeshShape> shape;
    };

    TOY3D_REFLECT_TYPE("toy3d.CollisionAssetData", 1)
    struct CollisionAssetData
    {
        TOY3D_PROPERTY("shapes", Edit)
        std::vector<CollisionShapeData> shapes;
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneComponentData", 1)
    struct SceneComponentData
    {
        TOY3D_PROPERTY("id")
        std::string id;
        TOY3D_PROPERTY("type")
        std::string type;
        TOY3D_PROPERTY("asset", Edit)
        AssetRef asset;
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneActorData", 1)
    struct SceneActorData
    {
        TOY3D_PROPERTY("id")
        std::string id;
        TOY3D_PROPERTY("type")
        std::string type;
        TOY3D_PROPERTY("components", Edit)
        std::vector<SceneComponentData> components;
        TOY3D_PROPERTY("root_component_id")
        std::string root_component_id;
        TOY3D_PROPERTY("parent_actor_id")
        std::string parent_actor_id;
        TOY3D_PROPERTY("parent_component_id")
        std::string parent_component_id;
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneAssetData", 1)
    struct SceneAssetData
    {
        TOY3D_PROPERTY("actors", Edit)
        std::vector<SceneActorData> actors;
    };

    TOY3D_REFLECT_TYPE("toy3d.MaterialOverrideData", 1)
    struct MaterialOverrideData
    {
        TOY3D_PROPERTY("parameter_id")
        std::uint64_t parameter_id = 0;
        TOY3D_PROPERTY("parameter_name")
        std::string parameter_name;
        TOY3D_PROPERTY("declared_type")
        std::string declared_type;
        TOY3D_PROPERTY("scalar", Edit)
        float scalar = 0.0f;
    };

    TOY3D_REFLECT_TYPE("toy3d.MaterialAssetData", 1)
    struct MaterialAssetData
    {
        TOY3D_PROPERTY("shader", Edit)
        AssetRef shader;
        TOY3D_PROPERTY("overrides", Edit)
        std::vector<MaterialOverrideData> overrides;
    };
} // namespace toy3d
