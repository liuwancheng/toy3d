#pragma once

#include "placement/actor_factory.h"
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
        void begin(World& world, std::uint32_t actor_id, const Transform& before, EditorTransformSource source);
        void finish(World& world, EditorTransformSource source);
        void cancel();
        void clear();
        std::uint32_t place_actor(World& world, const PlacementRequest& request);
        bool delete_actor(World& world, std::uint32_t actor_id);
        bool undo(World& world);
        bool redo(World& world);
        bool active() const { return active_; }
        bool active_for(EditorTransformSource source) const { return active_ && source_ == source; }

      private:
        enum class Kind { Modify, Create, Delete };
        struct Record
        {
            Kind kind = Kind::Modify;
            std::uint32_t actor_id = 0;
            PlacementRequest placement;
            EditorActorState before;
            EditorActorState after;
        };
        void bind(World& world);
        bool apply(World& world, Record& record, bool forward);
        void remap_actor(std::uint32_t old_id, std::uint32_t new_id);
        ActorFactory& factory_;
        World* world_ = nullptr;
        bool active_ = false;
        EditorTransformSource source_ = EditorTransformSource::Gizmo;
        Record pending_;
        std::vector<Record> undo_;
        std::vector<Record> redo_;
    };
}
