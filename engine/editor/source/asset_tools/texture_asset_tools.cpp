#include "asset_tools/texture_asset_tools.h"

#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "texture_import/texture_import.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    bool prepare_texture_asset_from_source(const PhysicalPath& source, const AssetId& id,
        std::vector<std::uint8_t>& bytes, std::string& error)
    {
        error.clear();
        NativePlatformFile platform;
        const auto canonical = platform.canonical(source);
        if (!canonical.succeeded()) { error = canonical.status().message; return false; }
        const auto parent = platform.parent_path(canonical.value());
        if (!parent.succeeded()) { error = parent.status().message; return false; }
        const std::string& host = canonical.value().utf8();
        const auto input = VirtualPath::parse("/Source/" + host.substr(host.find_last_of("/\\") + 1u));
        if (!input.succeeded()) { error = input.status().message; return false; }
        DirectoryFileStoreDesc store_desc;
        store_desc.physical_root = parent.value();
        store_desc.writable = false;
        const auto store = DirectoryFileStore::create(platform, store_desc);
        if (!store.succeeded()) { error = store.status().message; return false; }
        FileSystem files;
        FileMountDesc mount;
        const auto root = VirtualPath::parse("/Source");
        if (!root.succeeded()) { error = root.status().message; return false; }
        mount.virtual_root = root.value();
        mount.store = store.value();
        mount.access = MountAccess::ReadOnly;
        const FileStatus mounted = files.add_mount(mount);
        if (!mounted.succeeded()) { error = mounted.message; return false; }
        const FileStatus frozen = files.freeze();
        if (!frozen.succeeded()) { error = frozen.message; return false; }
        const auto source_bytes = files.read_binary(input.value(), 32u * 1024u * 1024u);
        if (!source_bytes.succeeded()) { error = source_bytes.status().message; return false; }
        const auto imported = import_texture_asset(source_bytes.value(), id);
        if (!imported.succeeded()) { error = imported.status().message; return false; }
        bytes = imported.value();
        return true;
    }

    bool publish_texture_asset(EditorWorkspace& workspace, const std::string& destination,
        const AssetId& id, const std::vector<std::uint8_t>& bytes, AssetId& published_id, std::string& error)
    {
        error.clear();
        const auto output = VirtualPath::parse(destination);
        if (!workspace.ready() || !id.valid() || !output.succeeded() || destination.compare(0, 9, "/Project/") != 0 ||
            destination.size() < 6u || destination.substr(destination.size() - 6u) != ".asset")
        { error = "Choose a .asset destination inside the project asset root."; return false; }
        const auto existing = workspace.files().stat(output.value());
        if (existing.succeeded()) { error = "Texture asset already exists; choose another resource name."; return false; }
        if (existing.status().code != FileErrorCode::NotFound) { error = existing.status().message; return false; }
        if (workspace.catalog().index.find(id)) { error = "Texture2D ID already exists; retry import."; return false; }
        const auto inspected = inspect_asset_bytes(bytes);
        if (!inspected.succeeded() || !(inspected.value().asset_id == id) ||
            inspected.value().root_type != "toy3d.Texture2DAssetData")
        { error = "Prepared Texture2D package identity is invalid."; return false; }
        const FileStatus published = workspace.files().write_binary_atomic(output.value(), bytes,
            FilePublishMode::CreateNew);
        if (!published.succeeded()) { error = published.message; return false; }
        published_id = id;
        if (!workspace.refresh())
        {
            error = "Texture was saved at " + destination + ", but catalog refresh failed: " + workspace.error();
            return false;
        }
        return true;
    }

} // namespace toy3d
