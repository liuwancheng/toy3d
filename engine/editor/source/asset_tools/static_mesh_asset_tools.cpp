#include "asset_tools/static_mesh_asset_tools.h"

#include "asset_import/static_mesh_import.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "logging/logger.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    bool import_static_mesh_to_workspace(EditorWorkspace& workspace, const PhysicalPath& source,
        const std::string& destination, const StaticMeshImportOptions& options, AssetId& published_id, std::string& error)
    {
        error.clear();
        const auto output = VirtualPath::parse(destination);
        if (!workspace.ready() || !output.succeeded() || destination.compare(0, 9, "/Project/") != 0 ||
            destination.size() < 6 || destination.substr(destination.size() - 6) != ".asset")
        {
            error = "Choose a .asset destination inside the project asset root.";
            return false;
        }
        const auto existing = workspace.files().stat(output.value());
        if (existing.succeeded()) { error = "An asset already exists at " + destination + ". Choose another resource name."; return false; }
        if (existing.status().code != FileErrorCode::NotFound) { error = existing.status().message; return false; }
        NativePlatformFile platform;
        const auto canonical = platform.canonical(source);
        if (!canonical.succeeded()) { error = canonical.status().message; return false; }
        const auto parent = platform.parent_path(canonical.value());
        if (!parent.succeeded()) { error = parent.status().message; return false; }
        const std::string& host = canonical.value().utf8();
        const std::string filename = host.substr(host.find_last_of("/\\") + 1);
        const auto input = VirtualPath::parse("/Source/" + filename);
        if (!input.succeeded()) { error = input.status().message; return false; }
        DirectoryFileStoreDesc store_desc;
        store_desc.physical_root = parent.value();
        store_desc.writable = false;
        const auto store = DirectoryFileStore::create(platform, store_desc);
        if (!store.succeeded()) { error = store.status().message; return false; }
        FileSystem files;
        const auto root = VirtualPath::parse("/Source");
        if (!root.succeeded()) { error = root.status().message; return false; }
        FileMountDesc mount;
        mount.virtual_root = root.value();
        mount.store = store.value();
        mount.access = MountAccess::ReadOnly;
        const FileStatus mounted = files.add_mount(mount);
        if (!mounted.succeeded()) { error = mounted.message; return false; }
        const FileStatus frozen = files.freeze();
        if (!frozen.succeeded()) { error = frozen.message; return false; }
        AssetId id;
        if (!AssetId::try_generate(id)) { error = "Could not generate an Asset ID."; return false; }
        for (const AssetCatalogEntry& entry : workspace.catalog().entries)
            if (entry.file.asset_id == id) { error = "Generated Asset ID already exists; retry import."; return false; }
        const auto imported = import_static_mesh_asset(files, input.value(), id, options);
        if (!imported.succeeded()) { error = imported.status().message; return false; }
        const FileStatus published = workspace.files().write_binary_atomic(output.value(), imported.value().bytes,
                                                                          FilePublishMode::CreateNew);
        if (!published.succeeded()) { error = published.message; return false; }
        published_id = id;
        for (const std::string& warning : imported.value().warnings) TOY_LOG_WARN("Model import: {}", warning);
        if (!workspace.refresh())
        {
            error = "Asset was saved at " + destination + ", but catalog refresh failed: " + workspace.error();
            return false;
        }
        TOY_LOG_INFO("Imported static mesh: {} ({})", destination, id.hex());
        return true;
    }
} // namespace toy3d
