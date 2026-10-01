#include "scene/editor_command_history.h"

#include <algorithm>
#include "gamescene/actor/actor.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"

namespace toy3d
{
    void EditorCommandHistory::clear()
    {
        cancel();
        undo_.clear();
        redo_.clear();
        world_ = nullptr;
        revision_ = 0;
        next_revision_ = 1;
        saved_revision_ = 0;
        observed_generation_ = 0;
        external_dirty_ = false;
        error_.clear();
    }

    void EditorCommandHistory::bind(World& world)
    {
        if (world_ != &world)
        {
            clear();
            world_ = &world;
            observed_generation_ = world.content_revision();
        }
    }

    void EditorCommandHistory::synchronize(World& world)
    {
        bind(world);
        if (!active_ && observed_generation_ != world.content_revision()) external_dirty_ = true;
        observed_generation_ = world.content_revision();
    }

    void EditorCommandHistory::mark_saved(World& world)
    {
        bind(world);
        saved_revision_ = revision_;
        external_dirty_ = false;
        observed_generation_ = world.content_revision();
    }

    void EditorCommandHistory::acknowledge_rollback(World& world)
    {
        if (world_ == &world) observed_generation_ = world.content_revision();
    }

    bool EditorCommandHistory::dirty(const World& world) const
    {
        return active_ || external_dirty_ || revision_ != saved_revision_ ||
               (world_ == &world && observed_generation_ != world.content_revision());
    }

    void EditorCommandHistory::commit(World& world, Record record)
    {
        record.before_revision = revision_;
        record.after_revision = next_revision_++;
        revision_ = record.after_revision;
        undo_.push_back(std::move(record));
        redo_.clear();
        observed_generation_ = world.content_revision();
    }

    void EditorCommandHistory::begin(World& world, std::uint32_t actor_id, const Transform& before,
                                     EditorTransformSource source)
    {
        if (active_) finish(world, source_);
        if (source == EditorTransformSource::Details) synchronize(world);
        else bind(world);
        Actor* actor = world.find_actor_by_id(actor_id);
        if (!actor || !actor->root_component()) return;
        pending_ = {};
        pending_.actor_id = actor_id;
        pending_.before = factory_.capture(*actor);
        if (!pending_.before.valid) return;
        // Gizmo reports activation after its first delta. Replace just the root
        // Transform in the captured component list with its pre-gesture value.
        for (auto& component : pending_.before.components)
            if (component.component_id == pending_.before.root_component_id) component.data.transform = before;
        active_ = true;
        source_ = source;
    }

    bool EditorCommandHistory::preview_component(World& world, std::uint32_t actor_id,
        std::uint32_t component_id, const SceneComponentData& candidate)
    {
        if (!active_ || pending_.actor_id != actor_id || world_ != &world) return false;
        Actor* actor = world.find_actor_by_id(actor_id);
        auto* component = actor ? dynamic_cast<SceneComponent*>(actor->find_component_by_id(component_id)) : nullptr;
        if (!component || !factory_.component_editors().apply(*component, candidate)) return false;
        observed_generation_ = world.content_revision();
        return true;
    }

    void EditorCommandHistory::finish(World& world, EditorTransformSource source)
    {
        if (!active_for(source)) return;
        active_ = false;
        if (world_ != &world) return;
        Actor* actor = world.find_actor_by_id(pending_.actor_id);
        if (!actor) return;
        pending_.after = factory_.capture(*actor);
        if (pending_.after.valid && !same_actor_state(pending_.before, pending_.after))
            commit(world, std::move(pending_));
        observed_generation_ = world.content_revision();
    }

    void EditorCommandHistory::cancel()
    {
        if (active_ && world_)
        {
            Actor* actor = world_->find_actor_by_id(pending_.actor_id);
            if (actor && !apply_actor_state(*actor, pending_.before, factory_.component_editors()))
                TOY_LOG_ERROR("Could not restore cancelled editor gesture.");
            observed_generation_ = world_->content_revision();
        }
        active_ = false;
        pending_ = {};
    }

    std::uint32_t EditorCommandHistory::place_actor(World& world, const PlacementRequest& request)
    {
        if (active_) return 0;
        synchronize(world);
        Actor* actor = factory_.create(world, request);
        if (!actor) { acknowledge_rollback(world); return 0; }
        Record record;
        record.kind = Kind::Create;
        record.actor_id = actor->actor_id();
        record.component_id = actor->root_component()->component_id();
        record.placement = request;
        record.after = factory_.capture(*actor);
        if (!record.after.valid)
        {
            const auto id = actor->actor_id();
            if (!world.destroy_actor(*actor)) TOY_LOG_ERROR("Placement rollback failed.");
            factory_.forget(id);
            observed_generation_ = world.content_revision();
            return 0;
        }
        commit(world, record);
        return record.actor_id;
    }

