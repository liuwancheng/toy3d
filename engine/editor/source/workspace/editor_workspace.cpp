#include "workspace/editor_workspace.h"
#include "asset/asset_descriptor_path.h"

#include "file_system/directory_file_store.h"
#include "file_system/virtual_path.h"
#include "asset/material/material_asset.h"
#include "asset/scene/scene_asset.h"
#include "asset/mesh/static_mesh_asset.h"
#include "asset/animation/animation_asset.h"
#include "asset/texture/texture_asset.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "platform/platform_defines.h"

namespace toy3d
{
    bool EditorWorkspace::read_material_properties(const PhysicalPath& registered_root, const std::string& shader_name,
                                                   const shader::ShaderParameterSchema& schema,
                                                   std::vector<shader::ShaderEditorProperty>& properties,
                                                   std::string& error) const
    {
        const auto entries = platform_file_.enumerate_directory(registered_root);
        if (!entries.succeeded())
        {
            error = entries.status().message;
            return false;
        }
        for (const auto& entry : entries.value())
        {
            if (entry.type != FileType::Directory)
            {
                continue;
            }
            std::vector<shader::ShaderEditorProperty> candidate;
            std::string problem;
            if (shader::read_shader_editor_properties(platform_file_, entry.path, shader_name, schema, candidate,
                                                      problem) &&
                !candidate.empty())
            {
                properties = std::move(candidate);
                error.clear();
                return true;
            }
            if (!problem.empty())
            {
                error = problem;
            }
        }
        if (!error.empty())
        {
            return false;
        }
        properties.clear();
        return true;
    }

    namespace
    {
        AssetStatus asset_operation_error(AssetErrorCode code, const std::string& message)
        {
            return {code, {}, {}, {}, {}, message, {}};
        }

        bool writable_asset_path(const VirtualPath& path)
        {
            const std::string& name = path.utf8();
            return name.compare(0u, 9u, "/Project/") == 0 &&
                   asset_descriptor_kind(path) != AssetDescriptorKind::Invalid;
        }

