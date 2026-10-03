#include "assets/animation/skeletal_mesh_asset_tools.h"

#include <algorithm>
#include <exception>
#include <set>

#include "asset/asset_descriptor_path.h"
#include "asset/mesh/mesh_materials.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "misc/sha256.h"
#include "threading/task_graph/graph_task.h"
#include "threading/task_graph/task_graph_interface.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        bool project_destination(const std::string& path)
        {
            return path.compare(0, 9, "/Project/") == 0 && path.size() > 15 &&
                   path.substr(path.size() - 6) == ".asset" && VirtualPath::parse(path).succeeded();
        }

        bool source_mount(NativePlatformFile& platform, const PhysicalPath& root, FileSystem& files, std::string& error)
        {
            DirectoryFileStoreDesc desc;
            desc.physical_root = root;
            desc.writable = false;
            const auto store = DirectoryFileStore::create(platform, desc);
            if (!store.succeeded())
            {
                error = store.status().message;
                return false;
            }
            FileMountDesc mount;
            mount.virtual_root = VirtualPath::parse("/Source").value();
            mount.store = store.value();
            const auto mounted = files.add_mount(mount);
            const auto frozen = files.freeze();
            if (!mounted.succeeded() || !frozen.succeeded())
            {
                error = !mounted.succeeded() ? mounted.message : frozen.message;
                return false;
            }
            return true;
        }

        bool baseline_matches(EditorWorkspace& workspace, const std::string& path, const AssetId& id,
                              const std::vector<std::uint8_t>& baseline, std::string& error)
        {
            const auto* location = workspace.catalog().index.find(id);
            const auto current = workspace.asset_pairs().read(VirtualPath::parse(path).value());
            if (!location || location->path.utf8() != path || !current.succeeded() ||
                !(current.value().description.index.asset_id == id) || current.value().description_bytes != baseline)
            {
                error = "Asset changed, moved or became unreadable during import: " + path;
                return false;
            }
            return true;
        }

        bool source_matches(const PreparedSkeletalImport& result, std::string& error)
        {
            NativePlatformFile platform;
            FileSystem files;
            if (!source_mount(platform, result.source_root, files, error))
            {
                return false;
            }
            std::size_t total = 0;
            if (result.sources.empty() || result.sources.size() > 256)
            {
                error = "Missing or oversized source content baseline.";
                return false;
            }
            for (const auto& source : result.sources)
            {
                constexpr std::size_t maximum = 256u * 1024u * 1024u;
                constexpr std::size_t maximum_file = 64u * 1024u * 1024u;
                if (source.path.utf8().compare(0, 8, "/Source/") != 0 || total >= maximum)
                {
                    error = "Source content baseline exceeds import limits.";
                    return false;
                }
                const auto bytes = files.read_binary(source.path, std::min(maximum_file, maximum - total));
                if (!bytes.succeeded() || sha256_to_hex(sha256(bytes.value())) != source.content_hash)
                {
                    error = "Source changed or became unreadable during import: " + source.path.utf8();
                    return false;
                }
                total += bytes.value().size();
            }
            return true;
        }
    } // namespace

    bool capture_skeletal_import(EditorWorkspace& workspace, const PhysicalPath& source, const std::string& destination,
                                 SkeletalImportMode mode, const AssetId& skeleton_id,
                                 const SkeletalMeshImportOptions& options, const AssetId& reimport_id,
                                 SkeletalImportRequest& request, std::string& error)
    {
        error.clear();
        if (!workspace.ready() || !workspace.has_project() || !project_destination(destination))
        {
            error = "Choose a .asset destination in writable Project content.";
            return false;
        }
        SkeletalImportRequest candidate;
        candidate.source = source;
        candidate.destination = destination;
        candidate.mode = mode;
        candidate.options = options;
        candidate.workspace_root = workspace.source_root().utf8();
        candidate.reimport_id = reimport_id;
        candidate.skeleton_id = skeleton_id;
        if (reimport_id.valid())
        {
            const auto* location = workspace.catalog().index.find(reimport_id);
            const auto pair = workspace.asset_pairs().read(VirtualPath::parse(destination).value());
            if (!location || location->path.utf8() != destination || !pair.succeeded() ||
                !(pair.value().description.index.asset_id == reimport_id))
            {
                error = "Reimport target is missing, moved or unreadable.";
                return false;
            }
            AssetRef reference;
            if (mode == SkeletalImportMode::AnimationOnly)
            {
                const auto decoded = decode_animation_sequence_asset_pair(pair.value());
                if (!decoded.succeeded())
                {
                    error = decoded.status().message;
                    return false;
                }
                reference = decoded.value().data.skeleton;
            }
            else
            {
                const auto decoded = decode_skeletal_mesh_asset_pair(pair.value());
                if (!decoded.succeeded())
                {
                    error = decoded.status().message;
                    return false;
                }
                reference = decoded.value().data.skeleton;
                candidate.material_slots = decoded.value().data.material_slots;
                candidate.default_materials = decoded.value().data.default_materials;
            }
            candidate.skeleton_id = reference.asset_id;
            candidate.target_baseline = pair.value().description_bytes;
        }
        if (candidate.skeleton_id.valid())
        {
            const auto* location = workspace.catalog().index.find(candidate.skeleton_id);
            if (!location)
            {
                error = "Select an existing Skeleton asset.";
                return false;
            }
            const auto pair = workspace.asset_pairs().read(location->path);
            if (!pair.succeeded() || !(pair.value().description.index.asset_id == candidate.skeleton_id))
            {
                error = "Selected Skeleton is unreadable or its identity changed.";
                return false;
            }
            const auto decoded = decode_skeleton_asset_pair(pair.value());
            if (!decoded.succeeded())
            {
                error = decoded.status().message;
                return false;
            }
            candidate.skeleton = decoded.value();
            candidate.skeleton_path = location->path.utf8();
            candidate.skeleton_baseline = pair.value().description_bytes;
        }
        else if (mode == SkeletalImportMode::AnimationOnly)
        {
            error = "Animation-only import requires an existing Skeleton.";
            return false;
        }
        request = std::move(candidate);
        return true;
    }

    bool prepare_skeletal_import(const SkeletalImportRequest& request, PreparedSkeletalImport& result)
    {
        result = {};
#if WITH_MODEL_IMPORT
        NativePlatformFile platform;
        const auto canonical = platform.canonical(request.source);
        if (!canonical.succeeded())
        {
            result.error = canonical.status().message;
            return false;
        }
        const auto parent = platform.parent_path(canonical.value());
        if (!parent.succeeded())
        {
            result.error = parent.status().message;
            return false;
        }
        result.source_root = parent.value();
        FileSystem files;
        if (!source_mount(platform, parent.value(), files, result.error))
        {
            return false;
        }
        const auto& host = canonical.value().utf8();
        const auto source = VirtualPath::parse("/Source/" + host.substr(host.find_last_of("/\\") + 1));
        AssetId skeleton_id = request.skeleton_id;
        if (!source.succeeded() || (!skeleton_id.valid() && !AssetId::try_generate(skeleton_id)))
        {
            result.error = "Could not resolve source path or generate a Skeleton ID.";
            return false;
        }
        const auto imported =
            request.skeleton_id.valid()
                ? import_skeletal_mesh(files, source.value(), skeleton_id, request.skeleton, request.options)
                : import_skeletal_mesh(files, source.value(), skeleton_id, request.options);
        if (!imported.succeeded())
        {
            result.error = imported.status().message;
            return false;
        }
        if (request.mode == SkeletalImportMode::AnimationOnly && imported.value().animations.size() != 1)
        {
            result.error =
                "Animation-only import requires exactly one clip and a compatible preview mesh in the source.";
            return false;
        }
        TypeRegistry types;
        const auto registered = register_animation_asset_types(types);
        const auto frozen = types.freeze();
        if (!registered.succeeded() || !frozen.succeeded())
        {
            result.error = "Animation schema registration failed.";
            return false;
        }
        const std::string stem = request.destination.substr(0, request.destination.size() - 6);
        auto append = [&](const std::string& path, const AssetId& id, const AssetResult<AssetPairBytes>& bytes)
        {
            const auto parsed = VirtualPath::parse(path);
            if (!parsed.succeeded() || !bytes.succeeded())
            {
                result.error = !parsed.succeeded() ? parsed.status().message : bytes.status().message;
                return false;
            }
            result.outputs.push_back({id, parsed.value(), bytes.value()});
            return true;
        };
        if (!request.skeleton_id.valid() &&
            !append(stem + "_Skeleton.asset", skeleton_id,
                    encode_skeleton_asset_pair(types, skeleton_id, imported.value().skeleton)))
        {
            return false;
        }
        if (request.mode == SkeletalImportMode::MeshAndAnimations)
        {
            auto mesh = imported.value().mesh;
            if (request.reimport_id.valid())
            {
                const auto remapped =
                    remap_mesh_materials(request.material_slots, request.default_materials, mesh.data.material_slots);
                if (!remapped.succeeded())
                {
                    result.error = remapped.status().message;
                    return false;
                }
                mesh.data.default_materials = remapped.value();
            }
            AssetId mesh_id = request.reimport_id;
            if ((!mesh_id.valid() && !AssetId::try_generate(mesh_id)) ||
                !append(request.destination, mesh_id, encode_skeletal_mesh_asset_pair(types, mesh_id, mesh)))
            {
                if (result.error.empty())
                {
                    result.error = "Could not generate a mesh ID.";
                }
                return false;
            }
        }
        // Mesh reimport touches only the selected mesh; associated clips retain independent identities.
        if (!request.reimport_id.valid() || request.mode == SkeletalImportMode::AnimationOnly)
        {
            for (std::size_t i = 0; i < imported.value().animations.size(); ++i)
            {
                AssetId id = request.reimport_id;
                const auto path = request.mode == SkeletalImportMode::AnimationOnly
                                      ? request.destination
                                      : stem + "_Animation_" + std::to_string(i) + ".asset";
                if ((!id.valid() && !AssetId::try_generate(id)) ||
                    !append(path, id,
                            encode_animation_sequence_asset_pair(types, id, imported.value().animations[i].sequence)))
                {
                    if (result.error.empty())
                    {
                        result.error = "Could not generate an animation ID.";
                    }
                    return false;
                }
            }
        }
        result.warnings = imported.value().warnings;
        result.sources = imported.value().sources;
        return source_matches(result, result.error);
#else
        result.error = "Model import is disabled in this build.";
        return false;
#endif
    }

    bool publish_skeletal_import(EditorWorkspace& workspace, const SkeletalImportRequest& request,
                                 const PreparedSkeletalImport& result, std::vector<AssetId>& committed,
                                 std::string& error)
    {
        error.clear();
        committed.clear();
        if (!result.error.empty() || result.outputs.empty() || !workspace.ready() || !workspace.has_project() ||
            workspace.source_root().utf8() != request.workspace_root)
        {
            error = result.error.empty() ? "Import result is empty or belongs to a different workspace." : result.error;
            return false;
        }
        if (!source_matches(result, error) ||
            (request.skeleton_id.valid() && !baseline_matches(workspace, request.skeleton_path, request.skeleton_id,
                                                              request.skeleton_baseline, error)) ||
            (request.reimport_id.valid() &&
             !baseline_matches(workspace, request.destination, request.reimport_id, request.target_baseline, error)))
        {
            return false;
        }
        std::set<std::string> paths;
        std::set<std::string> ids;
        for (const auto& output : result.outputs)
        {
            if (!project_destination(output.path.utf8()) || !output.id.valid() ||
                !paths.insert(output.path.utf8()).second || !ids.insert(output.id.hex()).second)
            {
                error = "Invalid or duplicated import output.";
                return false;
            }
            if (request.reimport_id.valid())
            {
                if (result.outputs.size() != 1 || !(output.id == request.reimport_id) ||
                    output.path.utf8() != request.destination)
                {
                    error = "Reimport must replace only the selected asset.";
                    return false;
                }
                continue;
            }
            if (workspace.catalog().index.find(output.id))
            {
                error = "Generated asset ID already exists; restart import.";
                return false;
            }
            VirtualPath meta;
            if (!asset_meta_path(output.path, meta))
            {
                error = "Invalid paired output path.";
                return false;
            }
            for (const auto& path : {output.path, meta})
            {
                const auto existing = workspace.files().stat(path);
                if (existing.succeeded() || existing.status().code != FileErrorCode::NotFound)
                {
                    error = "Output already exists or cannot be inspected: " + path.utf8();
                    return false;
                }
            }
        }
        for (const auto& output : result.outputs)
        {
            const auto status = workspace.asset_pairs().publish(
                output.path, output.bytes,
                request.reimport_id.valid() ? FilePublishMode::Replace : FilePublishMode::CreateNew);
            if (!status.succeeded())
            {
                error = std::to_string(committed.size()) + "/" + std::to_string(result.outputs.size()) +
                        " assets committed; failed " + output.path.utf8() + ": " + status.message;
                break;
            }
            committed.push_back(output.id);
        }
        if (!committed.empty() && !workspace.refresh())
        {
            error += " Assets were saved, but catalog refresh failed: " + workspace.error();
        }
        return error.empty();
    }

    // --------------------------------------------------------------------------
    // SkeletalImportJob: owns CPU work and retains publication authority on the GT
    // --------------------------------------------------------------------------
    SkeletalImportJob::~SkeletalImportJob()
    {
        shutdown();
    }

    bool SkeletalImportJob::start(SkeletalImportRequest request, std::string& error)
    {
        if (task_ || !TaskGraphInterface::is_running())
        {
            error = "Import requires an idle job and a running Task Graph.";
            return false;
        }
        request_ = std::move(request);
        result_ = std::make_shared<PreparedSkeletalImport>();
        committed_.clear();
        warnings_.clear();
        error_.clear();
        cancelled_ = false;
        auto result = result_;
        const auto input = request_;
        try
        {
            task_ = dispatch_graph_task(TaskGraphInterface::get(), "Import Skeletal Asset",
                                        [input, result](NamedThread, const GraphEventRef&)
                                        {
                                            try
                                            {
                                                prepare_skeletal_import(input, *result);
                                            }
                                            catch (const std::exception& exception)
                                            {
                                                result->error = exception.what();
                                            }
                                        });
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            result_.reset();
            return false;
        }
        return true;
    }

    bool SkeletalImportJob::update(EditorWorkspace& workspace)
    {
        if (!task_ || !task_->is_complete())
        {
            return false;
        }
        if (!cancelled_)
        {
            warnings_ = result_->warnings;
            if (task_->get_outcome() != TaskOutcome::Succeeded)
            {
                error_ = "Skeletal import worker failed.";
            }
            else
            {
                publish_skeletal_import(workspace, request_, *result_, committed_, error_);
            }
        }
        task_.reset();
        result_.reset();
        request_ = {};
        return true;
    }

    void SkeletalImportJob::cancel()
    {
        cancelled_ = true;
    }

    void SkeletalImportJob::shutdown()
    {
        cancel();
        if (task_ && !task_->is_complete() && TaskGraphInterface::is_running())
        {
            TaskGraphInterface::get().wait_until_task_completes(task_);
        }
        task_.reset();
        result_.reset();
        request_ = {};
    }

    bool SkeletalImportJob::busy() const
    {
        return static_cast<bool>(task_);
    }

    const std::string& SkeletalImportJob::error() const
    {
        return error_;
    }

    const std::vector<AssetId>& SkeletalImportJob::committed() const
    {
        return committed_;
    }

    const std::vector<std::string>& SkeletalImportJob::warnings() const
    {
        return warnings_;
    }
} // namespace toy3d
