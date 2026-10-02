#pragma once

#include "asset/asset_identity.h"
#include "math/transform.h"
#include "asset/scene/component_settings.h"
#include "reflection/reflection_macros.h"
#include "serialization/reflected_value.h"

#include <string>
#include <variant>
#include <vector>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.SceneResourceBinding", 1)
    struct SceneResourceBinding
    {
        TOY3D_PROPERTY("role")
        std::string role;
        TOY3D_PROPERTY("reference")
        AssetRef reference;
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneNodeData", 1)
    struct SceneNodeData
    {
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneMeshData", 1)
    struct SceneMeshData
    {
        TOY3D_PROPERTY("settings", Edit)
        PrimitiveSettings settings;
        TOY3D_PROPERTY("builtin_mesh")
        std::string builtin_mesh;
        TOY3D_PROPERTY("resources", Edit)
        std::vector<SceneResourceBinding> resources;
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneDirectionalLightData", 1)
    struct SceneDirectionalLightData
    {
        TOY3D_PROPERTY("light", Edit)
        LightSettings light;
        TOY3D_PROPERTY("shadow", Edit)
        DirectionalShadowSettings shadow;
    };

    TOY3D_REFLECT_TYPE("toy3d.ScenePointLightData", 1)
    struct ScenePointLightData
    {
        TOY3D_PROPERTY("light", Edit)
        LightSettings light;
        TOY3D_PROPERTY("attenuation", Edit)
        LocalLightSettings attenuation;
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneComponentData", 1)
    struct SceneComponentData
    {
        TOY3D_PROPERTY("id")
        std::string id;
        TOY3D_PROPERTY("type")
        std::string type;
        TOY3D_PROPERTY("parent_component_id")
        std::string parent_component_id;
        TOY3D_PROPERTY("transform", Edit)
        Transform transform;
        // C++17 variant retains typed author data for each supported component.
        // A new persisted component explicitly declares its schema branch.
        TOY3D_PROPERTY("properties", Edit)
        std::variant<SceneNodeData, SceneMeshData, SceneDirectionalLightData, ScenePointLightData, CameraSettings> properties;
    };

    TOY3D_REFLECT_TYPE("toy3d.ActorSettings", 1)
    struct ActorSettings
    {
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneActorData", 6)
    struct SceneActorData
    {
        TOY3D_PROPERTY("id")
        std::string id;
        TOY3D_PROPERTY("kind")
        std::string kind;
        TOY3D_PROPERTY("type")
        std::string type = "toy3d.Actor";
        TOY3D_PROPERTY("properties", Edit)
        ReflectedValue properties = ReflectedValue{"toy3d.ActorSettings", 1, {0, 0, 0, 0}};
        TOY3D_PROPERTY("root_component_id")
        std::string root_component_id;
        TOY3D_PROPERTY("components", Edit)
        std::vector<SceneComponentData> components;
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneAssetData", 6)
    struct SceneAssetData
    {
        TOY3D_PROPERTY("actors", Edit)
        std::vector<SceneActorData> actors;
    };

    bool validate_component_data(const SceneComponentData& component);
}
