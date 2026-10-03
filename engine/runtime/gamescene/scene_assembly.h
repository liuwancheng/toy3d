#pragma once

#include "asset/scene/scene_asset.h"
#include "gamescene/actor/actor_type_registry.h"
#include "gamescene/world/world.h"
#include "rendercore/geometry/static_mesh.h"

#include <functional>
#include <map>

namespace toy3d
{
    class StaticMeshComponent;
    struct SceneAssemblyServices
    {
        std::function<TextureRef(const AssetRef&, std::string&)> load_environment;
        std::function<StaticMeshRef(const SceneMeshData&, std::string&)> load_mesh;
        std::function<bool(Actor&, StaticMeshComponent&, const std::string&, const AssetRef&, std::string&)>
            assign_material;
        std::function<void(std::uint32_t)> forget_actor;
    };
    struct SceneAssemblyResult
    {
        std::map<std::uint32_t, std::string> actors;
        std::map<std::uint32_t, std::string> components;
    };
    // GT only. Builds a complete candidate in a non-playing World, then replaces old Actors.
    // Failure destroys only candidate Actors and retains the previous scene.
    bool assemble_scene(World& world, const SceneAssetData& data, const ActorTypeRegistry& types,
                        const TypeRegistry& schemas, const SceneAssemblyServices& services, SceneAssemblyResult& result,
                        std::string& error, const AssetIndex* assets = nullptr);
} // namespace toy3d
