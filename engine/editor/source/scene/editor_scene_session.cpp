#include "scene/editor_scene_session.h"

#include <algorithm>
#include "asset_descriptor_path.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"
#include "selection/editor_selection.h"
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
            case PlacementItemId::EmptyActor: return "EmptyActor";
            case PlacementItemId::Cube: return "Cube";
            case PlacementItemId::Plane: return "Plane";
            case PlacementItemId::StaticMesh: return "StaticMesh";
            case PlacementItemId::DirectionalLight: return "DirectionalLight";
            case PlacementItemId::PointLight: return "PointLight";
            case PlacementItemId::Camera: return "Camera";
            }
            return "";
        }
        bool placement_kind(const std::string& name, PlacementItemId& kind)
        {
            for (const auto& item : placement_catalog())
                if (name == actor_kind(item.id)) { kind = item.id; return true; }
            if (name == "StaticMesh") { kind = PlacementItemId::StaticMesh; return true; }
            return false;
        }
        bool ensure_identity(std::map<std::uint32_t, std::string>& identities, std::uint32_t id)
        {
            if (identities.count(id)) return true;
            AssetId stable;
            if (!AssetId::try_generate(stable)) return false;
            identities.emplace(id, stable.hex());
            return true;
        }
    }

    EditorSceneSession::EditorSceneSession(EditorWorkspace& workspace, ActorFactory& factory,
        MaterialAssignments& materials, EditorSelection& selection, SceneViewport& viewport)
        : workspace_(workspace), factory_(factory), materials_(materials), selection_(selection), viewport_(viewport),
          history_(factory, materials)
    {
        history_.set_identity_remap([this](std::uint32_t old_id, std::uint32_t new_id,
            const std::map<std::uint32_t, std::uint32_t>& components) { remap(old_id, new_id, components); });
    }

    void EditorSceneSession::bind(World& world)
    {
        if (world_ && world_ != &world)
        {
            clear_interaction();
            actor_ids_.clear(); component_ids_.clear(); published_bytes_.clear();
            asset_id_ = {}; path_ = {}; error_.clear();
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
        if (!world_) return false;
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
        if (!world_) { error_ = "Scene session has no World."; return false; }
        SceneAssetData candidate;
        for (const auto id : world_->actor_ids())
        {
            const Actor* actor = world_->find_actor_by_id(id);
            if (!actor || !ensure_identity(actor_ids_, id)) { error_ = "Could not allocate Actor identity."; return false; }
            for (const auto component : actor->component_ids())
                if (!ensure_identity(component_ids_, component)) { error_ = "Could not allocate Component identity."; return false; }
        }
        for (const auto id : world_->actor_ids())
        {
            const Actor& actor = *world_->find_actor_by_id(id);
            PlacementRequest placement;
            if (!factory_.describe(actor, placement))
            { error_ = "Actor type has no registered scene construction path."; return false; }
            const auto state = factory_.capture(actor);
            if (!state.valid) { error_ = "Actor contains an unsupported Component."; return false; }
            SceneActorData saved;
            saved.id = actor_ids_.at(id);
            saved.kind = actor_kind(placement.item);
            saved.root_component_id = component_ids_.at(state.root_component_id);
            const auto assignments = materials_.capture(*world_, id);
            for (const auto& snapshot : state.components)
            {
                SceneComponentData component = snapshot.data;
                component.id = component_ids_.at(snapshot.component_id);
                if (snapshot.parent_component_id)
                {
                    const auto parent = component_ids_.find(snapshot.parent_component_id);
                    if (parent == component_ids_.end()) { error_ = "Attachment target is absent."; return false; }
                    component.parent_component_id = parent->second;
                }
                // C++17 get_if exposes resource identity only for StaticMesh components.
                if (auto* mesh = std::get_if<SceneMeshData>(&component.properties))
                {
                    const auto* runtime = static_cast<const StaticMeshComponent*>(actor.find_component_by_id(snapshot.component_id));
                    SceneMeshData source;
                    if (!factory_.mesh_source(*runtime, source))
                    { error_ = "StaticMesh geometry has no registered author source."; return false; }
                    *mesh = std::move(source);
                    if (!runtime->static_mesh()) { error_ = "StaticMesh geometry is unavailable."; return false; }
                    const auto& slots = runtime->static_mesh()->material_slot_names();
                    for (std::size_t slot = 0; slot < slots.size(); ++slot)
                    {
                        const auto found = std::find_if(assignments.begin(), assignments.end(),
                            [&](const MaterialSlotAssignment& value)
                            { return value.component_id == snapshot.component_id && value.slot_name == slots[slot]; });
                        if (runtime->has_material_override(static_cast<std::uint32_t>(slot)) != (found != assignments.end()))
                        { error_ = "Runtime Material override has no author Asset reference."; return false; }
                        if (found != assignments.end()) mesh->resources.push_back({"material:" + slots[slot], found->material});
                    }
                    for (const auto& assignment : assignments)
                        if (assignment.component_id == snapshot.component_id &&
                            std::find(slots.begin(), slots.end(), assignment.slot_name) == slots.end())
                        { error_ = "Material assignment targets an unknown slot."; return false; }
                }
                saved.components.push_back(std::move(component));
            }
            candidate.actors.push_back(std::move(saved));
        }
        const auto valid = validate_scene_asset(candidate, &workspace_.catalog().index);
        if (!valid.succeeded()) { error_ = valid.message; return false; }
        data = std::move(candidate);
        error_.clear();
        return true;
    }

    bool EditorSceneSession::replace(const SceneAssetData& data)
    {
        if (!world_) { error_ = "Scene session has no World."; return false; }
        if (history_.active()) { error_ = "Finish the active gesture before replacing the Scene."; return false; }
        history_.synchronize(*world_);
        const auto valid = validate_scene_asset(data, &workspace_.catalog().index);
        if (!valid.succeeded()) { error_ = valid.message; return false; }
        std::map<std::string, StaticMeshRef> meshes;
        // Resolve and build all geometry before publishing a candidate Actor.
        for (const auto& actor : data.actors)
            for (const auto& component : actor.components)
            {
                if (!factory_.component_editors().find(component.type))
                { error_ = "Component editor is not registered: " + component.type; return false; }
                // C++17 get_if selects geometry-owning author data.
                const auto* mesh = std::get_if<SceneMeshData>(&component.properties);
                if (!mesh) continue;
                StaticMeshRef geometry;
                if (!mesh->builtin_mesh.empty()) geometry = factory_.instantiate_builtin(mesh->builtin_mesh);
                else
                {
                    const auto source = std::find_if(mesh->resources.begin(), mesh->resources.end(),
                        [](const SceneResourceBinding& value) { return value.role == "mesh"; });
                    const auto* location = source == mesh->resources.end() ? nullptr :
                        workspace_.catalog().index.find(source->reference.asset_id);
                    if (!location) { error_ = "Scene mesh asset is missing."; return false; }
                    const auto loaded = read_static_mesh_asset(workspace_.files(), location->path);
                    if (!loaded.succeeded()) { error_ = loaded.status().message; return false; }
                    geometry = create_static_mesh_from_asset(loaded.value(), factory_.default_material());
                }
                if (!geometry) { error_ = "Could not prepare Scene geometry."; return false; }
                meshes.emplace(component.id, std::move(geometry));
            }
        const auto old_ids = world_->actor_ids();
        std::vector<std::uint32_t> candidates;
        std::map<std::string, SceneComponent*> components;
        std::map<std::uint32_t, std::string> next_actors;
        std::map<std::uint32_t, std::string> next_components;
        auto rollback = [&]()
        {
            for (const auto id : candidates)
            {
                Actor* actor = world_->find_actor_by_id(id);
                if (actor && !world_->destroy_actor(*actor)) TOY_LOG_ERROR("Scene candidate rollback failed.");
                factory_.forget(id);
                materials_.forget(id);
            }
            history_.acknowledge_rollback(*world_);
        };
        for (const auto& saved : data.actors)
        {
            PlacementRequest request;
            if (!placement_kind(saved.kind, request.item)) { error_ = "Unsupported Actor kind."; rollback(); return false; }
            const auto root = std::find_if(saved.components.begin(), saved.components.end(),
                [&](const SceneComponentData& value) { return value.id == saved.root_component_id; });
            request.transform = root->transform;
            if (request.item == PlacementItemId::StaticMesh)
            {
                // C++17 get_if requires a mesh root for the StaticMesh placement archetype.
                const auto* mesh = std::get_if<SceneMeshData>(&root->properties);
                if (!mesh) { error_ = "StaticMesh Actor requires a mesh root."; rollback(); return false; }
                for (const auto& binding : mesh->resources)
                    if (binding.role == "mesh") request.asset_id = binding.reference.asset_id;
                request.static_mesh = meshes.at(root->id);
            }
            EditorActorState state;
            state.valid = true;
            std::uint32_t local_id = 1;
            for (const auto& component : saved.components)
            {
                EditorComponentSnapshot snapshot;
                snapshot.component_id = local_id++;
                snapshot.data = component;
                snapshot.data.parent_component_id.clear();
                const auto mesh = meshes.find(component.id);
                if (mesh != meshes.end()) snapshot.mesh = mesh->second;
                if (component.id == saved.root_component_id) state.root_component_id = snapshot.component_id;
                state.components.push_back(std::move(snapshot));
            }
            std::map<std::uint32_t, std::uint32_t> remapped;
            Actor* actor = factory_.restore(*world_, request, state, remapped);
            if (!actor) { error_ = "Scene Actor construction failed."; rollback(); return false; }
            candidates.push_back(actor->actor_id());
            next_actors.emplace(actor->actor_id(), saved.id);
            for (std::size_t i = 0; i < saved.components.size(); ++i)
            {
                const auto& component = saved.components[i];
                auto* runtime = static_cast<SceneComponent*>(actor->find_component_by_id(remapped.at(state.components[i].component_id)));
                components.emplace(component.id, runtime);
                next_components.emplace(runtime->component_id(), component.id);
                // C++17 get_if binds restored mesh sources and Material identities.
                if (const auto* mesh = std::get_if<SceneMeshData>(&component.properties))
                {
                    SceneMeshData source = *mesh;
                    source.resources.erase(std::remove_if(source.resources.begin(), source.resources.end(),
                        [](const SceneResourceBinding& value) { return value.role != "mesh"; }), source.resources.end());
                    factory_.remember_mesh(*runtime, source);
                    for (const auto& binding : mesh->resources)
                        if (binding.role.compare(0, 9, "material:") == 0 &&
                            !materials_.assign(*world_, actor->actor_id(),
                                {runtime->component_id(), binding.role.substr(9), binding.reference}, error_))
                        { rollback(); return false; }
                }
            }
        }
        for (const auto& actor : data.actors)
            for (const auto& component : actor.components)
                if (!component.parent_component_id.empty() &&
                    !components.at(component.id)->attach_to(components.at(component.parent_component_id), AttachmentRule::KeepRelative))
                { error_ = "Scene attachment failed."; rollback(); return false; }
        clear_interaction();
        for (const auto id : old_ids)
        {
            Actor* actor = world_->find_actor_by_id(id);
            if (actor && !world_->destroy_actor(*actor)) TOY_LOG_ERROR("Old Scene Actor removal failed.");
            factory_.forget(id);
            materials_.forget(id);
        }
        actor_ids_ = std::move(next_actors);
        component_ids_ = std::move(next_components);
        history_.mark_saved(*world_);
        error_.clear();
        return true;
    }

    bool EditorSceneSession::new_scene()
    {
        if (!replace({})) return false;
        asset_id_ = {};
        path_ = {};
        published_bytes_.clear();
        return true;
    }

    bool EditorSceneSession::open(const AssetId& id)
    {
        const auto* location = workspace_.catalog().index.find(id);
        if (!location || asset_descriptor_kind(location->path) != AssetDescriptorKind::Scene)
        { error_ = "Scene asset is missing."; return false; }
        SceneAssetData loaded;
        std::vector<std::uint8_t> bytes;
        const auto read = read_scene_asset(workspace_.types(), workspace_.files(), location->path, loaded,
                                          &workspace_.catalog().index, &bytes);
        if (!read.succeeded()) { error_ = read.message; return false; }
        const VirtualPath next_path = location->path;
        if (!replace(loaded)) return false;
        asset_id_ = id;
        path_ = next_path;
        published_bytes_ = std::move(bytes);
        selection_.select_asset(id);
        return true;
    }

    bool EditorSceneSession::save(const VirtualPath& path, bool create_new)
    {
        if (!world_ || history_.active()) { error_ = "Finish the active edit before saving."; return false; }
        if (asset_descriptor_kind(path) != AssetDescriptorKind::Scene || path.utf8().compare(0, 9, "/Project/") != 0)
        { error_ = "Choose a .scene path inside Project assets."; return false; }
        SceneAssetData data;
        if (!capture(data)) return false;
        AssetId id = asset_id_;
        if (create_new || !id.valid())
        {
            if (!AssetId::try_generate(id) || workspace_.catalog().index.find(id))
            { error_ = "Could not allocate Scene asset identity."; return false; }
        }
        else
        {
            const auto disk = workspace_.files().read_binary(path, published_bytes_.size() + 1u);
            if (!disk.succeeded() || disk.value() != published_bytes_)
            { error_ = "Scene changed on disk. Reopen it or use Save Scene As."; return false; }
        }
        const auto bytes = encode_scene_asset_pair(workspace_.types(), id, data, &workspace_.catalog().index);
        if (!bytes.succeeded()) { error_ = bytes.status().message; return false; }
        const auto saved = workspace_.asset_pairs().publish(path, bytes.value(),
            create_new ? FilePublishMode::CreateNew : FilePublishMode::Replace);
        if (!saved.succeeded()) { error_ = saved.message; return false; }
        asset_id_ = id;
        path_ = path;
        published_bytes_ = bytes.value().asset;
        history_.mark_saved(*world_);
        if (!workspace_.refresh()) { error_ = "Scene saved; asset catalog refresh failed: " + workspace_.error(); return false; }
        selection_.select_asset(id);
        error_.clear();
        return true;
    }
}
