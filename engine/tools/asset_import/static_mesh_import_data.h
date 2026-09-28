#pragma once

#include "static_mesh_import.h"

#if WITH_EDITORONLY_DATA
namespace toy3d
{
    struct StaticMeshImportData
    {
        std::string source_file;
        StaticMeshImportOptions options;
    };

    AssetResult<AssetSegmentData> encode_static_mesh_import_data(const StaticMeshImportData& data);
} // namespace toy3d
#endif