    bool EditorCommandHistory::delete_actor(World& world, std::uint32_t actor_id)
    {
        if (active_) return false;
        synchronize(world);
        Actor* actor = world.find_actor_by_id(actor_id);
        Record record;
        if (!actor || !factory_.describe(*actor, record.placement)) return false;
        record.kind = Kind::Delete;
        record.actor_id = actor_id;
        record.component_id = actor->root_component()->component_id();
        record.before = factory_.capture(*actor);
        if (!record.before.valid) return false;
        if (materials_) record.material_assignments = materials_->capture(world, actor_id);
        for (const auto id : world.actor_ids())
        {
            if (id == actor_id) continue;
            auto state = factory_.capture(*world.find_actor_by_id(id));
            const bool attached = std::any_of(state.components.begin(), state.components.end(),
                [actor_id](const EditorComponentSnapshot& value) { return value.parent_actor_id == actor_id; });
            if (attached)
            {
                if (!state.valid) return false;
                record.attached.push_back({id, std::move(state)});
            }
        }
        if (!world.destroy_actor(*actor)) return false;
        factory_.forget(actor_id);
        if (materials_) materials_->forget(actor_id);
        commit(world, std::move(record));
        return true;
    }

    bool EditorCommandHistory::assign_material(World& world, std::uint32_t actor_id,
        std::uint32_t component_id, const std::string& slot_name, const AssetRef& material, std::string& error)
    {
        if (active_ || !materials_) { error = "Finish the active property edit first."; return false; }
        synchronize(world);
        Actor* actor = world.find_actor_by_id(actor_id);
        auto* component = actor ? dynamic_cast<StaticMeshComponent*>(actor->find_component_by_id(component_id)) : nullptr;
        if (!component || !component->static_mesh()) { error = "Material target no longer exists."; return false; }
        const auto& names = component->static_mesh()->material_slot_names();
        const auto slot = std::find(names.begin(), names.end(), slot_name);
        if (slot == names.end()) { error = "Material slot no longer exists."; return false; }
        const AssetRef before = materials_->reference(world, actor_id, component_id, slot_name);
        if (component->has_material_override(static_cast<std::uint32_t>(slot - names.begin())) != before.asset_id.valid())
        { error = "Runtime override has no Editor asset identity."; return false; }
        if (before.asset_id == material.asset_id && before.subresource_id == material.subresource_id &&
            before.expected_type == material.expected_type && before.strength == material.strength) return true;
        if (!materials_->assign(world, actor_id, {component_id, slot_name, material}, error)) return false;
        Record record;
        record.kind = Kind::Material;
        record.actor_id = actor_id;
        record.component_id = component_id;
        record.slot_name = slot_name;
        record.material_before = before;
        record.material_after = material;
        commit(world, std::move(record));
        return true;
    }

    void EditorCommandHistory::remap_actor(std::uint32_t old_id, std::uint32_t new_id,
        const std::map<std::uint32_t, std::uint32_t>& ids, Record& current)
    {
        auto remap_state = [&](EditorActorState& state, bool owned)
        {
            if (owned)
            {
                const auto root = ids.find(state.root_component_id);
                if (root != ids.end()) state.root_component_id = root->second;
            }
            for (auto& component : state.components)
            {
                const auto local = ids.find(component.component_id);
                if (owned && local != ids.end()) component.component_id = local->second;
                if (component.parent_actor_id == old_id)
                {
                    component.parent_actor_id = new_id;
                    const auto parent = ids.find(component.parent_component_id);
                    if (parent != ids.end()) component.parent_component_id = parent->second;
                }
            }
        };
        auto remap_record = [&](Record& record)
        {
            const bool owned = record.actor_id == old_id;
            remap_state(record.before, owned);
            remap_state(record.after, owned);
            for (auto& attached : record.attached)
            {
                remap_state(attached.state, attached.actor_id == old_id);
                if (attached.actor_id == old_id) attached.actor_id = new_id;
            }
            if (owned)
            {
                record.actor_id = new_id;
                const auto component = ids.find(record.component_id);
                if (component != ids.end()) record.component_id = component->second;
                for (auto& assignment : record.material_assignments)
                {
                    const auto target = ids.find(assignment.component_id);
                    if (target != ids.end()) assignment.component_id = target->second;
                }
            }
        };
        remap_record(current);
        for (auto& record : undo_) remap_record(record);
        for (auto& record : redo_) remap_record(record);
        if (remap_) remap_(old_id, new_id, ids);
    }

