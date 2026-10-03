#pragma once

#include "scene/components/component_editor_registry.h"
#include "rendercore/geometry/skeletal_mesh.h"
#include "animation/animation_sequence.h"
#include <cstdint>
#include <vector>

namespace toy3d
{
    class ActorTypeRegistry;

    struct EditorComponentSnapshot
    {
        std::uint32_t component_id = 0;
        std::uint32_t parent_actor_id = 0;
        std::uint32_t parent_component_id = 0;
        SceneComponentData data;
        // History retains CPU geometry; reconstruction creates fresh render resources.
        StaticMeshRef mesh;
        SkeletalMeshRef skeletal_mesh;
        std::shared_ptr<const AnimationSequence> sequence;
        std::vector<MaterialInterfaceRef> material_overrides;
    };

    struct EditorActorState
    {
        bool valid = false;
        std::string actor_type;
        ReflectedValue properties;
        std::uint32_t root_component_id = 0;
        std::vector<EditorComponentSnapshot> components;
    };

    EditorActorState capture_actor_state(const Actor& actor, const ComponentEditorRegistry& editors,
                                         const ActorTypeRegistry* types = nullptr);
    bool apply_actor_state(Actor& actor, const EditorActorState& state, const ComponentEditorRegistry& editors,
                           const ActorTypeRegistry* types = nullptr, bool recreate_mesh_resources = false);
    bool same_actor_state(const EditorActorState& a, const EditorActorState& b);
    bool restore_actor_attachments(Actor& actor, const EditorActorState& state);
} // namespace toy3d
