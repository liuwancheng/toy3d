#include "scene/editor_scene_session.h"
#include "asset_loader/asset_loader.h"
#include "rendercore/geometry/static_mesh_load_job.h"
#include "rendercore/texture/texture_load_job.h"
#include "gamescene/scene_assembly.h"

#include <algorithm>
#include <chrono>
#include "asset/asset_descriptor_path.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/component/skeletal_mesh_component.h"
#include "rendercore/geometry/skeletal_mesh_asset_loader.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"
#include "scene/editor_selection.h"
#include "viewport/scene_viewport.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        const char* actor_kind(PlacementItemId kind)
        {
            switch (kind)
            {
            case PlacementItemId::EmptyActor:
                return "EmptyActor";
            case PlacementItemId::Cube:
                return "Cube";
            case PlacementItemId::Plane:
                return "Plane";
            case PlacementItemId::StaticMesh:
                return "StaticMesh";
            case PlacementItemId::SkeletalMesh:
                return "SkeletalMesh";
            case PlacementItemId::DirectionalLight:
                return "DirectionalLight";
            case PlacementItemId::PointLight:
                return "PointLight";
            case PlacementItemId::Camera:
                return "Camera";
            }
            return "";
        }
        bool placement_kind(const std::string& name, PlacementItemId& kind)
        {
            for (const auto& item : placement_catalog())
            {
                if (name == actor_kind(item.id))
                {
                    kind = item.id;
                    return true;
                }
            }
            if (name == "StaticMesh")
            {
                kind = PlacementItemId::StaticMesh;
                return true;
            }
            return false;
        }
        bool ensure_identity(std::map<std::uint32_t, std::string>& identities, std::uint32_t id)
        {
            if (identities.count(id))
            {
                return true;
            }
            AssetId stable;
            if (!AssetId::try_generate(stable))
            {
                return false;
            }
            identities.emplace(id, stable.hex());
            return true;
        }
    } // namespace

    EditorSceneSession::EditorSceneSession(EditorWorkspace& workspace, ActorFactory& factory,
                                           MaterialAssignments& materials, EditorSelection& selection,
                                           SceneViewport& viewport)
        : workspace_(workspace), factory_(factory), materials_(materials), selection_(selection), viewport_(viewport),
          history_(factory, materials)
    {
        history_.set_identity_remap(
            [this](std::uint32_t old_id, std::uint32_t new_id, const std::map<std::uint32_t, std::uint32_t>& components)
            {
                remap(old_id, new_id, components);
            });
    }

    void EditorSceneSession::bind(World& world)
    {
        if (world_ && world_ != &world)
        {
            clear_interaction();
            actor_ids_.clear();
            component_ids_.clear();
            published_bytes_.clear();
            asset_id_ = {};
            path_ = {};
            error_.clear();
        }
        world_ = &world;
        history_.synchronize(world);
    }

    void EditorSceneSession::remap(std::uint32_t old_id, std::uint32_t new_id,
                                   const std::map<std::uint32_t, std::uint32_t>& components)
    {
        const auto actor = actor_ids_.find(old_id);
        if (actor != actor_ids_.end())
        {
            actor_ids_[new_id] = actor->second;
            actor_ids_.erase(actor);
        }
        for (const auto& component : components)
        {
            const auto found = component_ids_.find(component.first);
            if (found != component_ids_.end())
            {
                component_ids_[component.second] = found->second;
                component_ids_.erase(found);
            }
        }
    }

    bool EditorSceneSession::dirty() const
    {
        if (!world_)
        {
            return false;
        }
        return path_.empty() ? world_->actor_count() != 0 : history_.dirty(*world_);
    }

    void EditorSceneSession::clear_interaction()
    {
        history_.clear();
        selection_.clear_actor();
        viewport_.exit_camera_view();
        viewport_.cancel_pending_hit();
    }

    bool EditorSceneSession::capture(SceneAssetData& data)
    {
        if (!world_)
        {
            error_ = "Scene session has no World.";
            return false;
        }
        SceneAssetData candidate;
        candidate.environment = world_->environment_settings();
        for (const auto id : world_->actor_ids())
        {
            const Actor* actor = world_->find_actor_by_id(id);
            if (!actor || !ensure_identity(actor_ids_, id))
            {
                error_ = "Could not allocate Actor identity.";
                return false;
            }
            for (const auto component : actor->component_ids())
            {
                if (!ensure_identity(component_ids_, component))
                {
                    error_ = "Could not allocate Component identity.";
                    return false;
                }
            }
        }
        for (const auto id : world_->actor_ids())
        {
            const Actor& actor = *world_->find_actor_by_id(id);
            PlacementRequest placement;
            if (!factory_.describe(actor, placement))
            {
                error_ = "Actor type has no registered scene construction path.";
                return false;
            }
            const auto state = factory_.capture(actor);
            if (!state.valid)
            {
                error_ = "Actor contains an unsupported Component.";
                return false;
            }
            SceneActorData saved;
            saved.id = actor_ids_.at(id);
            saved.kind = placement.actor_type.empty() ? actor_kind(placement.item) : "Custom";
            saved.type = state.actor_type;
            saved.properties = state.properties;
            saved.root_component_id = component_ids_.at(state.root_component_id);
            const auto assignments = materials_.capture(*world_, id);
            for (const auto& snapshot : state.components)
            {
                SceneComponentData component = snapshot.data;
                component.id = component_ids_.at(snapshot.component_id);
                if (snapshot.parent_component_id)
                {
                    const auto parent = component_ids_.find(snapshot.parent_component_id);
                    if (parent == component_ids_.end())
                    {
                        error_ = "Attachment target is absent.";
                        return false;
                    }
                    component.parent_component_id = parent->second;
                }
                // C++17 get_if selects the independent mesh author schemas.
                auto* static_data = std::get_if<SceneMeshData>(&component.properties);
                auto* skeletal_data = std::get_if<SceneSkeletalMeshData>(&component.properties);
                if (static_data || skeletal_data)
                {
                    const auto* runtime =
                        dynamic_cast<const MeshComponent*>(actor.find_component_by_id(snapshot.component_id));
                    if (!runtime || !(static_data ? factory_.mesh_source(*runtime, *static_data)
                                                  : factory_.mesh_source(*runtime, *skeletal_data)))
                    {
                        error_ = "Mesh geometry/animation has no registered author source.";
                        return false;
                    }
                    auto& resources = static_data ? static_data->resources : skeletal_data->resources;
                    const auto& slots = runtime->material_slot_names();
                    for (std::size_t slot = 0; slot < slots.size(); ++slot)
                    {
                        const auto found = std::find_if(assignments.begin(), assignments.end(),
                                                        [&](const MaterialSlotAssignment& value)
                                                        {
                                                            return value.component_id == snapshot.component_id &&
                                                                   value.slot_name == slots[slot];
                                                        });
                        if (runtime->has_material_override(static_cast<std::uint32_t>(slot)) !=
                            (found != assignments.end()))
                        {
                            error_ = "Runtime Material override has no author Asset reference.";
                            return false;
                        }
                        if (found != assignments.end())
                        {
                            resources.push_back({"material:" + slots[slot], found->material});
                        }
                    }
                    for (const auto& assignment : assignments)
                    {
                        if (assignment.component_id == snapshot.component_id &&
                            std::find(slots.begin(), slots.end(), assignment.slot_name) == slots.end())
                        {
                            error_ = "Material assignment targets an unknown slot.";
                            return false;
                        }
                    }
                }
                saved.components.push_back(std::move(component));
            }
            candidate.actors.push_back(std::move(saved));
        }
        const auto valid = validate_scene_asset(candidate, &workspace_.catalog().index, &workspace_.types());
        if (!valid.succeeded())
        {
            error_ = valid.message;
            return false;
        }
        data = std::move(candidate);
        error_.clear();
        return true;
    }

    bool EditorSceneSession::replace(const SceneAssetData& data)
    {
        if (!world_)
        {
            error_ = "Scene session has no World.";
            return false;
        }
        if (history_.active())
        {
            error_ = "Finish the active gesture before replacing the Scene.";
            return false;
        }
        if (!factory_.actor_types().frozen() && !factory_.actor_types().freeze(workspace_.types()))
        {
            error_ = "Scene Actor types could not freeze.";
            return false;
        }
        history_.synchronize(*world_);
        const auto valid = validate_scene_asset(data, &workspace_.catalog().index, &workspace_.types());
        if (!valid.succeeded())
        {
            error_ = valid.message;
            return false;
        }
        SceneAssemblyServices services;
        services.load_environment = [this](const AssetRef& reference, std::string& error) -> TextureRef
        {
            if (assets_ == nullptr)
            {
                error = "Scene environment: the asset loader is unavailable.";
                return {};
            }
            // Assembly cannot continue without the World environment, so this one Critical
            // decode is waited for; preview windows keep polling instead.
            return load_assembly_texture(*assets_, reference, workspace_.catalog().index, error);
        };
        services.load_mesh = [&](const SceneMeshData& mesh, std::string& problem) -> StaticMeshRef
        {
            if (!mesh.builtin_mesh.empty())
            {
                return factory_.instantiate_builtin(mesh.builtin_mesh);
            }
            const auto source = std::find_if(mesh.resources.begin(), mesh.resources.end(),
                                             [](const SceneResourceBinding& value)
                                             {
                                                 return value.role == "mesh";
                                             });
            if (source == mesh.resources.end())
            {
                problem = "Scene mesh asset is missing.";
                return {};
            }
            if (assets_ == nullptr)
            {
                problem = "Scene mesh: the asset loader is unavailable.";
                return {};
            }
            // Assembly cannot continue without the mesh, so this one Critical decode is waited
            // for on the loader thread instead of being read and built inline here.
            return load_assembly_static_mesh(*assets_, source->reference, workspace_.catalog().index,
                                             factory_.default_material(), factory_.material_resolver(), problem);
        };
        services.load_skeletal_mesh = [this](const SceneSkeletalMeshData& mesh)
        {
            return load_skeletal_mesh_assets(workspace_.types(), workspace_.files(), workspace_.catalog().index, mesh,
                                             factory_.default_material(), factory_.material_resolver());
        };
        services.assign_material = [&](Actor& actor, MeshComponent& component, const std::string& slot,
                                       const AssetRef& material, std::string& problem)
        {
            return materials_.assign(*world_, actor.actor_id(), {component.component_id(), slot, material}, problem);
        };
        services.forget_actor = [&](std::uint32_t id)
        {
            factory_.forget(id);
            materials_.forget(id);
        };
        const auto old = world_->actor_ids();
        SceneAssemblyResult assembled;
        if (!assemble_scene(*world_, data, factory_.actor_types(), workspace_.types(), services, assembled, error_,
                            &workspace_.catalog().index))
        {
            history_.acknowledge_rollback(*world_);
            return false;
        }
        clear_interaction();
        for (const auto id : old)
        {
            factory_.forget(id);
            materials_.forget(id);
        }
        actor_ids_ = std::move(assembled.actors);
        component_ids_ = std::move(assembled.components);
        for (const auto& entry : actor_ids_)
        {
            Actor& actor = *world_->find_actor_by_id(entry.first);
            const auto saved = std::find_if(data.actors.begin(), data.actors.end(),
                                            [&](const SceneActorData& value)
                                            {
                                                return value.id == entry.second;
                                            });
            PlacementRequest request;
            if (saved->kind == "Custom")
            {
                request.actor_type = saved->type;
            }
            else if (!placement_kind(saved->kind, request.item))
            {
                error_ = "Unsupported placement archetype.";
                return false;
            }
            request.transform = actor.root_component()->local_transform();
            factory_.remember(actor, request);
            for (const auto& component : saved->components)
            {
                if (const auto* skeletal = std::get_if<SceneSkeletalMeshData>(&component.properties))
                {
                    const auto found = std::find_if(component_ids_.begin(), component_ids_.end(),
                                                    [&](const std::pair<const std::uint32_t, std::string>& value)
                                                    {
                                                        return value.second == component.id;
                                                    });
                    auto* runtime = static_cast<SceneComponent*>(actor.find_component_by_id(found->first));
                    factory_.remember_mesh(*runtime, *skeletal);
                }
                if (const auto* mesh = std::get_if<SceneMeshData>(&component.properties))
                {
                    const auto found = std::find_if(component_ids_.begin(), component_ids_.end(),
                                                    [&](const std::pair<const std::uint32_t, std::string>& value)
                                                    {
                                                        return value.second == component.id;
                                                    });
                    auto* runtime = static_cast<SceneComponent*>(actor.find_component_by_id(found->first));
                    factory_.remember_mesh(*runtime, *mesh);
                    if (runtime == actor.root_component())
                    {
                        const auto source = std::find_if(mesh->resources.begin(), mesh->resources.end(),
                                                         [](const SceneResourceBinding& value)
                                                         {
                                                             return value.role == "mesh";
                                                         });
                        if (source != mesh->resources.end())
                        {
                            request.asset_id = source->reference.asset_id;
                        }
                        request.static_mesh = static_cast<StaticMeshComponent*>(runtime)->static_mesh();
                    }
                }
            }
            factory_.remember(actor, request);
        }
        history_.mark_saved(*world_);
        error_.clear();
        return true;
    }

    bool EditorSceneSession::new_scene()
    {
        if (!replace({}))
        {
            return false;
        }
        asset_id_ = {};
        path_ = {};
        published_bytes_.clear();
        return true;
    }

    bool EditorSceneSession::open_path(const std::string& path)
    {
        const auto parsed = VirtualPath::parse(path);
        if (!parsed.succeeded() || parsed.value().utf8() != path ||
            asset_descriptor_kind(parsed.value()) != AssetDescriptorKind::Scene ||
            (path.compare(0u, 9u, "/Project/") != 0 && path.compare(0u, 8u, "/Engine/") != 0))
        {
            error_ = "Choose a canonical .scene virtual path under /Project or /Engine.";
            return false;
        }
        for (const auto& entry : workspace_.catalog().entries)
        {
            if (entry.path == parsed.value())
            {
                return open(entry.file.asset_id);
            }
        }
        error_ = "Scene is not in the asset catalog: " + path;
        return false;
    }

    bool EditorSceneSession::open(const AssetId& id)
    {
        const auto* location = workspace_.catalog().index.find(id);
        if (!location || asset_descriptor_kind(location->path) != AssetDescriptorKind::Scene)
        {
            error_ = "Scene asset is missing.";
            return false;
        }
        SceneAssetData loaded;
        std::vector<std::uint8_t> bytes;
        const auto read = read_scene_asset(workspace_.types(), workspace_.files(), location->path, loaded,
                                           &workspace_.catalog().index, &bytes);
        if (!read.succeeded())
        {
            error_ = read.message;
            return false;
        }
        const VirtualPath next_path = location->path;
        if (!replace(loaded))
        {
            return false;
        }
        asset_id_ = id;
        path_ = next_path;
        published_bytes_ = std::move(bytes);
        selection_.select_asset(id);
        return true;
    }

    bool EditorSceneSession::save(const VirtualPath& path, bool create_new)
    {
        if (!world_ || history_.active())
        {
            error_ = "Finish the active edit before saving.";
            return false;
        }
        if (asset_descriptor_kind(path) != AssetDescriptorKind::Scene || path.utf8().compare(0, 9, "/Project/") != 0)
        {
            error_ = "Choose a .scene path inside Project assets.";
            return false;
        }
        SceneAssetData data;
        if (!capture(data))
        {
            return false;
        }
        AssetId id = asset_id_;
        if (create_new || !id.valid())
        {
            if (!AssetId::try_generate(id) || workspace_.catalog().index.find(id))
            {
                error_ = "Could not allocate Scene asset identity.";
                return false;
            }
        }
        else
        {
            const auto disk = workspace_.files().read_binary(path, published_bytes_.size() + 1u);
            if (!disk.succeeded() || disk.value() != published_bytes_)
            {
                error_ = "Scene changed on disk. Reopen it or use Save Scene As.";
                return false;
            }
        }
        const auto bytes = encode_scene_asset_pair(workspace_.types(), id, data, &workspace_.catalog().index);
        if (!bytes.succeeded())
        {
            error_ = bytes.status().message;
            return false;
        }
        const auto saved = workspace_.asset_pairs().publish(
            path, bytes.value(), create_new ? FilePublishMode::CreateNew : FilePublishMode::Replace);
        if (!saved.succeeded())
        {
            error_ = saved.message;
            return false;
        }
        asset_id_ = id;
        path_ = path;
        published_bytes_ = bytes.value().asset;
        history_.mark_saved(*world_);
        if (!workspace_.refresh())
        {
            error_ = "Scene saved; asset catalog refresh failed: " + workspace_.error();
            return false;
        }
        selection_.select_asset(id);
        error_.clear();
        return true;
    }
} // namespace toy3d