        std::string comparable_path(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            while (path.size() > 1 && path.back() == '/')
            {
                path.pop_back();
            }
#if WITH_WIN
            std::transform(path.begin(), path.end(), path.begin(),
                           [](unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
#endif
            return path;
        }

        bool paths_overlap(const PhysicalPath& first, const PhysicalPath& second)
        {
            const std::string left = comparable_path(first.utf8());
            const std::string right = comparable_path(second.utf8());
            return left == right || left.compare(0, right.size() + 1u, right + "/") == 0 ||
                   right.compare(0, left.size() + 1u, left + "/") == 0;
        }
    } // namespace

    bool EditorWorkspace::initialize(const EditorWorkspacePaths& paths,
                                     std::function<bool(TypeRegistry&)> register_project_types)
    {
        if (ready_)
        {
            error_ = "Editor workspace is already initialized.";
            return false;
        }
        const bool has_project = !paths.project_assets.empty();
        const auto source =
            has_project ? platform_file_.canonical(paths.project_assets) : FileResult<PhysicalPath>(PhysicalPath{});
        const auto deployed = platform_file_.canonical(paths.deployment);
        const auto engine = platform_file_.canonical(paths.engine_assets);
        const auto resources = platform_file_.canonical(paths.editor_resources);
        if (!source.succeeded() || !deployed.succeeded() || !engine.succeeded() || !resources.succeeded())
        {
            error_ = "Editor project, engine, interface resource or deployment root could not be resolved.";
            return false;
        }
        if (has_project &&
            (paths_overlap(source.value(), deployed.value()) || paths_overlap(source.value(), engine.value()) ||
             paths_overlap(source.value(), resources.value())))
        {
            error_ = "Project asset source must not overlap deployment, engine assets or Editor interface resources.";
            return false;
        }

        auto mount_directory = [&](const PhysicalPath& physical_root, const char* virtual_root, bool writable)
        {
            DirectoryFileStoreDesc descriptor;
            descriptor.physical_root = physical_root;
            descriptor.writable = writable;
            descriptor.debug_name = virtual_root;
            const auto store = DirectoryFileStore::create(platform_file_, descriptor);
            if (!store.succeeded())
            {
                return store.status();
            }
            const auto root = VirtualPath::parse(virtual_root);
            if (!root.succeeded())
            {
                return root.status();
            }
            FileMountDesc mount;
            mount.virtual_root = root.value();
            mount.store = store.value();
            mount.access = writable ? MountAccess::ReadWrite : MountAccess::ReadOnly;
            mount.allow_enumeration = true;
            mount.debug_name = virtual_root;
            return files_.add_mount(mount);
        };
        FileStatus mounted = has_project ? mount_directory(source.value(), "/Project", true) : FileStatus::success();
        if (mounted.succeeded())
        {
            mounted = mount_directory(engine.value(), "/Engine", false);
        }
        if (mounted.succeeded())
        {
            mounted = mount_directory(resources.value(), "/Editor/Resources", false);
        }
        const auto saved = paths.saved.empty() ? platform_file_.join_relative(deployed.value(), "saved")
                                               : FileResult<PhysicalPath>(paths.saved);
        if (mounted.succeeded() && saved.succeeded())
        {
            mounted = platform_file_.create_directories(saved.value());
        }
        if (mounted.succeeded() && saved.succeeded())
        {
            mounted = mount_directory(saved.value(), "/Saved", true);
        }
        if (!saved.succeeded())
        {
            error_ = saved.status().message;
            return false;
        }
        if (!mounted.succeeded())
        {
            error_ = mounted.message;
            return false;
        }
        const FileStatus frozen = files_.freeze();
        if (!frozen.succeeded())
        {
            error_ = frozen.message;
            return false;
        }
        ReflectionStatus registered = register_static_mesh_asset_types(types_);
        if (registered.succeeded())
        {
            registered = register_animation_asset_types(types_);
        }
        if (registered.succeeded())
        {
            registered = register_material_asset_types(types_);
        }
        if (registered.succeeded())
        {
            registered = register_texture_asset_types(types_);
        }
        if (registered.succeeded())
        {
            registered = register_scene_asset_types(types_);
        }
        if (registered.succeeded() && register_project_types && !register_project_types(types_))
        {
            error_ = "Project type registration failed.";
            return false;
        }
        if (registered.succeeded())
        {
            registered = types_.freeze();
        }
        if (!registered.succeeded())
        {
            error_ = registered.message;
            return false;
        }
        asset_pairs_ = std::make_unique<AssetPairStore>(types_, files_);
        source_root_ = source.value();
        ready_ = true;
        return refresh();
    }

    bool EditorWorkspace::refresh()
    {
        if (!ready_)
        {
            error_ = "Editor asset workspace is not initialized.";
            return false;
        }
        const auto project_root = VirtualPath::parse("/Project");
        const auto engine_root = VirtualPath::parse("/Engine");
        if (!project_root.succeeded() || !engine_root.succeeded())
        {
            error_ = "Editor asset catalog roots could not be parsed.";
            return false;
        }
        const AssetStatus recovered =
            has_project() ? asset_pairs_->recover_tree(project_root.value()) : AssetStatus::success();
        if (!recovered.succeeded())
        {
            error_ = recovered.virtual_path + ": " + recovered.message;
            return false;
        }
        std::vector<VirtualPath> roots{engine_root.value()};
        if (has_project())
        {
            roots.push_back(project_root.value());
        }
        auto scanned = scan_asset_catalog(types_, files_, roots);
        if (!scanned.succeeded())
        {
            error_ = scanned.status().virtual_path + ": " + scanned.status().message;
            return false;
        }
        catalog_ = scanned.value();
        error_.clear();
        return true;
    }

    AssetStatus EditorWorkspace::delete_asset(const AssetId& id)
    {
        if (!ready_)
        {
            return asset_operation_error(AssetErrorCode::InvalidState, "Editor workspace is not ready.");
        }
        const auto* location = catalog_.index.find(id);
        if (!location)
        {
            return asset_operation_error(AssetErrorCode::MissingReference, "Asset was not found.");
        }
        if (!writable_asset_path(location->path))
        {
            return asset_operation_error(AssetErrorCode::ReadOnly, "Only Project assets can be deleted.");
        }
        for (const AssetCatalogEntry& entry : catalog_.entries)
        {
            for (const AssetRef& dependency : entry.file.dependencies)
            {
                if (dependency.asset_id == id && dependency.strength == AssetRefStrength::Strong)
                {
                    return asset_operation_error(AssetErrorCode::Conflict, "Asset is used by " + entry.path.utf8() +
                                                                               ". Remove that reference first.");
                }
            }
        }
        const AssetStatus removed = asset_pairs_->remove(location->path);
        if (!removed.succeeded())
        {
            return removed;
        }
        if (!refresh())
        {
            return asset_operation_error(AssetErrorCode::InvalidState,
                                         "Asset was deleted, but refresh failed: " + error_);
        }
        return AssetStatus::success();
    }

    AssetStatus EditorWorkspace::move_asset(const AssetId& id, const VirtualPath& destination)
    {
        if (!ready_)
        {
            return asset_operation_error(AssetErrorCode::InvalidState, "Editor workspace is not ready.");
        }
        const auto* location = catalog_.index.find(id);
        if (!location)
        {
            return asset_operation_error(AssetErrorCode::MissingReference, "Asset was not found.");
        }
        if (!writable_asset_path(location->path) || !writable_asset_path(destination))
        {
            return asset_operation_error(AssetErrorCode::ReadOnly, "Asset move must stay inside Project assets.");
        }
        const AssetStatus moved = asset_pairs_->move(location->path, destination);
        if (!moved.succeeded())
        {
            return moved;
        }
        if (!refresh())
        {
            return asset_operation_error(AssetErrorCode::InvalidState,
                                         "Asset was moved, but refresh failed: " + error_);
        }
        return AssetStatus::success();
    }

    AssetResult<AssetId> EditorWorkspace::copy_asset(const AssetId& id, const VirtualPath& destination)
    {
        if (!ready_)
        {
            return AssetResult<AssetId>(
                asset_operation_error(AssetErrorCode::InvalidState, "Editor workspace is not ready."));
        }
        const auto* location = catalog_.index.find(id);
        if (!location)
        {
            return AssetResult<AssetId>(
                asset_operation_error(AssetErrorCode::MissingReference, "Asset was not found."));
        }
        if (!writable_asset_path(location->path) || !writable_asset_path(destination))
        {
            return AssetResult<AssetId>(
                asset_operation_error(AssetErrorCode::ReadOnly, "Asset copy must stay inside Project assets."));
        }
        const auto copied = asset_pairs_->copy(location->path, destination);
        if (!copied.succeeded())
        {
            return copied;
        }
        if (!refresh())
        {
            return AssetResult<AssetId>(
                asset_operation_error(AssetErrorCode::InvalidState, "Asset was copied, but refresh failed: " + error_));
        }
        return copied;
    }
} // namespace toy3d
