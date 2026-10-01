#pragma once

#include "asset/mesh/mesh_description.h"
#include "asset/asset_pair.h"

namespace toy3d
{
    struct StaticMeshImportOptions
    {
        float import_uniform_scale = 1.0f;
        bool convert_scene_unit = true;
        bool use_file_unit = true;
        // Caller supplies the source-unit policy; one source unit is this many cm.
        float source_unit_in_centimeters = 1.0f;
    };

    struct ImportedStaticMesh
    {
        MeshDescription mesh;
        std::vector<std::string> warnings;
    };

    // First version combines static instances; a collection allows later split mode.
    AssetResult<std::vector<ImportedStaticMesh>> import_static_meshes(const FileSystem& files,
        const VirtualPath& source, const StaticMeshImportOptions& options);

    struct StaticMeshImportAsset
    {
        AssetPairBytes pair;
        std::vector<std::string> warnings;
    };

    AssetResult<StaticMeshImportAsset> import_static_mesh_asset(const FileSystem& files,
        const VirtualPath& source, const AssetId& id, const StaticMeshImportOptions& options);
} // namespace toy3d
