#include "scene/mesh_asset_bindings.h"

#include <algorithm>

#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "scene/editor_command_history.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"
#include "workspace/editor_workspace.h"
#include "threading/task_graph/graph_task.h"

namespace toy3d
{
    struct MeshAssetBindings::Result
    {
        EditorComponentSnapshot candidate;
        std::shared_ptr<const AnimationPreviewAsset> animation;
        std::vector<std::uint8_t> description;
        AssetId id;
        VirtualPath path;
        std::string error;
        bool placement = false;
        AssetPlacementRequest placement_request;
    };

    // --------------------------------------------------------------------------
    // MeshAssetBindings: asynchronous resource candidates and GT author transactions
    // --------------------------------------------------------------------------
    MeshAssetBindings::MeshAssetBindings(EditorWorkspace& workspace, EditorCommandHistory& history)
        : workspace_(workspace), history_(history)
    {
    }
    MeshAssetBindings::~MeshAssetBindings()
    {
        shutdown();
    }
    void MeshAssetBindings::initialize(TaskGraphInterface& tasks)
    {
        tasks_ = &tasks;
    }
    bool MeshAssetBindings::busy() const
    {
        return task_ != nullptr;
    }
    const std::string& MeshAssetBindings::error() const
    {
        return error_;
    }
    std::uint32_t MeshAssetBindings::take_placed_actor()
    {
        const auto id = placed_actor_;
        placed_actor_ = 0;
        return id;
    }
    bool MeshAssetBindings::place(World& world, const AssetPlacementRequest& request)
    {
        if (!tasks_ || busy() || history_.active())
        {
            error_ = "Finish the current edit or resource load first.";
            return false;
        }
        const auto* location = workspace_.catalog().index.find(request.asset_id);
        if (!location || (location->index.root_type != "toy3d.StaticMeshAssetData" &&
                          location->index.root_type != "toy3d.SkeletalMeshAssetData" &&
                          location->index.root_type != "toy3d.AnimationSequenceAssetData"))
        {
            error_ = "The dragged asset does not contain a mesh or animation.";
            return false;
        }
        const bool is_static = location->index.root_type == "toy3d.StaticMeshAssetData";
        auto result = std::make_shared<Result>();
        result->placement = true;
        result->placement_request = request;
        result->id = request.asset_id;
        result->path = location->path;
        result_ = result;
        world_ = &world;
        generation_ = world.scene_generation();
        actor_id_ = 0;
        error_.clear();
        auto* pairs = &workspace_.asset_pairs();
        const auto* files = &workspace_.files();
        const auto catalog = workspace_.catalog();
        const auto material = history_.actor_factory().default_material();
        task_ = dispatch_graph_task(
            *tasks_, "Load placed mesh assets",
            [result, pairs, files, catalog, material, is_static](NamedThread, const GraphEventRef&)
            {
                try
                {
                    if (is_static)
                    {
                        const auto pair = pairs->read(result->path);
                        if (!pair.succeeded())
                        {
                            result->error = pair.status().message;
                            return;
                        }
                        result->description = pair.value().description_bytes;
                        const auto geometry = read_static_mesh_asset(*files, result->path);
                        if (!geometry.succeeded())
                        {
                            result->error = geometry.status().message;
                            return;
                        }
                        result->candidate.mesh = create_static_mesh_from_asset(geometry.value(), material);
                        if (!result->candidate.mesh)
                        {
                            result->error = "Could not create placed Static Mesh geometry.";
                        }
                        return;
                    }
                    auto preview = load_animation_preview_asset(*pairs, catalog, result->id, false, {}, {}, {}, files);
                    if (!preview.succeeded())
                    {
                        result->error = preview.status().message;
                        return;
                    }
                    result->animation = std::make_shared<const AnimationPreviewAsset>(std::move(preview).value());
                    if (!result->animation->mesh)
                    {
                        result->error = "Animation placement requires a compatible preview mesh.";
                        return;
                    }
                    const auto mesh =
                        SkeletalMesh::create(result->animation->layout, *result->animation->mesh,
                                             std::vector<MaterialInterfaceRef>(
                                                 result->animation->mesh->data.material_slots.size(), material));
                    if (!mesh.succeeded())
                    {
                        result->error = mesh.status().message;
                        return;
                    }
                    result->candidate.skeletal_mesh = mesh.value();
                    result->candidate.sequence = result->animation->sequence;
                }
                catch (const std::exception& error)
                {
                    result->error = error.what();
                }
            });
        return true;
    }
    bool MeshAssetBindings::set_builtin(World& world, std::uint32_t actor_id, std::uint32_t component_id,
                                        const std::string& builtin)
    {
        if (busy() || history_.active())
        {
            return false;
        }
        const auto* actor = world.find_actor_by_id(actor_id);
        if (!actor)
        {
            return false;
        }
        const auto state = history_.actor_factory().capture(*actor);
        for (auto candidate : state.components)
        {
            if (candidate.component_id != component_id)
            {
                continue;
            }
            auto* data = std::get_if<SceneMeshData>(&candidate.data.properties);
            if (!data)
            {
                return false;
            }
            candidate.mesh = history_.actor_factory().instantiate_builtin(builtin);
            if (!candidate.mesh)
            {
                error_ = "Built-in mesh is unavailable.";
                return false;
            }
            data->builtin_mesh = builtin;
            data->resources.clear();
            return history_.replace_mesh(world, actor_id, std::move(candidate), error_);
        }
        return false;
    }
    bool MeshAssetBindings::request(World& world, std::uint32_t actor_id, std::uint32_t component_id,
                                    const std::string& role, const AssetId& id)
    {
        if (!tasks_ || busy() || history_.active() || (role != "mesh" && role != "animation"))
        {
            error_ = "Finish the current edit or resource load first.";
            return false;
        }
        const auto* actor = world.find_actor_by_id(actor_id);
        if (!actor)
        {
            error_ = "Mesh Actor no longer exists.";
            return false;
        }
        before_ = history_.actor_factory().capture(*actor);
        const auto found = std::find_if(before_.components.begin(), before_.components.end(),
                                        [&](const EditorComponentSnapshot& snapshot)
                                        {
                                            return snapshot.component_id == component_id;
                                        });
        if (!before_.valid || found == before_.components.end())
        {
            error_ = "Mesh component is unavailable.";
            return false;
        }
        auto result = std::make_shared<Result>();
        result->candidate = *found;
        auto* static_data = std::get_if<SceneMeshData>(&result->candidate.data.properties);
        auto* skeletal_data = std::get_if<SceneSkeletalMeshData>(&result->candidate.data.properties);
        if ((!static_data && !skeletal_data) || (static_data && role != "mesh"))
        {
            error_ = "The selected component does not support this resource.";
            return false;
        }
        auto& resources = static_data ? static_data->resources : skeletal_data->resources;
        resources.erase(std::remove_if(resources.begin(), resources.end(),
                                       [&](const SceneResourceBinding& binding)
                                       {
                                           return binding.role == role;
                                       }),
                        resources.end());
        if (static_data)
        {
            static_data->builtin_mesh.clear();
        }
        const auto* location = id.valid() ? workspace_.catalog().index.find(id) : nullptr;
        const std::string expected = role == "animation" ? "toy3d.AnimationSequenceAssetData"
                                     : static_data       ? "toy3d.StaticMeshAssetData"
                                                         : "toy3d.SkeletalMeshAssetData";
        if (id.valid() && (!location || location->index.root_type != expected))
        {
            error_ = "The resource is missing or has the wrong type.";
            return false;
        }
        if (id.valid())
        {
            resources.push_back({role, {id, {}, expected, AssetRefStrength::Strong}});
        }
        else if (role == "mesh")
        {
            resources.clear();
            result->candidate.mesh.reset();
            result->candidate.skeletal_mesh.reset();
            result->candidate.sequence.reset();
        }
        else
        {
            result->candidate.sequence.reset();
        }
        if (!id.valid())
        {
            return history_.replace_mesh(world, actor_id, std::move(result->candidate), error_);
        }
        if (role == "animation" && !result->candidate.skeletal_mesh)
        {
            error_ = "Bind a Skeletal Mesh before choosing an animation.";
            return false;
        }
        world_ = &world;
        generation_ = world.scene_generation();
        actor_id_ = actor_id;
        error_.clear();
        result_ = result;
        result->id = id;
        result->path = location->path;
        auto* pairs = &workspace_.asset_pairs();
        const auto* files = &workspace_.files();
        const auto catalog = workspace_.catalog();
        const auto material = history_.actor_factory().default_material();
        const bool is_static = static_data != nullptr;
        task_ = dispatch_graph_task(
            *tasks_, "Load component mesh assets",
            [result, pairs, files, catalog, material, is_static](NamedThread, const GraphEventRef&)
            {
                try
                {
                    if (is_static)
                    {
                        const auto pair = pairs->read(result->path);
                        if (!pair.succeeded() || !(pair.value().description.index.asset_id == result->id))
                        {
                            result->error =
                                pair.succeeded() ? "Mesh identity changed during load." : pair.status().message;
                            return;
                        }
                        result->description = pair.value().description_bytes;
                        const auto geometry = read_static_mesh_asset(*files, result->path);
                        if (!geometry.succeeded())
                        {
                            result->error = geometry.status().message;
                            return;
                        }
                        result->candidate.mesh = create_static_mesh_from_asset(geometry.value(), material);
                        if (!result->candidate.mesh)
                        {
                            result->error = "Static Mesh geometry could not be created.";
                        }
                        return;
                    }
                    const auto& data = std::get<SceneSkeletalMeshData>(result->candidate.data.properties);
                    AssetId mesh_id;
                    AssetId animation_id;
                    for (const auto& resource : data.resources)
                    {
                        if (resource.role == "mesh")
                        {
                            mesh_id = resource.reference.asset_id;
                        }
                        else if (resource.role == "animation")
                        {
                            animation_id = resource.reference.asset_id;
                        }
                    }
                    auto preview = load_animation_preview_asset(*pairs, catalog, mesh_id, true, mesh_id, animation_id);
                    if (!preview.succeeded())
                    {
                        result->error = preview.status().message;
                        return;
                    }
                    result->animation = std::make_shared<const AnimationPreviewAsset>(std::move(preview).value());
                    if (result->candidate.skeletal_mesh &&
                        result->candidate.skeletal_mesh->bone_layout()->skeleton_id() ==
                            result->animation->layout->skeleton_id() &&
                        result->id == animation_id)
                    {
                        // Animation-only edits retain immutable mesh geometry and its existing proxy.
                        result->candidate.sequence = result->animation->sequence;
                        return;
                    }
                    const auto mesh =
                        SkeletalMesh::create(result->animation->layout, *result->animation->mesh,
                                             std::vector<MaterialInterfaceRef>(
                                                 result->animation->mesh->data.material_slots.size(), material));
                    if (!mesh.succeeded())
                    {
                        result->error = mesh.status().message;
                        return;
                    }
                    result->candidate.skeletal_mesh = mesh.value();
                    result->candidate.sequence = result->animation->sequence;
                }
                catch (const std::exception& error)
                {
                    result->error = error.what();
                }
            });
        return true;
    }
    void MeshAssetBindings::tick(World& world)
    {
        if (!task_ || !task_->is_complete())
        {
            return;
        }
        auto result = std::move(result_);
        const bool task_ok = task_->get_outcome() == TaskOutcome::Succeeded;
        task_.reset();
        auto before = std::move(before_);
        if (!task_ok || !result->error.empty())
        {
            error_ = result->error.empty() ? "Mesh resource worker failed." : result->error;
            TOY_LOG_ERROR("Mesh resource load failed: {}", error_);
            return;
        }
        const auto* actor = world.find_actor_by_id(actor_id_);
        if (world_ != &world || generation_ != world.scene_generation() || history_.active() ||
            (!result->placement && (!actor || !same_actor_state(before, history_.actor_factory().capture(*actor)))))
        {
            error_ = "Resource load cancelled because its Scene or component changed.";
            TOY_LOG_ERROR("Mesh resource load failed: {}", error_);
            return;
        }
        bool current = false;
        if (result->animation)
        {
            current = animation_preview_asset_current(workspace_.asset_pairs(), workspace_.catalog(),
                                                      *result->animation, &workspace_.files());
        }
        else
        {
            const auto* location = workspace_.catalog().index.find(result->id);
            if (location && location->path.utf8() == result->path.utf8())
            {
                const auto pair = workspace_.asset_pairs().read(location->path);
                current = pair.succeeded() && pair.value().description_bytes == result->description;
            }
        }
        if (!current)
        {
            error_ = "Resource load cancelled because its source asset changed.";
            TOY_LOG_ERROR("Mesh resource load failed: {}", error_);
            return;
        }
        if (result->placement)
        {
            PlacementRequest request;
            request.transform = result->placement_request.transform;
            request.item = result->candidate.mesh ? PlacementItemId::StaticMesh : PlacementItemId::SkeletalMesh;
            request.asset_id = result->animation ? result->animation->mesh_id : result->id;
            request.animation_id = result->animation ? result->animation->sequence_id : AssetId{};
            request.static_mesh = result->candidate.mesh;
            request.skeletal_assets = {result->candidate.skeletal_mesh, result->candidate.sequence};
            if (result->placement_request.on_ground)
            {
                float minimum = request.static_mesh
                                    ? request.static_mesh->local_bounds().minimum.y
                                    : request.skeletal_assets.mesh->asset().geometry.mesh.vertices.front().position.y;
                if (request.skeletal_assets.mesh)
                {
                    for (const auto& vertex : request.skeletal_assets.mesh->asset().geometry.mesh.vertices)
                    {
                        minimum = std::min(minimum, vertex.position.y);
                    }
                }
                request.transform.translation.y -= minimum;
            }
            placed_actor_ = history_.place_actor(world, request);
            if (!placed_actor_)
            {
                error_ = "Could not place the mesh Actor.";
            }
        }
        else
        {
            history_.replace_mesh(world, actor_id_, std::move(result->candidate), error_);
        }
        if (!error_.empty())
        {
            TOY_LOG_ERROR("Mesh resource commit failed: {}", error_);
        }
    }
    void MeshAssetBindings::shutdown()
    {
        if (task_ && !task_->is_complete())
        {
            const auto status = tasks_->wait_until_task_completes(task_, NamedThread::GameThread);
            if (!status.succeeded())
            {
                TOY_LOG_ERROR("Mesh resource worker shutdown failed.");
            }
        }
        task_.reset();
        result_.reset();
        before_ = {};
        world_ = nullptr;
        placed_actor_ = 0;
        tasks_ = nullptr;
    }
} // namespace toy3d
