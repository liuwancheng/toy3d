#include "scene/editor_actor_state.h"

#include "asset/scene/scene_asset.h"
#include "gamescene/actor/actor_type_registry.h"

#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/component/skeletal_mesh_component.h"
#include "gamescene/scene_geometry.h"

namespace toy3d
{
    EditorActorState capture_actor_state(const Actor& actor, const ComponentEditorRegistry& editors,
                                         const ActorTypeRegistry* types)
    {
        EditorActorState result;
        if (!actor.root_component())
        {
            return result;
        }
        if (types)
        {
            const auto* type = types->find(actor);
            if (!type || !types->capture(actor, result.properties))
            {
                return result;
            }
            result.actor_type = type->name;
        }
        result.root_component_id = actor.root_component()->component_id();
        for (const auto id : actor.component_ids())
        {
            const auto* component = dynamic_cast<const SceneComponent*>(actor.find_component_by_id(id));
            if (!component)
            {
                return result;
            }
            EditorComponentSnapshot snapshot;
            snapshot.component_id = id;
            if (!editors.capture(*component, snapshot.data))
            {
                return result;
            }
            if (const auto* mesh = dynamic_cast<const StaticMeshComponent*>(component))
            {
                snapshot.mesh = mesh->static_mesh();
            }
            else if (const auto* mesh = dynamic_cast<const SkeletalMeshComponent*>(component))
            {
                snapshot.skeletal_mesh = mesh->skeletal_mesh();
                snapshot.sequence = mesh->animation_sequence();
            }
            if (const auto* mesh = dynamic_cast<const MeshComponent*>(component))
            {
                for (std::uint32_t slot = 0; slot < mesh->material_slot_names().size(); ++slot)
                {
                    snapshot.material_overrides.push_back(
                        mesh->has_material_override(slot) ? mesh->material_for_slot(slot) : nullptr);
                }
            }
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

    bool apply_actor_state(Actor& actor, const EditorActorState& state, const ComponentEditorRegistry& editors,
                           const ActorTypeRegistry* types, bool recreate_mesh_resources)
    {
        if (!state.valid || actor.component_count() != state.components.size() || !actor.root_component() ||
            actor.root_component()->component_id() != state.root_component_id)
        {
            return false;
        }
        if (types && !state.actor_type.empty())
        {
            const auto* type = types->find(actor);
            if (!type || type->name != state.actor_type || !types->validate(state.actor_type, state.properties))
            {
                return false;
            }
        }
        // Validate every component before any setter, so a bad camera/light candidate
        // cannot partially change an earlier component's Transform.
        for (const auto& snapshot : state.components)
        {
            const auto* component =
                dynamic_cast<const SceneComponent*>(actor.find_component_by_id(snapshot.component_id));
            const auto* editor = component ? editors.find(*component) : nullptr;
            if (!editor || editor->persistent_type != snapshot.data.type || !validate_component_data(snapshot.data))
            {
                return false;
            }
            const auto* static_data = std::get_if<SceneMeshData>(&snapshot.data.properties);
            const auto* skeletal_data = std::get_if<SceneSkeletalMeshData>(&snapshot.data.properties);
            if ((snapshot.skeletal_mesh && !skeletal_data) || (snapshot.mesh && !static_data) ||
                (static_data && (snapshot.skeletal_mesh || snapshot.sequence)) ||
                (skeletal_data && (snapshot.mesh || (!snapshot.skeletal_mesh && snapshot.sequence))))
            {
                return false;
            }
            if (static_data || skeletal_data)
            {
                const auto& settings = static_data ? static_data->settings : skeletal_data->settings;
                const auto* mesh_component = dynamic_cast<const MeshComponent*>(component);
                static const std::vector<MaterialInterfaceRef> empty_materials;
                const auto& defaults = snapshot.mesh            ? snapshot.mesh->material_slots()
                                       : snapshot.skeletal_mesh ? snapshot.skeletal_mesh->material_slots()
                                                                : empty_materials;
                if (!mesh_component || snapshot.material_overrides.size() != defaults.size())
                {
                    return false;
                }
                const auto factory =
                    skeletal_data ? shader::VertexFactoryType::GPUSkin : shader::VertexFactoryType::Local;
                const bool colors =
                    snapshot.skeletal_mesh || (snapshot.mesh && !snapshot.mesh->vertex_colors().empty());
                const bool tangent = snapshot.skeletal_mesh
                                         ? snapshot.skeletal_mesh->asset().geometry.mesh.valid_tangent_frame
                                         : snapshot.mesh && snapshot.mesh->has_valid_tangent_frame();
                for (std::size_t slot = 0; slot < defaults.size(); ++slot)
                {
                    std::string error;
                    const auto& effective =
                        snapshot.material_overrides[slot] ? snapshot.material_overrides[slot] : defaults[slot];
                    if (!defaults[slot] || !effective ||
                        !validate_material_geometry(effective->desc(), factory, colors, tangent, error) ||
                        !validate_material_mesh_pass(effective->desc(), shader::ShaderPassRole::HitProxy, factory,
                                                     error) ||
                        ((settings.cast_shadows || mesh_component->cast_shadows()) &&
                         (!validate_material_mesh_pass(effective->desc(), shader::ShaderPassRole::ShadowDepth, factory,
                                                       error) ||
                          !validate_material_mesh_pass(defaults[slot]->desc(), shader::ShaderPassRole::ShadowDepth,
                                                       factory, error))))
                    {
                        return false;
                    }
                }
            }
            if (snapshot.skeletal_mesh)
            {
                const auto* data = std::get_if<SceneSkeletalMeshData>(&snapshot.data.properties);
                AnimationInstance candidate;
                std::vector<AnimationSequenceInput> inputs;
                if (snapshot.sequence)
                {
                    inputs.push_back({snapshot.sequence, data->playback});
                }
                if (!candidate.set_sources(snapshot.skeletal_mesh->bone_layout(), inputs).succeeded() ||
                    !candidate
                         .update({0.0, snapshot.sequence ? std::vector<double>{1.0} : std::vector<double>{},
                                  data->lock_root})
                         .succeeded() ||
                    !candidate.evaluate().succeeded())
                {
                    return false;
                }
            }
        }
        if (types && !state.actor_type.empty() && !types->apply(actor, state.properties))
        {
            return false;
        }
        for (const auto& snapshot : state.components)
        {
            auto* component = static_cast<SceneComponent*>(actor.find_component_by_id(snapshot.component_id));
            if (!editors.apply(*component, snapshot.data))
            {
                return false;
            }
            if (auto* mesh = dynamic_cast<StaticMeshComponent*>(component))
            {
                if (mesh->static_mesh() != snapshot.mesh)
                {
                    // Removed StaticMesh render resources cannot be reinitialized. History
                    // retains CPU geometry, and reconstruction creates a fresh resource owner.
                    mesh->set_static_mesh(recreate_mesh_resources && snapshot.mesh ? clone_scene_geometry(snapshot.mesh)
                                                                                   : snapshot.mesh);
                }
            }
            else if (auto* mesh = dynamic_cast<SkeletalMeshComponent*>(component))
            {
                const auto status = mesh->skeletal_mesh() != snapshot.skeletal_mesh
                                        ? mesh->set_assets(snapshot.skeletal_mesh, snapshot.sequence)
                                    : mesh->animation_sequence() != snapshot.sequence
                                        ? mesh->set_animation(snapshot.sequence)
                                        : AssetStatus::success();
                if (!status.succeeded())
                {
                    return false;
                }
            }
            if (auto* mesh = dynamic_cast<MeshComponent*>(component))
            {
                for (std::uint32_t slot = 0; slot < snapshot.material_overrides.size(); ++slot)
                {
                    const auto& material = snapshot.material_overrides[slot];
                    if (!(material ? mesh->set_material_override(slot, material) : mesh->clear_material_override(slot)))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool restore_actor_attachments(Actor& actor, const EditorActorState& state)
    {
        for (const auto& snapshot : state.components)
        {
            auto* component = dynamic_cast<SceneComponent*>(actor.find_component_by_id(snapshot.component_id));
            if (!component)
            {
                return false;
            }
            SceneComponent* parent = nullptr;
            if (snapshot.parent_component_id != 0)
            {
                const Actor* owner = actor.world().find_actor_by_id(snapshot.parent_actor_id);
                parent = owner
                             ? dynamic_cast<SceneComponent*>(owner->find_component_by_id(snapshot.parent_component_id))
                             : nullptr;
                if (!parent)
                {
                    return false;
                }
            }
            if (!component->attach_to(parent, AttachmentRule::KeepRelative))
            {
                return false;
            }
        }
        return true;
    }

    bool same_actor_state(const EditorActorState& a, const EditorActorState& b)
    {
        if (!a.valid || !b.valid || a.actor_type != b.actor_type || a.properties.type != b.properties.type ||
            a.properties.schema_version != b.properties.schema_version || a.properties.bytes != b.properties.bytes ||
            a.root_component_id != b.root_component_id || a.components.size() != b.components.size())
        {
            return false;
        }
        // Generated codecs compare complete typed properties, so a new settings
        // field participates without expanding a central field-by-field comparison.
        for (std::size_t i = 0; i < a.components.size(); ++i)
        {
            const auto& left = a.components[i];
            const auto& right = b.components[i];
            if (left.component_id != right.component_id || left.parent_actor_id != right.parent_actor_id ||
                left.parent_component_id != right.parent_component_id || left.mesh != right.mesh ||
                left.skeletal_mesh != right.skeletal_mesh || left.sequence != right.sequence ||
                left.material_overrides != right.material_overrides)
            {
                return false;
            }
            ValueWriter left_bytes;
            ValueWriter right_bytes;
            if (!encode_value(left_bytes, left.data).succeeded() ||
                !encode_value(right_bytes, right.data).succeeded() || left_bytes.bytes() != right_bytes.bytes())
            {
                return false;
            }
        }
        return true;
    }
} // namespace toy3d
