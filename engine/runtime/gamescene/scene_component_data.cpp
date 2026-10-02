#include "gamescene/scene_component_data.h"

#include "gamescene/actor/actor.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/component/static_mesh_component.h"

namespace toy3d
{
    SceneComponent* create_scene_component(Actor& actor, const std::string& type)
    {
        if (type == "toy3d.SceneComponent")
        {
            return &actor.create_component<SceneComponent>();
        }
        if (type == "toy3d.StaticMeshComponent")
        {
            return &actor.create_component<StaticMeshComponent>();
        }
        if (type == "toy3d.DirectionalLightComponent")
        {
            return &actor.create_component<DirectionalLightComponent>();
        }
        if (type == "toy3d.PointLightComponent")
        {
            return &actor.create_component<PointLightComponent>();
        }
        if (type == "toy3d.CameraComponent")
        {
            return &actor.create_component<CameraComponent>();
        }
        return nullptr;
    }
    bool capture_scene_component(const SceneComponent& component, SceneComponentData& data)
    {
        data.transform = component.local_transform();
        if (typeid(component) == typeid(SceneComponent))
        {
            data.type = "toy3d.SceneComponent";
            data.properties = SceneNodeData{};
        }
        else if (const auto* mesh = dynamic_cast<const StaticMeshComponent*>(&component))
        {
            data.type = "toy3d.StaticMeshComponent";
            SceneMeshData props;
            props.settings = mesh->primitive_settings();
            data.properties = props;
        }
        else if (const auto* directional = dynamic_cast<const DirectionalLightComponent*>(&component))
        {
            data.type = "toy3d.DirectionalLightComponent";
            data.properties = SceneDirectionalLightData{directional->light_settings(), directional->shadow_settings()};
        }
        else if (const auto* point = dynamic_cast<const PointLightComponent*>(&component))
        {
            data.type = "toy3d.PointLightComponent";
            data.properties = ScenePointLightData{point->light_settings(), point->local_light_settings()};
        }
        else if (const auto* camera = dynamic_cast<const CameraComponent*>(&component))
        {
            data.type = "toy3d.CameraComponent";
            data.properties = camera->camera_settings();
        }
        else
        {
            return false;
        }
        return true;
    }
    bool apply_scene_component(SceneComponent& component, const SceneComponentData& data)
    {
        SceneComponentData actual;
        if (!capture_scene_component(component, actual) || actual.type != data.type || !validate_component_data(data))
        {
            return false;
        }
        // C++17 get_if applies the finite builtin component schema; actor extension data is independent.
        if (const auto* mesh = std::get_if<SceneMeshData>(&data.properties))
        {
            static_cast<StaticMeshComponent&>(component).set_primitive_settings(mesh->settings);
        }
        else if (const auto* light = std::get_if<SceneDirectionalLightData>(&data.properties))
        {
            auto& target = static_cast<DirectionalLightComponent&>(component);
            if (!target.set_light_settings(light->light) || !target.set_shadow_settings(light->shadow))
            {
                return false;
            }
        }
        else if (const auto* point = std::get_if<ScenePointLightData>(&data.properties))
        {
            auto& target = static_cast<PointLightComponent&>(component);
            if (!target.set_light_settings(point->light) || !target.set_local_light_settings(point->attenuation))
            {
                return false;
            }
        }
        else if (const auto* camera = std::get_if<CameraSettings>(&data.properties))
        {
            if (!static_cast<CameraComponent&>(component).set_camera_settings(*camera))
            {
                return false;
            }
        }
        return component.set_local_transform(data.transform);
    }
} // namespace toy3d
