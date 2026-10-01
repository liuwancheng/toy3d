#include "scene/components/component_editor_registry.h"

#include <utility>
#include "gamescene/actor/actor.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/component/static_mesh_component.h"
#include "logging/logger.h"

namespace toy3d
{
    namespace
    {
        void capture_node(const SceneComponent&, SceneComponentData& data) { data.properties = SceneNodeData{}; }
        bool apply_node(SceneComponent&, const SceneComponentData&) { return true; }
        SceneComponent& create_node(Actor& actor) { return actor.create_component<SceneComponent>(); }

        void capture_mesh(const SceneComponent& component, SceneComponentData& data)
        {
            SceneMeshData mesh;
            mesh.settings = static_cast<const StaticMeshComponent&>(component).primitive_settings();
            data.properties = std::move(mesh);
        }
        bool apply_mesh(SceneComponent& component, const SceneComponentData& data)
        {
            // C++17 get_if rejects a schema/runtime mismatch before changing properties.
            const auto* mesh = std::get_if<SceneMeshData>(&data.properties);
            if (!mesh) return false;
            static_cast<StaticMeshComponent&>(component).set_primitive_settings(mesh->settings);
            return true;
        }
        SceneComponent& create_mesh(Actor& actor) { return actor.create_component<StaticMeshComponent>(); }

        void capture_directional(const SceneComponent& component, SceneComponentData& data)
        {
            const auto& light = static_cast<const DirectionalLightComponent&>(component);
            data.properties = SceneDirectionalLightData{light.light_settings(), light.shadow_settings()};
        }
        bool apply_directional(SceneComponent& component, const SceneComponentData& data)
        {
            // C++17 get_if preserves the typed directional settings boundary.
            const auto* settings = std::get_if<SceneDirectionalLightData>(&data.properties);
            if (!settings) return false;
            auto& light = static_cast<DirectionalLightComponent&>(component);
            return light.set_light_settings(settings->light) && light.set_shadow_settings(settings->shadow);
        }
        SceneComponent& create_directional(Actor& actor) { return actor.create_component<DirectionalLightComponent>(); }

        void capture_point(const SceneComponent& component, SceneComponentData& data)
        {
            const auto& light = static_cast<const PointLightComponent&>(component);
            data.properties = ScenePointLightData{light.light_settings(), light.local_light_settings()};
        }
        bool apply_point(SceneComponent& component, const SceneComponentData& data)
        {
            // C++17 get_if preserves the typed local-light settings boundary.
            const auto* settings = std::get_if<ScenePointLightData>(&data.properties);
            if (!settings) return false;
            auto& light = static_cast<PointLightComponent&>(component);
            return light.set_light_settings(settings->light) && light.set_local_light_settings(settings->attenuation);
        }
        SceneComponent& create_point(Actor& actor) { return actor.create_component<PointLightComponent>(); }

        void capture_camera(const SceneComponent& component, SceneComponentData& data)
        { data.properties = static_cast<const CameraComponent&>(component).camera_settings(); }
        bool apply_camera(SceneComponent& component, const SceneComponentData& data)
        {
            // C++17 get_if selects the camera branch without casts into untyped storage.
            const auto* settings = std::get_if<CameraSettings>(&data.properties);
            return settings && static_cast<CameraComponent&>(component).set_camera_settings(*settings);
        }
        SceneComponent& create_camera(Actor& actor) { return actor.create_component<CameraComponent>(); }
    }

    ComponentEditorRegistry::ComponentEditorRegistry()
    {
        const bool registered =
            add({typeid(SceneComponent), "toy3d.SceneComponent", "Scene", capture_node, apply_node, create_node, draw_node_details}) &&
            add({typeid(StaticMeshComponent), "toy3d.StaticMeshComponent", "Static Mesh", capture_mesh, apply_mesh, create_mesh, draw_mesh_details}) &&
            add({typeid(DirectionalLightComponent), "toy3d.DirectionalLightComponent", "Directional Light", capture_directional, apply_directional, create_directional, draw_directional_light_details}) &&
            add({typeid(PointLightComponent), "toy3d.PointLightComponent", "Point Light", capture_point, apply_point, create_point, draw_point_light_details}) &&
            add({typeid(CameraComponent), "toy3d.CameraComponent", "Camera", capture_camera, apply_camera, create_camera, draw_camera_details});
        if (!registered) TOY_LOG_ERROR("Builtin component editor registration failed.");
    }

    bool ComponentEditorRegistry::add(ComponentEditor editor)
    {
        if (frozen_ || editor.runtime_type == typeid(void) || editor.persistent_type.empty() ||
            !editor.capture || !editor.apply || !editor.create || !editor.draw_details) return false;
        for (const auto& registered : editors_)
            if (registered.runtime_type == editor.runtime_type || registered.persistent_type == editor.persistent_type)
                return false;
        editors_.push_back(std::move(editor));
        return true;
    }

    bool ComponentEditorRegistry::freeze() { frozen_ = true; return true; }

    const ComponentEditor* ComponentEditorRegistry::find(const SceneComponent& component) const
    {
        for (const auto& editor : editors_)
            if (editor.runtime_type == typeid(component)) return &editor;
        return nullptr;
    }

    const ComponentEditor* ComponentEditorRegistry::find(const std::string& persistent_type) const
    {
        for (const auto& editor : editors_)
            if (editor.persistent_type == persistent_type) return &editor;
        return nullptr;
    }

    bool ComponentEditorRegistry::capture(const SceneComponent& component, SceneComponentData& data) const
    {
        const auto* editor = find(component);
        if (!editor) return false;
        data = {};
        data.type = editor->persistent_type;
        data.transform = component.local_transform();
        editor->capture(component, data);
        return true;
    }

    bool ComponentEditorRegistry::apply(SceneComponent& component, const SceneComponentData& data) const
    {
        const auto* editor = find(component);
        if (!editor || editor->persistent_type != data.type || !validate_component_data(data)) return false;
        return component.set_local_transform(data.transform) && editor->apply(component, data);
    }
}
