#pragma once

#include "scene/placement/actor_factory.h"
#include "scene/material_assignments.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace toy3d
{
    class World;
    enum class EditorTransformSource
    {
        Gizmo,
        Details
    };

    // Scene-owned timeline. Records keep value snapshots and resolvable identities.
    class EditorCommandHistory
    {
      public:
        explicit EditorCommandHistory(ActorFactory& factory) : factory_(factory)
        {
        }
        EditorCommandHistory(ActorFactory& factory, MaterialAssignments& materials)
            : factory_(factory), materials_(&materials)
        {
        }
        void begin(World& world, std::uint32_t actor_id, const Transform& before, EditorTransformSource source);
        void finish(World& world, EditorTransformSource source);
        bool preview_component(World& world, std::uint32_t actor_id, std::uint32_t component_id,
                               const SceneComponentData& candidate);
        bool preview_actor_properties(World& world, std::uint32_t actor_id, const ReflectedValue& candidate);
        const ActorTypeRegistry& actor_types() const
        {
            return factory_.actor_types();
        }
        void cancel();
        void clear();
        std::uint32_t place_actor(World& world, const PlacementRequest& request);
        bool delete_actor(World& world, std::uint32_t actor_id);
        bool assign_material(World& world, std::uint32_t actor_id, std::uint32_t component_id,
                             const std::string& slot_name, const AssetRef& material, std::string& error);
        bool undo(World& world);
        bool redo(World& world);
        // Empty history is a harmless no-op; execution failures carry a reason for the UI.
        const std::string& error() const
        {
            return error_;
        }
        bool active() const
        {
            return active_;
        }
        bool active_for(EditorTransformSource source) const
        {
            return active_ && source_ == source;
        }
        const ComponentEditorRegistry& component_editors() const
        {
            return factory_.component_editors();
        }
        void synchronize(World& world);
        void mark_saved(World& world);
        // GT candidate construction may change World revisions before a complete rollback.
        void acknowledge_rollback(World& world);
        bool dirty(const World& world) const;
        void set_identity_remap(
            std::function<void(std::uint32_t, std::uint32_t, const std::map<std::uint32_t, std::uint32_t>&)> callback)
        {
            remap_ = std::move(callback);
        }

      private:
        enum class Kind
        {
            Modify,
            Create,
            Delete,
            Material
        };
        struct AttachedActor
        {
            std::uint32_t actor_id = 0;
            EditorActorState state;
        };
        struct Record
        {
            Kind kind = Kind::Modify;
            std::uint32_t actor_id = 0;
            PlacementRequest placement;
            EditorActorState before;
            EditorActorState after;
            std::uint32_t component_id = 0;
            std::string slot_name;
            AssetRef material_before;
            AssetRef material_after;
            std::vector<MaterialSlotAssignment> material_assignments;
            std::vector<AttachedActor> attached;
            std::uint64_t before_revision = 0;
            std::uint64_t after_revision = 0;
        };
        void bind(World& world);
        void commit(World& world, Record record);
        bool apply(World& world, Record& record, bool forward);
        void remap_actor(std::uint32_t old_id, std::uint32_t new_id,
                         const std::map<std::uint32_t, std::uint32_t>& component_ids, Record& current);
        ActorFactory& factory_;
        MaterialAssignments* materials_ = nullptr;
        World* world_ = nullptr;
        bool active_ = false;
        EditorTransformSource source_ = EditorTransformSource::Gizmo;
        Record pending_;
        std::vector<Record> undo_;
        std::vector<Record> redo_;
        std::uint64_t revision_ = 0;
        std::uint64_t next_revision_ = 1;
        std::uint64_t saved_revision_ = 0;
        std::uint64_t observed_generation_ = 0;
        bool external_dirty_ = false;
        std::string error_;
        std::function<void(std::uint32_t, std::uint32_t, const std::map<std::uint32_t, std::uint32_t>&)> remap_;
    };
} // namespace toy3d
