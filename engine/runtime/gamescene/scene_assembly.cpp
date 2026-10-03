#include "gamescene/scene_assembly.h"

#include <algorithm>

#include "gamescene/component/static_mesh_component.h"
#include "gamescene/component/skeletal_mesh_component.h"
#include "gamescene/scene_component_data.h"
#include "logging/logger.h"

namespace toy3d
{
    bool assemble_scene(World& world, const SceneAssetData& data, const ActorTypeRegistry& types,
                        const TypeRegistry& schemas, const SceneAssemblyServices& services, SceneAssemblyResult& result,
                        std::string& error, const AssetIndex* assets)
    {
        if (world.lifecycle_state() == WorldLifecycleState::Playing || !types.frozen() || !schemas.frozen())
        {
            error = "Scene assembly requires a stopped World and frozen Actor types.";
            return false;
        }
        const auto valid = validate_scene_asset(data, assets, &schemas);
        if (!valid.succeeded())
        {
            error = valid.message;
            return false;
        }
        TextureRef environment;
        if (data.environment.environment.asset_id.valid())
        {
            environment =
                services.load_environment ? services.load_environment(data.environment.environment, error) : nullptr;
            if (!environment)
            {
                if (error.empty())
                {
                    error = "Scene Environment could not load.";
                }
                return false;
            }
        }
        std::map<std::string, StaticMeshRef> meshes;
        std::map<std::string, SkeletalMeshAssets> skeletal_meshes;
        for (const auto& actor : data.actors)
        {
            if (!types.validate(actor.type, actor.properties))
            {
                error = "Actor type/properties unavailable: " + actor.type;
                return false;
            }
            for (const auto& component : actor.components)
            {
                // C++17 get_if resolves mesh resources before constructing candidate Actors.
                if (const auto* mesh = std::get_if<SceneMeshData>(&component.properties))
                {
                    if (mesh->builtin_mesh.empty() && mesh->resources.empty())
                    {
                        meshes.emplace(component.id, nullptr);
                        continue;
                    }
                    auto geometry = services.load_mesh ? services.load_mesh(*mesh, error) : nullptr;
                    if (!geometry)
                    {
                        if (error.empty())
                        {
                            error = "Scene mesh could not load: " + component.id;
                        }
                        return false;
                    }
                    meshes.emplace(component.id, std::move(geometry));
                }
                else if (const auto* mesh = std::get_if<SceneSkeletalMeshData>(&component.properties))
                {
                    if (mesh->resources.empty())
                    {
                        skeletal_meshes.emplace(component.id, SkeletalMeshAssets{});
                        continue;
                    }
                    if (!services.load_skeletal_mesh)
                    {
                        error = "Scene skeletal mesh loader is unavailable.";
                        return false;
                    }
                    auto loaded = services.load_skeletal_mesh(*mesh);
                    if (!loaded.succeeded() || !loaded.value().mesh)
                    {
                        error = loaded.succeeded() ? "Scene skeletal mesh could not load." : loaded.status().message;
                        return false;
                    }
                    skeletal_meshes.emplace(component.id, std::move(loaded).value());
                }
            }
        }
        const auto old = world.actor_ids();
        std::vector<std::uint32_t> candidates;
        std::map<std::string, SceneComponent*> components;
        SceneAssemblyResult candidate;
        auto rollback = [&]()
        {
            for (const auto id : candidates)
            {
                if (auto* actor = world.find_actor_by_id(id))
                {
                    if (!world.destroy_actor(*actor))
                    {
                        TOY_LOG_ERROR("Scene candidate rollback failed for Actor {}.", id);
                    }
                }
                if (services.forget_actor)
                {
                    services.forget_actor(id);
                }
            }
        };
        for (const auto& saved : data.actors)
        {
            Actor* created = types.create(world, saved.type);
            if (!created)
            {
                error = "Actor factory did not create exactly one matching Actor: " + saved.type;
                rollback();
                return false;
            }
            Actor& actor = *created;
            candidates.push_back(actor.actor_id());
            if (!world.contains(actor) || !types.find(actor) || types.find(actor)->name != saved.type)
            {
                error = "Actor factory returned the wrong runtime type: " + saved.type;
                rollback();
                return false;
            }
            if (!types.apply(actor, saved.properties))
            {
                error = "Actor properties rejected: " + saved.type;
                rollback();
                return false;
            }
            candidate.actors.emplace(actor.actor_id(), saved.id);
            auto unused = actor.component_ids();
            for (const auto& snapshot : saved.components)
            {
                SceneComponent* component = nullptr;
                for (auto entry = unused.begin(); entry != unused.end(); ++entry)
                {
                    auto* existing = dynamic_cast<SceneComponent*>(actor.find_component_by_id(*entry));
                    SceneComponentData described;
                    if (existing && capture_scene_component(*existing, described) && described.type == snapshot.type)
                    {
                        component = existing;
                        unused.erase(entry);
                        break;
                    }
                }
                if (!component)
                {
                    component = create_scene_component(actor, snapshot.type);
                }
                if (!component || !apply_scene_component(*component, snapshot))
                {
                    error = "Component construction/application failed: " + snapshot.type;
                    rollback();
                    return false;
                }
                if (snapshot.id == saved.root_component_id && !actor.set_root_component(component))
                {
                    error = "Actor root assignment failed.";
                    rollback();
                    return false;
                }
                components.emplace(snapshot.id, component);
                candidate.components.emplace(component->component_id(), snapshot.id);
                if (const auto* mesh = std::get_if<SceneMeshData>(&snapshot.properties))
                {
                    auto& target = static_cast<StaticMeshComponent&>(*component);
                    target.set_static_mesh(meshes.at(snapshot.id));
                }
                else if (std::get_if<SceneSkeletalMeshData>(&snapshot.properties))
                {
                    const auto& assets = skeletal_meshes.at(snapshot.id);
                    const auto status =
                        static_cast<SkeletalMeshComponent&>(*component).set_assets(assets.mesh, assets.sequence);
                    if (!status.succeeded())
                    {
                        error = status.message;
                        rollback();
                        return false;
                    }
                }
                const auto* mesh_data = std::get_if<SceneMeshData>(&snapshot.properties);
                const auto* skeletal_data = std::get_if<SceneSkeletalMeshData>(&snapshot.properties);
                if (mesh_data || skeletal_data)
                {
                    auto& target = static_cast<MeshComponent&>(*component);
                    for (const auto& resource : mesh_data ? mesh_data->resources : skeletal_data->resources)
                    {
                        if (resource.role.compare(0, 9, "material:") == 0 &&
                            (!services.assign_material ||
                             !services.assign_material(actor, target, resource.role.substr(9), resource.reference,
                                                       error)))
                        {
                            if (error.empty())
                            {
                                error = "Scene Material assignment failed.";
                            }
                            rollback();
                            return false;
                        }
                    }
                }
            }
            if (!unused.empty() || !actor.root_component())
            {
                error = "Actor constructor components differ from saved Scene.";
                rollback();
                return false;
            }
        }
        for (const auto& actor : data.actors)
        {
            for (const auto& component : actor.components)
            {
                if (!component.parent_component_id.empty() &&
                    !components.at(component.id)
                         ->attach_to(components.at(component.parent_component_id), AttachmentRule::KeepRelative))
                {
                    error = "Scene attachment failed.";
                    rollback();
                    return false;
                }
            }
        }
        if (!world.set_environment(data.environment, std::move(environment)))
        {
            error = "Scene Environment snapshot was rejected.";
            rollback();
            return false;
        }
        for (const auto id : old)
        {
            if (auto* actor = world.find_actor_by_id(id))
            {
                if (!world.destroy_actor(*actor))
                {
                    TOY_LOG_ERROR("Old Scene removal failed for Actor {}.", id);
                }
            }
            if (services.forget_actor)
            {
                services.forget_actor(id);
            }
        }
        result = std::move(candidate);
        error.clear();
        return true;
    }
} // namespace toy3d
