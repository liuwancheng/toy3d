#pragma once

#include "components/component_editor_registry.h"
#include <cstdint>
#include <vector>

namespace toy3d
{
    struct EditorComponentSnapshot
    {
        std::uint32_t component_id = 0;
        std::uint32_t parent_actor_id = 0;
        std::uint32_t parent_component_id = 0;
        SceneComponentData data;
        // History retains CPU geometry; reconstruction creates fresh render resources.
        StaticMeshRef mesh;
    };

    struct EditorActorState
    {
        bool valid = false;
        std::uint32_t root_component_id = 0;
        std::vector<EditorComponentSnapshot> components;
    };

    EditorActorState capture_actor_state(const Actor& actor, const ComponentEditorRegistry& editors);
    bool apply_actor_state(Actor& actor, const EditorActorState& state, const ComponentEditorRegistry& editors);
    bool same_actor_state(const EditorActorState& a, const EditorActorState& b);
    bool restore_actor_attachments(Actor& actor, const EditorActorState& state);
}
