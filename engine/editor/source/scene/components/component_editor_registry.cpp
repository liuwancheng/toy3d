#include "scene/components/component_editor_registry.h"

#include <utility>
#include "gamescene/scene_component_data.h"
#include "gamescene/actor/actor.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/component/static_mesh_component.h"
#include "logging/logger.h"

namespace toy3d
{
    namespace
    {
        // Persisted builtin behavior belongs to Runtime; Editor adds only widget dispatch.
        void capture_builtin(const SceneComponent& component, SceneComponentData& data)
        {
            if (!capture_scene_component(component, data)) TOY_LOG_ERROR("Builtin component capture failed.");
        }
        bool apply_builtin(SceneComponent& component, const SceneComponentData& data)
        { return apply_scene_component(component, data); }
        SceneComponent& create_node(Actor& actor) { return actor.create_component<SceneComponent>(); }
        SceneComponent& create_mesh(Actor& actor) { return actor.create_component<StaticMeshComponent>(); }
        SceneComponent& create_directional(Actor& actor) { return actor.create_component<DirectionalLightComponent>(); }
        SceneComponent& create_point(Actor& actor) { return actor.create_component<PointLightComponent>(); }
        SceneComponent& create_camera(Actor& actor) { return actor.create_component<CameraComponent>(); }
    }

    ComponentEditorRegistry::ComponentEditorRegistry()
    {
        const bool registered =
            add({typeid(SceneComponent), "toy3d.SceneComponent", "Scene", capture_builtin, apply_builtin, create_node, draw_node_details}) &&
            add({typeid(StaticMeshComponent), "toy3d.StaticMeshComponent", "Static Mesh", capture_builtin, apply_builtin, create_mesh, draw_mesh_details}) &&
            add({typeid(DirectionalLightComponent), "toy3d.DirectionalLightComponent", "Directional Light", capture_builtin, apply_builtin, create_directional, draw_directional_light_details}) &&
            add({typeid(PointLightComponent), "toy3d.PointLightComponent", "Point Light", capture_builtin, apply_builtin, create_point, draw_point_light_details}) &&
            add({typeid(CameraComponent), "toy3d.CameraComponent", "Camera", capture_builtin, apply_builtin, create_camera, draw_camera_details});
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
        return editor->apply(component, data) && component.set_local_transform(data.transform);
    }
}
