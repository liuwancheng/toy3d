#pragma once

#include "asset/scene/scene_asset_data.h"
#include "rendercore/geometry/static_mesh.h"
#include <string>
#include <typeindex>
#include <vector>

namespace toy3d
{
    class Actor;
    class SceneComponent;
    class EditorCommandHistory;
    class EditorWorkspace;
    class EditorSelection;
    class SceneViewport;
    class MaterialAssignments;
    class ActorFactory;
    class World;

    struct ComponentDetailsContext
    {
        World& world;
        Actor& actor;
        SceneComponent& component;
        EditorCommandHistory& history;
        const EditorWorkspace& workspace;
        EditorSelection& selection;
        SceneViewport& viewport;
        MaterialAssignments& materials;
        std::string& error;
    };

    // One descriptor belongs to one runtime component type. It contains behavior,
    // never component values or long-lived pointers to the edited object.
    struct ComponentEditor
    {
        std::type_index runtime_type{typeid(void)};
        std::string persistent_type;
        const char* display_name = "";
        void (*capture)(const SceneComponent&, SceneComponentData&) = nullptr;
        bool (*apply)(SceneComponent&, const SceneComponentData&) = nullptr;
        SceneComponent& (*create)(Actor&) = nullptr;
        void (*draw_details)(ComponentDetailsContext&) = nullptr;
    };

    class ComponentEditorRegistry
    {
      public:
        ComponentEditorRegistry();
        bool add(ComponentEditor editor);
        bool freeze();
        const ComponentEditor* find(const SceneComponent& component) const;
        const ComponentEditor* find(const std::string& persistent_type) const;
        bool capture(const SceneComponent& component, SceneComponentData& data) const;
        bool apply(SceneComponent& component, const SceneComponentData& data) const;

      private:
        std::vector<ComponentEditor> editors_;
        bool frozen_ = false;
    };

    void draw_node_details(ComponentDetailsContext& context);
    void draw_mesh_details(ComponentDetailsContext& context);
    void draw_directional_light_details(ComponentDetailsContext& context);
    void draw_point_light_details(ComponentDetailsContext& context);
    void draw_camera_details(ComponentDetailsContext& context);
} // namespace toy3d
