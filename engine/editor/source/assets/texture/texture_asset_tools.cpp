#include "assets/texture/texture_asset_tools.h"

#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "asset_pipeline/texture_import.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        bool read_image_source(const PhysicalPath& source, std::vector<std::uint8_t>& bytes, std::string& error)
        {
            error.clear();
            NativePlatformFile platform;
            const auto canonical = platform.canonical(source);
            if (!canonical.succeeded())
            {
                error = canonical.status().message;
                return false;
            }
            const auto parent = platform.parent_path(canonical.value());
            if (!parent.succeeded())
            {
                error = parent.status().message;
                return false;
            }
            const std::string& host = canonical.value().utf8();
            const auto input = VirtualPath::parse("/Source/" + host.substr(host.find_last_of("/\\") + 1u));
            if (!input.succeeded())
            {
                error = input.status().message;
                return false;
            }
            DirectoryFileStoreDesc store_desc;
            store_desc.physical_root = parent.value();
            store_desc.writable = false;
            const auto store = DirectoryFileStore::create(platform, store_desc);
            if (!store.succeeded())
            {
                error = store.status().message;
                return false;
            }
            FileSystem files;
            FileMountDesc mount;
            const auto root = VirtualPath::parse("/Source");
            if (!root.succeeded())
            {
                error = root.status().message;
                return false;
            }
            mount.virtual_root = root.value();
            mount.store = store.value();
            mount.access = MountAccess::ReadOnly;
            const FileStatus mounted = files.add_mount(mount);
            if (!mounted.succeeded())
            {
                error = mounted.message;
                return false;
            }
            const FileStatus frozen = files.freeze();
            if (!frozen.succeeded())
            {
                error = frozen.message;
                return false;
            }
            const auto loaded = files.read_binary(input.value(), 32u * 1024u * 1024u);
            if (!loaded.succeeded())
            {
                error = loaded.status().message;
                return false;
            }
            bytes = loaded.value();
            return true;
        }
    } // namespace

    bool prepare_texture_asset_from_source(const PhysicalPath& source, Texture2DAsset& texture, std::string& error,
                                           const TextureImportSettings& settings)
    {
        std::vector<std::uint8_t> source_bytes;
        if (!read_image_source(source, source_bytes, error))
        {
            return false;
        }
        const auto imported = import_texture_image(source_bytes, settings);
        if (!imported.succeeded())
        {
            error = imported.status().message;
            return false;
        }
        texture = imported.value();
        return true;
    }

    bool prepare_environment_asset_from_source(const PhysicalPath& source, EnvironmentAsset& environment,
                                               std::string& error, EnvironmentImportSettings settings)
    {
        std::vector<std::uint8_t> bytes;
        if (!read_image_source(source, bytes, error))
        {
            return false;
        }
        const auto imported = import_environment_hdr(bytes, settings);
        if (!imported.succeeded())
        {
            error = imported.status().message;
            return false;
        }
        environment = imported.value();
        return true;
    }

    bool publish_reimported_texture_asset(EditorWorkspace& workspace, const std::string& destination, const AssetId& id,
                                          const Texture2DAsset& texture, const std::vector<std::uint8_t>& baseline,
                                          std::string& error)
    {
        const auto path = VirtualPath::parse(destination);
        const auto location = workspace.catalog().index.find(id);
        if (!workspace.ready() || !path.succeeded() || !location || location->path.utf8() != destination ||
            destination.compare(0u, 9u, "/Project/") != 0 || baseline.empty())
        {
            error = "Reimport requires the original writable Project texture and a conflict baseline.";
            return false;
        }
        const auto current = workspace.asset_pairs().read(path.value());
        if (!current.succeeded() || current.value().description_bytes != baseline)
        {
            error = "Texture changed during reimport; reload before retrying.";
            return false;
        }
        const auto pair = encode_texture_asset_pair(workspace.types(), id, texture);
        if (!pair.succeeded())
        {
            error = pair.status().message;
            return false;
        }
        const auto published = workspace.asset_pairs().publish(path.value(), pair.value(), FilePublishMode::Replace);
        if (!published.succeeded())
        {
            error = published.message;
            return false;
        }
        if (!workspace.refresh())
        {
            error = "Texture was reimported, but catalog refresh failed: " + workspace.error();
            return true;
        }
        error.clear();
        return true;
    }

    bool publish_texture_asset(EditorWorkspace& workspace, const std::string& destination, const AssetId& id,
                               const Texture2DAsset& texture, AssetId& published_id, std::string& error)
    {
        error.clear();
        const auto output = VirtualPath::parse(destination);
        if (!workspace.ready() || !id.valid() || !output.succeeded() || destination.compare(0, 9, "/Project/") != 0 ||
            destination.size() < 6u || destination.substr(destination.size() - 6u) != ".asset")
        {
            error = "Choose a .asset destination inside the project asset root.";
            return false;
        }
        const auto existing = workspace.files().stat(output.value());
        if (existing.succeeded())
        {
            error = "Texture asset already exists; choose another resource name.";
            return false;
        }
        if (existing.status().code != FileErrorCode::NotFound)
        {
            error = existing.status().message;
            return false;
        }
        if (workspace.catalog().index.find(id))
        {
            error = "Texture2D ID already exists; retry import.";
            return false;
        }
        const auto pair = encode_texture_asset_pair(workspace.types(), id, texture);
        if (!pair.succeeded())
        {
            error = pair.status().message;
            return false;
        }
        const AssetStatus published =
            workspace.asset_pairs().publish(output.value(), pair.value(), FilePublishMode::CreateNew);
        if (!published.succeeded())
        {
            error = published.message;
            return false;
        }
        published_id = id;
        if (!workspace.refresh())
        {
            error = "Texture was saved at " + destination + ", but catalog refresh failed: " + workspace.error();
            return false;
        }
        return true;
    }
    bool publish_environment_asset(EditorWorkspace& workspace, const std::string& destination, const AssetId& id,
                                   const EnvironmentAsset& environment, AssetId& published_id, std::string& error)
    {
        error.clear();
        const auto output = VirtualPath::parse(destination);
        if (!workspace.ready() || !id.valid() || !output.succeeded() || destination.compare(0, 9, "/Project/") != 0 ||
            destination.size() < 6u || destination.substr(destination.size() - 6u) != ".asset")
        {
            error = "Choose a .asset destination inside the project asset root.";
            return false;
        }
        const auto existing = workspace.files().stat(output.value());
        if (existing.succeeded())
        {
            error = "Environment asset already exists; choose another resource name.";
            return false;
        }
        if (existing.status().code != FileErrorCode::NotFound)
        {
            error = existing.status().message;
            return false;
        }
        if (workspace.catalog().index.find(id))
        {
            error = "Environment ID already exists; retry import.";
            return false;
        }
        const auto pair = encode_environment_asset_pair(workspace.types(), id, environment);
        if (!pair.succeeded())
        {
            error = pair.status().message;
            return false;
        }
        const AssetStatus published =
            workspace.asset_pairs().publish(output.value(), pair.value(), FilePublishMode::CreateNew);
        if (!published.succeeded())
        {
            error = published.message;
            return false;
        }
        published_id = id;
        if (!workspace.refresh())
        {
            error = "Environment was saved at " + destination + ", but catalog refresh failed: " + workspace.error();
            return false;
        }
        return true;
    }

} // namespace toy3d
