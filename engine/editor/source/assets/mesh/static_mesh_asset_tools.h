#pragma once

#include "asset/asset_identity.h"
#include "file_system/physical_path.h"

#include <string>

namespace toy3d
{
    class EditorWorkspace;
    struct StaticMeshImportOptions;

    // The Editor owns destination policy; shared importer only returns candidate bytes.
    bool import_static_mesh_to_workspace(EditorWorkspace& workspace, const PhysicalPath& source,
                                         const std::string& destination, const StaticMeshImportOptions& options,
                                         AssetId& published_id, std::string& error);
} // namespace toy3d
