#include "scene/placement/actor_factory.h"

#include <utility>
#include <algorithm>

#include "file_system/physical_path.h"
#include "asset/scene/scene_asset.h"
#include "shader/shader_binding_identity.h"
#include "shader/shader_format_types.h"
#include "gamescene/actor/camera_actor.h"
#include "gamescene/actor/light_actor.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/material/material_asset_builder.h"

namespace toy3d
{
    bool ActorFactory::initialize()
    {
        if (!actor_types_.frozen())
        {
            TypeRegistry types;
            if (!register_scene_asset_types(types).succeeded() || !types.freeze().succeeded() || !actor_types_.freeze(types)) return false;
        }
        return component_editors_.freeze() && geometry_.initialize(PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
    }
    StaticMeshRef ActorFactory::instantiate_builtin(const std::string& kind) const
    { return geometry_.instantiate(kind); }

    void ActorFactory::remember(const Actor& actor, const PlacementRequest& request)
    {
        placed_items_[actor.actor_id()] = request;
    }

    EditorActorState ActorFactory::capture(const Actor& actor) const
    {
        EditorActorState state = capture_actor_state(actor, component_editors_, &actor_types_);
        for (auto& snapshot : state.components)
        {
            // C++17 get_if augments mesh snapshots with their author identity.
            if (auto* mesh = std::get_if<SceneMeshData>(&snapshot.data.properties))
            {
                const auto* component = dynamic_cast<const SceneComponent*>(actor.find_component_by_id(snapshot.component_id));
                SceneMeshData source;
                if (component && mesh_source(*component, source))
                {
                    source.settings = mesh->settings;
                    *mesh = std::move(source);
                }
            }
        }
        return state;
    }

    bool ActorFactory::mesh_source(const SceneComponent& component, SceneMeshData& data) const
    {
        const auto found = mesh_sources_.find(component.component_id());
        const auto* mesh = dynamic_cast<const StaticMeshComponent*>(&component);
        if (!mesh || found == mesh_sources_.end() || found->second.geometry != mesh->static_mesh()) return false;
        data = found->second.data;
        data.settings = mesh->primitive_settings();
        return true;
    }

    void ActorFactory::remember_mesh(const SceneComponent& component, const SceneMeshData& data)
    {
        const auto* mesh = dynamic_cast<const StaticMeshComponent*>(&component);
        if (!mesh) return;
        SceneMeshData source = data;
        // Material asset identities belong to MaterialAssignments, not geometry provenance.
        source.resources.erase(std::remove_if(source.resources.begin(), source.resources.end(),
            [](const SceneResourceBinding& binding) { return binding.role != "mesh"; }), source.resources.end());
        mesh_sources_[component.component_id()] = {component.owner().actor_id(), mesh->static_mesh(), std::move(source)};
    }

    void ActorFactory::release()
    {
        placed_items_.clear();
        mesh_sources_.clear();
        geometry_.release();
    }

    Actor* ActorFactory::create(World& world, const PlacementRequest& request)
    {
        if ((!find_placement_item(request.item) && request.item != PlacementItemId::StaticMesh) || (!is_finite(request.transform.translation) || !is_finite(request.transform.rotation) || !is_finite(request.transform.scale)) ||
            request.transform.scale.x <= 0 || request.transform.scale.y <= 0 || request.transform.scale.z <= 0)
            return nullptr;
        if ((request.item == PlacementItemId::Cube && !geometry_.default_material()) ||
            (request.item == PlacementItemId::Plane && !geometry_.default_material()))
            return nullptr;
        StaticMeshRef geometry;
        if (request.item == PlacementItemId::StaticMesh)
        {
            if (!request.asset_id.valid()) return nullptr;
            geometry = clone_scene_geometry(request.static_mesh);
            if (!geometry) return nullptr;
        }
        if (request.item == PlacementItemId::Cube || request.item == PlacementItemId::Plane)
        {
            geometry = geometry_.instantiate(request.item == PlacementItemId::Cube ? "Cube" : "Plane");
            if (!geometry) return nullptr;
        }
        if (!request.actor_type.empty())
        {
            const auto* type = actor_types_.find(request.actor_type);
            if (!type || !type->placeable || !actor_types_.frozen()) return nullptr;
            const auto custom_mesh = type->default_mesh.empty() ? StaticMeshRef{} : geometry_.instantiate(type->default_mesh);
            if (!type->default_mesh.empty() && !custom_mesh) return nullptr;
            Actor* created = actor_types_.create(world, request.actor_type);
            if (!created) return nullptr;
            Actor& custom = *created;
            SceneComponent* root = nullptr;
            if (!type->default_mesh.empty())
            {
                auto& mesh = custom.create_component<StaticMeshComponent>();
                mesh.set_static_mesh(custom_mesh);
                root = &mesh;
                SceneMeshData source; source.builtin_mesh = type->default_mesh;
                remember_mesh(mesh, source);
            }
            else root = &custom.create_component<SceneComponent>();
            if (!custom.set_root_component(root) || !root->set_local_transform(request.transform))
            { const auto id = custom.actor_id(); world.destroy_actor(custom); forget(id); return nullptr; }
            remember(custom, request);
            return &custom;
        }
        Actor* actor = nullptr;
        switch (request.item)
        {
        case PlacementItemId::EmptyActor:
            actor = &world.spawn_actor<Actor>();
            if (!actor->set_root_component(&actor->create_component<SceneComponent>()))
            {
                if (!world.destroy_actor(*actor)) TOY_LOG_ERROR("Placement rollback failed.");
                return nullptr;
            }
            break;
        case PlacementItemId::Cube:
        case PlacementItemId::Plane:
        case PlacementItemId::StaticMesh:
            actor = &world.spawn_actor<StaticMeshActor>();
            break;
        case PlacementItemId::DirectionalLight:
            actor = &world.spawn_actor<DirectionalLightActor>();
            break;
        case PlacementItemId::PointLight:
            actor = &world.spawn_actor<PointLightActor>();
            break;
        case PlacementItemId::Camera:
            actor = &world.spawn_actor<CameraActor>();
            break;
        }
        if (!actor || !actor->root_component()->set_local_transform(request.transform))
        {
            if (actor && !world.destroy_actor(*actor)) TOY_LOG_ERROR("Placement rollback failed.");
            return nullptr;
        }
        if (auto* mesh_actor = dynamic_cast<StaticMeshActor*>(actor))
            mesh_actor->static_mesh_component().set_static_mesh(std::move(geometry));
        placed_items_[actor->actor_id()] = request;
        if (auto* mesh = dynamic_cast<StaticMeshComponent*>(actor->root_component()))
        {
            SceneMeshData source;
            if (request.item == PlacementItemId::Cube) source.builtin_mesh = "Cube";
            else if (request.item == PlacementItemId::Plane) source.builtin_mesh = "Plane";
            else source.resources.push_back({"mesh", {request.asset_id, {}, "toy3d.StaticMeshAssetData", AssetRefStrength::Strong}});
            remember_mesh(*mesh, source);
        }
        return actor;
    }

    Actor* ActorFactory::restore(World& world, const PlacementRequest& request, const EditorActorState& state,
                                 std::map<std::uint32_t, std::uint32_t>& component_ids)
    {
        if (!state.valid || state.components.empty()) return nullptr;
        for (const auto& snapshot : state.components)
            if (!component_editors_.find(snapshot.data.type) || !validate_component_data(snapshot.data)) return nullptr;
        if (!state.actor_type.empty() && !actor_types_.validate(state.actor_type, state.properties)) return nullptr;
        Actor* actor = nullptr;
        if (!state.actor_type.empty()) actor = actor_types_.create(world, state.actor_type);
        else actor = request.item == PlacementItemId::EmptyActor ? &world.spawn_actor<Actor>() : create(world, request);
        if (!actor) return nullptr;
        EditorActorState restored = state;
        std::vector<std::uint32_t> unused = actor->component_ids();
        bool valid = true;
        for (auto& snapshot : restored.components)
        {
            SceneComponent* component = nullptr;
            for (auto entry = unused.begin(); entry != unused.end(); ++entry)
            {
                auto* candidate = dynamic_cast<SceneComponent*>(actor->find_component_by_id(*entry));
                const auto* editor = candidate ? component_editors_.find(*candidate) : nullptr;
                if (editor && editor->persistent_type == snapshot.data.type)
                {
                    component = candidate;
                    unused.erase(entry);
                    break;
                }
            }
            if (!component) component = &component_editors_.find(snapshot.data.type)->create(*actor);
            component_ids.emplace(snapshot.component_id, component->component_id());
            snapshot.component_id = component->component_id();
            if (auto* mesh = dynamic_cast<StaticMeshComponent*>(component))
            {
                if (!snapshot.mesh) { valid = false; break; }
                mesh->set_static_mesh(clone_scene_geometry(snapshot.mesh));
                if (!mesh->static_mesh()) { valid = false; break; }
                // C++17 get_if restores the geometry source alongside the new Component ID.
                if (const auto* data = std::get_if<SceneMeshData>(&snapshot.data.properties)) remember_mesh(*mesh, *data);
            }
        }
        if (!unused.empty()) valid = false;
        const auto root = component_ids.find(state.root_component_id);
        if (root == component_ids.end()) valid = false;
        if (valid)
        {
            restored.root_component_id = root->second;
            for (auto& snapshot : restored.components)
            {
                const auto parent = component_ids.find(snapshot.parent_component_id);
                if (parent != component_ids.end())
                {
                    snapshot.parent_actor_id = actor->actor_id();
                    snapshot.parent_component_id = parent->second;
                }
            }
            valid = actor->set_root_component(static_cast<SceneComponent*>(actor->find_component_by_id(root->second))) &&
                    apply_actor_state(*actor, restored, component_editors_, &actor_types_) && restore_actor_attachments(*actor, restored);
        }
        if (valid) { remember(*actor, request); return actor; }
        const auto id = actor->actor_id();
        if (!world.destroy_actor(*actor)) TOY_LOG_ERROR("Component reconstruction rollback failed.");
        forget(id);
        component_ids.clear();
        return nullptr;
    }

    bool ActorFactory::describe(const Actor& actor, PlacementRequest& request) const
    {
        const auto found = placed_items_.find(actor.actor_id());
        if (!actor.root_component()) return false;
        if (found != placed_items_.end()) request = found->second;
        else if (typeid(actor) == typeid(Actor)) request.item = PlacementItemId::EmptyActor;
        else if (typeid(actor) == typeid(DirectionalLightActor)) request.item = PlacementItemId::DirectionalLight;
        else if (typeid(actor) == typeid(PointLightActor)) request.item = PlacementItemId::PointLight;
        else if (typeid(actor) == typeid(CameraActor)) request.item = PlacementItemId::Camera;
        else if (const auto* type = actor_types_.find(actor)) { request.item = PlacementItemId::EmptyActor; request.actor_type = type->name; }
        else return false;
        request.transform = actor.root_component()->local_transform();
        return true;
    }

    const char* ActorFactory::label(std::uint32_t actor_id) const
    {
        const auto found = placed_items_.find(actor_id);
        if (found != placed_items_.end() && !found->second.actor_type.empty())
        { const auto* type = actor_types_.find(found->second.actor_type); return type ? type->display_name.c_str() : "Actor"; }
        if (found != placed_items_.end() && found->second.item == PlacementItemId::StaticMesh) return "Static Mesh";
        const PlacementItem* item = found == placed_items_.end() ? nullptr : find_placement_item(found->second.item);
        return item ? item->name : "Actor";
    }

    void ActorFactory::forget(std::uint32_t actor_id)
    {
        placed_items_.erase(actor_id);
        for (auto item = mesh_sources_.begin(); item != mesh_sources_.end();)
            if (item->second.actor_id == actor_id) item = mesh_sources_.erase(item);
            else ++item;
    }
} // namespace toy3d
