#pragma once

#include "mesh_description/mesh_description.h"

namespace toy3d
{
    struct StaticMeshImportOptions
    {
        float scale = 1.0f;
    };

    struct ImportedStaticMesh
    {
        MeshDescription mesh;
        std::vector<std::string> warnings;
    };

    // First version combines static instances; a collection allows later split mode.
    AssetResult<std::vector<ImportedStaticMesh>> import_static_meshes(const FileSystem& files,
        const VirtualPath& source, const StaticMeshImportOptions& options = {});

    struct StaticMeshImportAsset
    {
        std::vector<std::uint8_t> bytes;
        std::vector<std::string> warnings;
    };

    AssetResult<StaticMeshImportAsset> import_static_mesh_asset(const FileSystem& files,
        const VirtualPath& source, const AssetId& id, const StaticMeshImportOptions& options = {});
} // namespace toy3d
