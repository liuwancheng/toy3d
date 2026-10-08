#include "rendercore/geometry/static_mesh_load_job.h"

#include <cstddef>
#include <utility>

namespace toy3d
{
    namespace
    {
        AssetStatus mesh_failure(const AssetId& asset, const std::string& message)
        {
            return {AssetErrorCode::Value, asset, {}, {}, {}, message, {}};
        }
    } // namespace

    StaticMeshLoadJob::StaticMeshLoadJob(AssetRef reference, MaterialInterfaceRef default_material,
                                         MeshMaterialResolver resolver)
        : AssetLoadJob(std::move(reference)), default_material_(std::move(default_material)),
          resolver_(std::move(resolver))
    {
    }

    AssetStatus StaticMeshLoadJob::decode(const FileSystem& files, const AssetIndex& index)
    {
        const AssetLocation* location = index.find(reference().asset_id);
        if (!location)
        {
            set_error("Static mesh asset was not found.");
            return mesh_failure(reference().asset_id, "Static mesh asset was not found.");
        }
        if (location->index.root_type != "toy3d.StaticMeshAssetData")
        {
            set_error("Referenced asset is not a StaticMesh.");
            return mesh_failure(reference().asset_id, "Referenced asset is not a StaticMesh.");
        }
        const auto geometry = read_static_mesh_asset(files, location->path);
        if (!geometry.succeeded())
        {
            set_error(geometry.status().message);
            return geometry.status();
        }
        geometry_ = geometry.value();
        return AssetStatus::success();
    }

    AssetStatus StaticMeshLoadJob::adopt()
    {
        // Runtime object creation is Game Thread work; the loader only scheduled the decode.
        mesh_ = create_static_mesh_from_asset(geometry_, default_material_, resolver_);
        if (!mesh_)
        {
            set_error("Decoded StaticMesh could not be created.");
            return mesh_failure(reference().asset_id, "Decoded StaticMesh could not be created.");
        }
        return AssetStatus::success();
    }

    std::size_t StaticMeshLoadJob::bytes() const
    {
        std::size_t total = geometry_.vertices.size() * sizeof(StaticMeshAssetVertex) +
                            geometry_.indices.size() * sizeof(std::uint32_t) +
                            geometry_.sections.size() * sizeof(StaticMeshAssetSection);
        for (const auto& slot : geometry_.material_slots)
        {
            total += slot.size();
        }
        return total;
    }

    AssetHandle<StaticMeshRef> request_static_mesh(AssetLoader& loader, const AssetRef& reference,
                                                   const AssetIndex& index, MaterialInterfaceRef default_material,
                                                   MeshMaterialResolver resolver, AssetLoadPriority priority)
    {
        return loader.request<StaticMeshRef>(
            std::make_shared<StaticMeshLoadJob>(reference, std::move(default_material), std::move(resolver)), index,
            priority,
            [](const AssetLoadJob& job)
            {
                return static_cast<const StaticMeshLoadJob&>(job).mesh();
            });
    }

    AssetHandle<StaticMeshRef> request_static_mesh(AssetLoader& loader, const AssetRef& reference,
                                                   const AssetIndex& index, MaterialInterfaceRef default_material,
                                                   MeshMaterialResolver resolver)
    {
        return request_static_mesh(loader, reference, index, std::move(default_material), std::move(resolver),
                                   default_asset_load_priority(reference.expected_type));
    }

    StaticMeshRef load_assembly_static_mesh(AssetLoader& loader, const AssetRef& reference, const AssetIndex& index,
                                            MaterialInterfaceRef default_material, MeshMaterialResolver resolver,
                                            std::string& error, std::chrono::milliseconds timeout)
    {
        auto handle = request_static_mesh(loader, reference, index, std::move(default_material), std::move(resolver),
                                          AssetLoadPriority::Critical);
        if (!loader.wait(handle, timeout))
        {
            error = handle.failed()        ? handle.error()
                    : handle.invalidated() ? std::string("Asset changed while it was decoding; reload to pick it up.")
                                           : std::string("Asset decode did not finish in time.");
            return {};
        }
        return handle.get();
    }
} // namespace toy3d
