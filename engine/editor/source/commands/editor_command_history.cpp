#include "commands/editor_command_history.h"

#include <algorithm>

#include "gamescene/actor/actor.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"

namespace toy3d
{
    namespace
    {
        bool same_state(const EditorActorState& a, const EditorActorState& b)
        {
            return a.transform.translation == b.transform.translation && a.transform.rotation == b.transform.rotation &&
                   a.transform.scale == b.transform.scale &&
                   a.primitive_cast_shadows == b.primitive_cast_shadows &&
                   a.primitive_receives_shadows == b.primitive_receives_shadows &&
                   a.light_enabled == b.light_enabled &&
                   a.light_color == b.light_color && a.light_intensity == b.light_intensity && a.light_range == b.light_range &&
                   a.light_priority == b.light_priority && a.shadow_cast_shadows == b.shadow_cast_shadows &&
                   a.shadow_cascade_count == b.shadow_cascade_count &&
                   a.cascade_distribution_exponent == b.cascade_distribution_exponent &&
                   a.shadow_map_resolution == b.shadow_map_resolution &&
                   a.shadow_distance == b.shadow_distance &&
                   a.shadow_distance_fade_fraction == b.shadow_distance_fade_fraction &&
                   a.shadow_bias == b.shadow_bias && a.shadow_slope_bias == b.shadow_slope_bias &&
                   a.shadow_receiver_bias == b.shadow_receiver_bias &&
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
        record.component_id = actor->root_component()->component_id();
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
        record.component_id = actor->root_component()->component_id();
        record.before = capture_actor_state(*actor);
        if (materials_) record.material_assignments = materials_->capture(world, actor_id);
        if (!world.destroy_actor(*actor)) return false;
        factory_.forget(actor_id);
        if (materials_) materials_->forget(actor_id);
        undo_.push_back(record);
        redo_.clear();
        return true;
    }

    bool EditorCommandHistory::assign_material(World& world, std::uint32_t actor_id, std::uint32_t component_id,
        const std::string& slot_name, const AssetRef& material, std::string& error)
    {
        error.clear();
        if (active_ || !materials_) { error = "Finish the active property edit before assigning a material."; return false; }
        bind(world);
        Actor* actor = world.find_actor_by_id(actor_id);
        auto* component = actor ? dynamic_cast<StaticMeshComponent*>(actor->root_component()) : nullptr;
        if (!component || component->component_id() != component_id || !component->static_mesh())
        { error = "The target root StaticMesh Component no longer exists."; return false; }
        const auto& names = component->static_mesh()->material_slot_names();
        const auto found = std::find(names.begin(), names.end(), slot_name);
        if (found == names.end()) { error = "The target material slot no longer exists: " + slot_name; return false; }
        const auto slot = static_cast<std::uint32_t>(found - names.begin());
        const AssetRef before = materials_->reference(world, actor_id, component_id, slot_name);
        if (component->has_material_override(slot) != before.asset_id.valid())
        { error = "This slot has a runtime override without an Editor asset reference."; return false; }
        if (before.asset_id == material.asset_id && before.subresource_id == material.subresource_id &&
            before.expected_type == material.expected_type && before.strength == material.strength) return true;
        if (!materials_->assign(world, actor_id, {component_id, slot_name, material}, error)) return false;
        Record record;
        record.kind = Kind::Material; record.actor_id = actor_id; record.component_id = component_id;
        record.slot_name = slot_name; record.material_before = before; record.material_after = material;
        undo_.push_back(record); redo_.clear();
        return true;
    }

    void EditorCommandHistory::remap_actor(std::uint32_t old_id, std::uint32_t new_id,
        std::uint32_t old_component_id, std::uint32_t new_component_id)
    {
        // Runtime Actor and Component IDs are never reused. Rebind identities
        // across the whole timeline, including deletion snapshots, not pointers.
        auto remap = [old_id, new_id, old_component_id, new_component_id](Record& record)
        {
            if (record.actor_id != old_id) return;
            record.actor_id = new_id;
            if (record.component_id == old_component_id) record.component_id = new_component_id;
            for (auto& assignment : record.material_assignments)
                if (assignment.component_id == old_component_id) assignment.component_id = new_component_id;
        };
        for (Record& record : undo_) remap(record);
        for (Record& record : redo_) remap(record);
    }

    bool EditorCommandHistory::apply(World& world, Record& record, bool forward)
    {
        if (world_ != &world) return false;
        if (record.kind == Kind::Material)
        {
            std::string error;
            const bool applied = materials_ && materials_->assign(world, record.actor_id,
                {record.component_id, record.slot_name, forward ? record.material_after : record.material_before}, error);
            if (!applied) TOY_LOG_ERROR("Material history rejected Actor {} slot {}: {}", record.actor_id, record.slot_name, error);
            return applied;
        }
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
            bool restored = apply_actor_state(*actor, forward ? record.after : record.before);
            std::string error;
            for (const auto& assignment : record.material_assignments)
            {
                if (!restored) break;
                if (assignment.component_id != record.component_id) { restored = false; break; }
                restored = materials_ && materials_->assign(world, actor->actor_id(),
                    {actor->root_component()->component_id(), assignment.slot_name, assignment.material}, error);
            }
            if (!restored)
            {
                if (!error.empty()) TOY_LOG_ERROR("Actor material reconstruction failed: {}", error);
                const auto candidate_id = actor->actor_id();
                if (!world.destroy_actor(*actor)) TOY_LOG_ERROR("History reconstruction rollback failed.");
                factory_.forget(candidate_id);
                if (materials_) materials_->forget(candidate_id);
                return false;
            }
            const auto old_id = record.actor_id;
            const auto old_component_id = record.component_id;
            record.actor_id = actor->actor_id();
            record.component_id = actor->root_component()->component_id();
            for (auto& assignment : record.material_assignments) assignment.component_id = record.component_id;
            remap_actor(old_id, record.actor_id, old_component_id, record.component_id);
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
