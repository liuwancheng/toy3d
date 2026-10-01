#include "scene/editor_actor_state.h"

#include "scene_asset/scene_asset.h"

#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"
#include "gamescene/component/static_mesh_component.h"

namespace toy3d
{
    EditorActorState capture_actor_state(const Actor& actor, const ComponentEditorRegistry& editors)
    {
        EditorActorState result;
        if (!actor.root_component()) return result;
        result.root_component_id = actor.root_component()->component_id();
        for (const auto id : actor.component_ids())
        {
            const auto* component = dynamic_cast<const SceneComponent*>(actor.find_component_by_id(id));
            if (!component) return result;
            EditorComponentSnapshot snapshot;
            snapshot.component_id = id;
            if (!editors.capture(*component, snapshot.data)) return result;
            if (const auto* mesh = dynamic_cast<const StaticMeshComponent*>(component)) snapshot.mesh = mesh->static_mesh();
            if (component->parent())
            {
                snapshot.parent_actor_id = component->parent()->owner().actor_id();
                snapshot.parent_component_id = component->parent()->component_id();
            }
            result.components.push_back(std::move(snapshot));
        }
        result.valid = true;
        return result;
    }

    bool apply_actor_state(Actor& actor, const EditorActorState& state, const ComponentEditorRegistry& editors)
    {
        if (!state.valid || actor.component_count() != state.components.size() ||
            !actor.root_component() || actor.root_component()->component_id() != state.root_component_id) return false;
        // Validate every component before any setter, so a bad camera/light candidate
        // cannot partially change an earlier component's Transform.
        for (const auto& snapshot : state.components)
        {
            const auto* component = dynamic_cast<const SceneComponent*>(actor.find_component_by_id(snapshot.component_id));
            const auto* editor = component ? editors.find(*component) : nullptr;
            if (!editor || editor->persistent_type != snapshot.data.type || !validate_component_data(snapshot.data)) return false;
        }
        for (const auto& snapshot : state.components)
        {
            auto* component = static_cast<SceneComponent*>(actor.find_component_by_id(snapshot.component_id));
            if (!editors.apply(*component, snapshot.data)) return false;
        }
        return true;
    }

    bool restore_actor_attachments(Actor& actor, const EditorActorState& state)
    {
        for (const auto& snapshot : state.components)
        {
            auto* component = dynamic_cast<SceneComponent*>(actor.find_component_by_id(snapshot.component_id));
            if (!component) return false;
            SceneComponent* parent = nullptr;
            if (snapshot.parent_component_id != 0)
            {
                const Actor* owner = actor.world().find_actor_by_id(snapshot.parent_actor_id);
                parent = owner ? dynamic_cast<SceneComponent*>(owner->find_component_by_id(snapshot.parent_component_id)) : nullptr;
                if (!parent) return false;
            }
            if (!component->attach_to(parent, AttachmentRule::KeepRelative)) return false;
        }
        return true;
    }

    bool same_actor_state(const EditorActorState& a, const EditorActorState& b)
    {
        if (!a.valid || !b.valid || a.root_component_id != b.root_component_id ||
            a.components.size() != b.components.size()) return false;
        // Generated codecs compare complete typed properties, so a new settings
        // field participates without expanding a central field-by-field comparison.
        for (std::size_t i = 0; i < a.components.size(); ++i)
        {
            const auto& left = a.components[i];
            const auto& right = b.components[i];
            if (left.component_id != right.component_id || left.parent_actor_id != right.parent_actor_id ||
                left.parent_component_id != right.parent_component_id || left.mesh != right.mesh) return false;
            ValueWriter left_bytes;
            ValueWriter right_bytes;
            if (!encode_value(left_bytes, left.data).succeeded() || !encode_value(right_bytes, right.data).succeeded() ||
                left_bytes.bytes() != right_bytes.bytes()) return false;
        }
        return true;
    }
}