    bool EditorCommandHistory::apply(World& world, Record& record, bool forward)
    {
        if (world_ != &world) return false;
        if (record.kind == Kind::Material)
        {
            return materials_ && materials_->assign(world, record.actor_id,
                {record.component_id, record.slot_name, forward ? record.material_after : record.material_before}, error_);
        }
        if (record.kind == Kind::Modify)
        {
            Actor* actor = world.find_actor_by_id(record.actor_id);
            return actor && apply_actor_state(*actor, forward ? record.after : record.before, factory_.component_editors());
        }
        const bool create = (record.kind == Kind::Create) == forward;
        if (create)
        {
            std::map<std::uint32_t, std::uint32_t> ids;
            Actor* actor = factory_.restore(world, record.placement, forward ? record.after : record.before, ids);
            if (!actor)
            {
                error_ = "Could not reconstruct the Actor and its recorded components.";
                acknowledge_rollback(world);
                return false;
            }
            bool restored = true;
            std::string error;
            for (const auto& assignment : record.material_assignments)
            {
                const auto target = ids.find(assignment.component_id);
                if (target == ids.end() || !materials_ || !materials_->assign(world, actor->actor_id(),
                    {target->second, assignment.slot_name, assignment.material}, error)) { restored = false; break; }
            }
            if (!restored)
            {
                error_ = error.empty() ? "Could not restore a recorded material slot." : error;
                const auto id = actor->actor_id();
                if (!world.destroy_actor(*actor)) TOY_LOG_ERROR("History reconstruction rollback failed.");
                factory_.forget(id);
                if (materials_) materials_->forget(id);
                acknowledge_rollback(world);
                return false;
            }
            std::vector<AttachedActor> previous_children;
            auto rollback = [&]()
            {
                const auto id = actor->actor_id();
                if (!world.destroy_actor(*actor)) TOY_LOG_ERROR("History candidate rollback failed.");
                factory_.forget(id);
                if (materials_) materials_->forget(id);
                for (const auto& saved : previous_children)
                {
                    Actor* child = world.find_actor_by_id(saved.actor_id);
                    if (!child || !apply_actor_state(*child, saved.state, factory_.component_editors()) ||
                        !restore_actor_attachments(*child, saved.state)) TOY_LOG_ERROR("History child rollback failed.");
                }
                acknowledge_rollback(world);
            };
            for (const auto& attached : record.attached)
            {
                Actor* child = world.find_actor_by_id(attached.actor_id);
                if (!child) { rollback(); return false; }
                auto previous = factory_.capture(*child);
                if (!previous.valid) { rollback(); return false; }
                previous_children.push_back({attached.actor_id, std::move(previous)});
                EditorActorState target = attached.state;
                for (auto& component : target.components)
                    if (component.parent_actor_id == record.actor_id)
                    {
                        component.parent_actor_id = actor->actor_id();
                        const auto parent = ids.find(component.parent_component_id);
                        if (parent == ids.end()) { rollback(); return false; }
                        component.parent_component_id = parent->second;
                    }
                if (!apply_actor_state(*child, target, factory_.component_editors()) ||
                    !restore_actor_attachments(*child, target)) { rollback(); return false; }
            }
            // Publish ID remapping only after all candidate objects and attachments succeed.
            remap_actor(record.actor_id, actor->actor_id(), ids, record);
            return true;
        }
        Actor* actor = world.find_actor_by_id(record.actor_id);
        if (!actor || !world.destroy_actor(*actor)) return false;
        factory_.forget(record.actor_id);
        if (materials_) materials_->forget(record.actor_id);
        return true;
    }

    bool EditorCommandHistory::undo(World& world)
    {
        error_.clear();
        if (active_) { error_ = "Finish the active gesture before undo."; return false; }
        if (undo_.empty()) return false;
        synchronize(world);
        Record record = undo_.back();
        if (!apply(world, record, false))
        {
            if (error_.empty()) error_ = "Undo failed: an Actor, component or attachment could not be restored.";
            return false;
        }
        undo_.pop_back();
        revision_ = record.before_revision;
        redo_.push_back(std::move(record));
        observed_generation_ = world.content_revision();
        return true;
    }

    bool EditorCommandHistory::redo(World& world)
    {
        error_.clear();
        if (active_) { error_ = "Finish the active gesture before redo."; return false; }
        if (redo_.empty()) return false;
        synchronize(world);
        Record record = redo_.back();
        if (!apply(world, record, true))
        {
            if (error_.empty()) error_ = "Redo failed: an Actor, component or attachment could not be restored.";
            return false;
        }
        redo_.pop_back();
        revision_ = record.after_revision;
        undo_.push_back(std::move(record));
        observed_generation_ = world.content_revision();
        return true;
    }
}
