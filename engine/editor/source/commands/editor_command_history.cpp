#include "commands/editor_command_history.h"

#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"

namespace toy3d
{
    namespace
    {
        bool same_state(const EditorActorState& a, const EditorActorState& b)
        {
            return a.transform.translation == b.transform.translation && a.transform.rotation == b.transform.rotation &&
                   a.transform.scale == b.transform.scale && a.light_enabled == b.light_enabled &&
                   a.light_color == b.light_color && a.light_intensity == b.light_intensity && a.light_range == b.light_range &&
                   a.camera_vertical_fov == b.camera_vertical_fov && a.camera_near_clip == b.camera_near_clip &&
                   a.camera_far_clip == b.camera_far_clip;
        }
    }

    void EditorCommandHistory::clear()
    {
        active_ = false;
        pending_ = {};
        undo_.clear();
        redo_.clear();
        world_ = nullptr;
    }

    void EditorCommandHistory::bind(World& world)
    {
        if (world_ != &world) clear();
        world_ = &world;
    }

    void EditorCommandHistory::begin(World& world, std::uint32_t actor_id, const Transform& before,
                                     EditorTransformSource source)
    {
        if (active_) finish(world, source_);
        bind(world);
        Actor* actor = world.find_actor_by_id(actor_id);
        if (!actor || !actor->root_component()) return;
        pending_ = {};
        pending_.actor_id = actor_id;
        pending_.before = capture_actor_state(*actor);
        // Gizmo has already written the first delta by the time it reports activation.
        pending_.before.transform = before;
        active_ = true;
        source_ = source;
    }

    void EditorCommandHistory::finish(World& world, EditorTransformSource source)
    {
        if (!active_for(source)) return;
        active_ = false;
        if (world_ != &world) return;
        Actor* actor = world.find_actor_by_id(pending_.actor_id);
        if (!actor || !actor->root_component()) return;
        pending_.after = capture_actor_state(*actor);
        if (!same_state(pending_.before, pending_.after))
        {
            undo_.push_back(pending_);
            redo_.clear();
        }
    }

    void EditorCommandHistory::cancel() { active_ = false; }

    std::uint32_t EditorCommandHistory::place_actor(World& world, const PlacementRequest& request)
    {
        if (active_) return 0;
        bind(world);
        Actor* actor = factory_.create(world, request);
        if (!actor)
        {
            TOY_LOG_ERROR("Actor placement failed.");
            return 0;
        }
        Record record;
        record.kind = Kind::Create;
        record.actor_id = actor->actor_id();
        record.placement = request;
        record.after = capture_actor_state(*actor);
        undo_.push_back(record);
        redo_.clear();
        return record.actor_id;
    }

    bool EditorCommandHistory::delete_actor(World& world, std::uint32_t actor_id)
    {
        if (active_) return false;
        bind(world);
        Actor* actor = world.find_actor_by_id(actor_id);
        Record record;
        if (!actor || !factory_.describe(*actor, record.placement)) return false;
        record.kind = Kind::Delete;
        record.actor_id = actor_id;
        record.before = capture_actor_state(*actor);
        if (!world.destroy_actor(*actor)) return false;
        factory_.forget(actor_id);
        undo_.push_back(record);
        redo_.clear();
        return true;
    }

    void EditorCommandHistory::remap_actor(std::uint32_t old_id, std::uint32_t new_id)
    {
        // Runtime IDs are never reused. Reconstruction must update the entire remaining timeline.
        for (Record& record : undo_) if (record.actor_id == old_id) record.actor_id = new_id;
        for (Record& record : redo_) if (record.actor_id == old_id) record.actor_id = new_id;
    }

    bool EditorCommandHistory::apply(World& world, Record& record, bool forward)
    {
        if (world_ != &world) return false;
        if (record.kind == Kind::Modify)
        {
            Actor* actor = world.find_actor_by_id(record.actor_id);
            return actor && apply_actor_state(*actor, forward ? record.after : record.before);
        }
        const bool create = (record.kind == Kind::Create) == forward;
        if (create)
        {
            Actor* actor = factory_.create(world, record.placement);
            if (!actor) return false;
            if (!apply_actor_state(*actor, forward ? record.after : record.before))
            {
                const auto candidate_id = actor->actor_id();
                if (!world.destroy_actor(*actor)) TOY_LOG_ERROR("History reconstruction rollback failed.");
                factory_.forget(candidate_id);
                return false;
            }
            const auto old_id = record.actor_id;
            record.actor_id = actor->actor_id();
            remap_actor(old_id, record.actor_id);
            return true;
        }
        Actor* actor = world.find_actor_by_id(record.actor_id);
        if (!actor || !world.destroy_actor(*actor)) return false;
        factory_.forget(record.actor_id);
        return true;
    }

    bool EditorCommandHistory::undo(World& world)
    {
        if (active_ || undo_.empty()) return false;
        Record record = undo_.back();
        if (!apply(world, record, false))
        {
            TOY_LOG_ERROR("Editor command undo failed for Actor {}.", record.actor_id);
            return false;
        }
        undo_.pop_back();
        redo_.push_back(record);
        return true;
    }

    bool EditorCommandHistory::redo(World& world)
    {
        if (active_ || redo_.empty()) return false;
        Record record = redo_.back();
        if (!apply(world, record, true))
        {
            TOY_LOG_ERROR("Editor command redo failed for Actor {}.", record.actor_id);
            return false;
        }
        redo_.pop_back();
        undo_.push_back(record);
        return true;
    }
}
