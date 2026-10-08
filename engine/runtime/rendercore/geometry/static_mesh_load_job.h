#pragma once

#include "asset_loader/asset_load_job.h"
#include "asset_loader/asset_loader.h"
#include "rendercore/geometry/mesh_material_loader.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"

#include <chrono>
#include <memory>
#include <string>

namespace toy3d
{
    // Scene and preview mesh decode: the loader thread reads the asset pair into CPU geometry
    // with no runtime knowledge, and the Game Thread adopts it into a StaticMesh using the
    // composition root's default material and material resolver.
    class StaticMeshLoadJob final : public AssetLoadJob
    {
      public:
        StaticMeshLoadJob(AssetRef reference, MaterialInterfaceRef default_material, MeshMaterialResolver resolver);

        AssetStatus decode(const FileSystem& files, const AssetIndex& index) override;
        AssetStatus adopt() override;
        std::size_t bytes() const override;

        const StaticMeshRef& mesh() const
        {
            return mesh_;
        }

      private:
        StaticMeshAssetGeometry geometry_;
        StaticMeshRef mesh_;
        MaterialInterfaceRef default_material_;
        MeshMaterialResolver resolver_;
    };

    // Typed entries: the three-argument overload applies the per-type default tier.
    AssetHandle<StaticMeshRef> request_static_mesh(AssetLoader& loader, const AssetRef& reference,
                                                   const AssetIndex& index, MaterialInterfaceRef default_material,
                                                   MeshMaterialResolver resolver, AssetLoadPriority priority);
    AssetHandle<StaticMeshRef> request_static_mesh(AssetLoader& loader, const AssetRef& reference,
                                                   const AssetIndex& index, MaterialInterfaceRef default_material,
                                                   MeshMaterialResolver resolver);

    // Assembly adapter: one Critical decode the caller cannot proceed without, with the same
    // bounded wait and diagnostics the texture adapter uses.
    StaticMeshRef load_assembly_static_mesh(AssetLoader& loader, const AssetRef& reference, const AssetIndex& index,
                                            MaterialInterfaceRef default_material, MeshMaterialResolver resolver,
                                            std::string& error,
                                            std::chrono::milliseconds timeout = std::chrono::seconds(5));
} // namespace toy3d
