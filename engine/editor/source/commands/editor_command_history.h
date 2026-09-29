#pragma once

#include "placement/actor_factory.h"
#include "material/material_assignments.h"
#include <cstdint>
#include <vector>

namespace toy3d
{
    class World;
    enum class EditorTransformSource { Gizmo, Details };

    // One command timeline for placement, deletion and continuous property gestures.
    class EditorCommandHistory
    {
      public:
        explicit EditorCommandHistory(ActorFactory& factory) : factory_(factory) {}
        EditorCommandHistory(ActorFactory& factory, MaterialAssignments& materials) : factory_(factory), materials_(&materials) {}
        void begin(World& world, std::uint32_t actor_id, const Transform& before, EditorTransformSource source);
        void finish(World& world, EditorTransformSource source);
        void cancel();
        void clear();
        std::uint32_t place_actor(World& world, const PlacementRequest& request);
        bool delete_actor(World& world, std::uint32_t actor_id);
        bool assign_material(World& world, std::uint32_t actor_id, std::uint32_t component_id,
                             const std::string& slot_name, const AssetRef& material, std::string& error);
        bool undo(World& world);
        bool redo(World& world);
        bool active() const { return active_; }
        bool active_for(EditorTransformSource source) const { return active_ && source_ == source; }

      private:
        enum class Kind { Modify, Create, Delete, Material };
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
        };
        void bind(World& world);
        bool apply(World& world, Record& record, bool forward);
        void remap_actor(std::uint32_t old_id, std::uint32_t new_id,
                         std::uint32_t old_component_id, std::uint32_t new_component_id);
        ActorFactory& factory_;
        MaterialAssignments* materials_ = nullptr;
        World* world_ = nullptr;
        bool active_ = false;
        EditorTransformSource source_ = EditorTransformSource::Gizmo;
        Record pending_;
        std::vector<Record> undo_;
        std::vector<Record> redo_;
    };
}
