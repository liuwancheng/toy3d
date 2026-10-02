#pragma once

#include "scene/editor_actor_state.h"
#include "scene/placement/placement_catalog.h"
#include "rendercore/geometry/static_mesh.h"
#include "gamescene/scene_geometry.h"
#include "gamescene/actor/actor_type_registry.h"

#include <map>

namespace toy3d
{
    class Actor;
    class World;

    // Holds geometry across add/remove commands; release only after scene drain.
    class ActorFactory
    {
      public:
        ComponentEditorRegistry& component_editors() { return component_editors_; }
        const ComponentEditorRegistry& component_editors() const { return component_editors_; }
        StaticMeshRef instantiate_builtin(const std::string& kind) const;
        void remember(const Actor& actor, const PlacementRequest& request);
        EditorActorState capture(const Actor& actor) const;
        bool mesh_source(const SceneComponent& component, SceneMeshData& data) const;
        void remember_mesh(const SceneComponent& component, const SceneMeshData& data);
        ActorTypeRegistry& actor_types() { return actor_types_; }
        const ActorTypeRegistry& actor_types() const { return actor_types_; }
        bool initialize();
        void release();
        Actor* create(World& world, const PlacementRequest& request);
        Actor* restore(World& world, const PlacementRequest& request, const EditorActorState& state,
                       std::map<std::uint32_t, std::uint32_t>& component_ids);
        bool describe(const Actor& actor, PlacementRequest& request) const;
        const char* label(std::uint32_t actor_id) const;
        void forget(std::uint32_t actor_id);
        const MaterialInstanceRef& default_material() const { return geometry_.default_material(); }
      private:
        ComponentEditorRegistry component_editors_;
        SceneGeometry geometry_;
        ActorTypeRegistry actor_types_;
        std::map<std::uint32_t, PlacementRequest> placed_items_;
        struct MeshSource
        {
            std::uint32_t actor_id = 0;
            StaticMeshRef geometry;
            SceneMeshData data;
        };
        std::map<std::uint32_t, MeshSource> mesh_sources_;
    };
}
