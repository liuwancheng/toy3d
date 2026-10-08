#include "assets/thumbnails/asset_thumbnail_pool.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <utility>

#include "assets/thumbnails/thumbnail_source.h"
#include "assets/material/material_thumbnail.h"
#include "logging/logger.h"
#include "threading/task_graph/graph_task.h"
#include "asset/material/material_asset.h"
#include "asset/texture/builtin_texture_assets.h"
#include "asset_loader/asset_loader.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/texture/texture_load_job.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        constexpr std::size_t cache_capacity = 128;

        const AssetCatalogEntry* find_asset(const EditorWorkspace& workspace, const AssetId& id)
        {
            for (const auto& asset : workspace.catalog().entries)
            {
                if (asset.file.asset_id == id)
                {
                    return &asset;
                }
            }
            return nullptr;
        }
    } // namespace

    struct AssetThumbnailPool::CpuResult
    {
        std::uint64_t request_id = 0;
        bool saving = false;
        bool validating = false;
        std::string error;
        ThumbnailSource input;
    };

    // --------------------------------------------------------------------------
    // AssetThumbnailPool: serial jobs and GT adoption of validated UI images
    // --------------------------------------------------------------------------
    AssetThumbnailPool::AssetThumbnailPool(EditorWorkspace& workspace) : workspace_(workspace)
    {
    }
    AssetThumbnailPool::~AssetThumbnailPool()
    {
        shutdown();
    }

    bool AssetThumbnailPool::initialize(SceneInterface& scene, MaterialInstanceRef material, TaskGraphInterface& tasks)
    {
        tasks_ = &tasks;
        SceneEnvironmentSettings environment;
        if (!AssetId::parse(builtin_studio_environment_id, environment.environment.asset_id))
        {
            TOY_LOG_ERROR("Invalid built-in studio environment identity.");
            return false;
        }
        environment.environment.expected_type = "toy3d.EnvironmentAssetData";
        if (!assets_)
        {
            TOY_LOG_ERROR("Thumbnail studio environment: the asset loader is unavailable.");
            return false;
        }
        // The first cube is needed before the private World exists, so this one Critical decode
        // is waited for; every later request for the identity is served from the shared cache.
        auto environment_handle =
            request_texture(*assets_, environment.environment, workspace_.catalog().index, AssetLoadPriority::Critical);
        if (!assets_->wait(environment_handle, std::chrono::seconds(5)) || !environment_handle.get())
        {
            TOY_LOG_ERROR("Thumbnail studio environment: {}",
                          environment_handle.failed() ? environment_handle.error() : "decode did not complete");
            return false;
        }
        const auto geometry_path = VirtualPath::parse("/Engine/S_MaterialPreview.asset");
        const auto geometry = read_static_mesh_asset(workspace_.files(), geometry_path.value());
        if (!geometry.succeeded() || !geometry.value().valid_tangent_frame)
        {
            TOY_LOG_ERROR("Material preview geometry is missing or has no tangent frame.");
            return false;
        }
        material_preview_geometry_ = geometry.value();
        initialized_ = preview_.initialize(scene, std::move(material), environment, environment_handle.get());
        if (!initialized_)
        {
            preview_.shutdown();
        }
        return initialized_;
    }

    bool AssetThumbnailPool::make_room()
    {
        if (entries_.size() < cache_capacity)
        {
            return true;
        }
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
        {
            if (it->first == active_id_ || it->second.persist || it->second.last_visible_frame == frame_)
            {
                continue;
            }
            if (oldest == entries_.end() || it->second.last_visible_frame < oldest->second.last_visible_frame)
            {
                oldest = it;
            }
        }
        if (oldest == entries_.end())
        {
            return false;
        }
        if (oldest->second.texture.valid())
        {
            pending_work_.retire_textures.push_back(oldest->second.texture);
        }
        entries_.erase(oldest);
        return true;
    }

    AssetThumbnailView AssetThumbnailPool::request(const AssetCatalogEntry& asset)
    {
        if (!initialized_ ||
            (asset.file.root_type != "toy3d.StaticMeshAssetData" &&
             asset.file.root_type != "toy3d.SkeletalMeshAssetData" &&
             asset.file.root_type != "toy3d.AnimationSequenceAssetData" &&
             asset.file.root_type != "toy3d.Texture2DAssetData" && !is_material_asset_type(asset.file.root_type)))
        {
            return {};
        }
        auto it = entries_.find(asset.file.asset_id);
        if (it == entries_.end())
        {
            if (!make_room())
            {
                return {{}, true, {}};
            }
            Entry entry;
            entry.id = asset.file.asset_id;
            entry.path = asset.path.utf8();
            entry.persist = asset.file.root_type == "toy3d.StaticMeshAssetData" ||
                            asset.file.root_type == "toy3d.SkeletalMeshAssetData" ||
                            asset.file.root_type == "toy3d.AnimationSequenceAssetData";
            it = entries_.emplace(entry.id, std::move(entry)).first;
        }
        Entry& entry = it->second;
        entry.last_visible_frame = frame_;
        return {entry.texture, entry.stage != Stage::Ready && entry.stage != Stage::Failed, entry.error};
    }

    AssetThumbnailView AssetThumbnailPool::request_builtin_mesh(const std::string& kind, StaticMeshRef geometry)
    {
        if (!initialized_ || !geometry || (kind != "Cube" && kind != "Plane"))
        {
            return {};
        }
        AssetId id;
        const auto key = sha256_to_hex(
            sha256("Toy3d builtin thumbnail " + kind + " v" + std::to_string(thumbnail_generator_version)));
        if (!AssetId::parse(key.substr(0, 32), id))
        {
            return {};
        }
        auto found = entries_.find(id);
        if (found == entries_.end())
        {
            if (!make_room())
            {
                return {{}, true, {}};
            }
            Entry entry;
            entry.id = id;
            entry.path = kind;
            entry.builtin_geometry = std::move(geometry);
            found = entries_.emplace(id, std::move(entry)).first;
        }
        auto& entry = found->second;
        entry.last_visible_frame = frame_;
        return {entry.texture, entry.stage != Stage::Ready && entry.stage != Stage::Failed, entry.error};
    }

    void AssetThumbnailPool::set_material_preview_meshes(const StaticMeshRef& plane, const StaticMeshRef& cube)
    {
        material_preview_plane_ = make_material_preview_geometry(plane, true);
        material_preview_cube_ = make_material_preview_geometry(cube, false);
        ++material_preview_revision_;
    }

    AssetThumbnailView AssetThumbnailPool::request_material_preview(const MaterialInstanceRef& material,
                                                                    std::uint64_t revision,
                                                                    const MaterialPreviewSettings& settings)
    {
        if (!initialized_ || !material)
        {
            return {};
        }
        if (!validate_material_preview_settings(settings))
        {
            return {material_preview_texture_, false, "Invalid material preview settings."};
        }
        if (preview_material_.lock() != material || material_source_revision_ != revision ||
            !(material_preview_settings_ == settings))
        {
            if (material_preview_revision_ == std::numeric_limits<std::uint64_t>::max())
            {
                return {material_preview_texture_, false, "Material preview revision space exhausted."};
            }
            ++material_preview_revision_;
            material_source_revision_ = revision;
            material_preview_settings_ = settings;
            material_preview_error_.clear();
        }
        preview_material_ = material;
        material_preview_visible_frame_ = frame_;
        return {material_preview_texture_,
                material_preview_active_ || rendered_preview_revision_ != material_preview_revision_ ||
                    rendered_preview_material_.lock() != material,
                material_preview_error_};
    }

    void AssetThumbnailPool::clear_material_preview()
    {
        preview_material_.reset();
        rendered_preview_material_.reset();
        material_preview_error_.clear();
        if (material_preview_active_)
        {
            // An already submitted result is retired on delivery; do not reuse the World until it arrives.
            preview_.clear_mesh();
            material_preview_cancelled_ = true;
            if (pending_work_.preview.request_id == material_preview_request_)
            {
                pending_work_.preview = {};
                material_preview_active_ = false;
                material_preview_candidate_ = {};
            }
        }
        // StaticMeshComponent removal retains its CPU material through a FIFO command.
        // Drain that removal before the asset editor performs its required final release.
        if (initialized_ && !flush_rendering_commands().succeeded())
        {
            TOY_LOG_ERROR("Could not drain the material preview references.");
        }
        if (material_preview_texture_.valid())
        {
            pending_work_.retire_textures.push_back(material_preview_texture_);
            material_preview_texture_ = {};
        }
    }

    void AssetThumbnailPool::generate(const AssetId& id)
    {
        const auto* asset = find_asset(workspace_, id);
        if (!asset || (asset->file.root_type != "toy3d.StaticMeshAssetData" &&
                       asset->file.root_type != "toy3d.SkeletalMeshAssetData" &&
                       asset->file.root_type != "toy3d.AnimationSequenceAssetData"))
        {
            return;
        }
        request(*asset);
        const auto it = entries_.find(id);
        if (it == entries_.end())
        {
            TOY_LOG_WARN("Thumbnail cache is busy; retry generation.");
            return;
        }
        Entry& entry = it->second;
        entry.persist = true;
        if (entry.id == active_id_)
        {
            entry.rerun = true;
            return;
        }
        entry.force = true;
        entry.stage = Stage::Queued;
        entry.error.clear();
    }

    void AssetThumbnailPool::invalidate()
    {
        // Save/rescan callbacks can run after another panel emitted Image commands.
        // Keep their IDs and RT bindings until the next pre-UI tick; retirement
        // is processed before rendering the frame that carries it.
        full_invalidation_ = true;
        invalidation_pending_ = true;
    }

    void AssetThumbnailPool::invalidate(const AssetId& changed)
    {
        if (!changed.valid())
        {
            invalidate();
            return;
        }
        // Targeted invalidation defers exactly like the full path: repeated callbacks in
        // one frame coalesce and no registered image disappears before its tick.
        if (std::find(pending_invalidations_.begin(), pending_invalidations_.end(), changed) ==
            pending_invalidations_.end())
        {
            pending_invalidations_.push_back(changed);
        }
        invalidation_pending_ = true;
    }

    void AssetThumbnailPool::apply_invalidation()
    {
        invalidation_pending_ = false;
        if (full_invalidation_)
        {
            full_invalidation_ = false;
            pending_invalidations_.clear();
            if (assets_)
            {
                assets_->invalidate_all();
            }
            ++material_preview_revision_;
            for (auto& pair : entries_)
            {
                Entry& entry = pair.second;
                const auto* asset = find_asset(workspace_, entry.id);
                // Only mesh/animation captures have source snapshots for the disk cache.
                // Rescanning must not enable persistence on live material or texture previews.
                entry.persist = asset && (asset->file.root_type == "toy3d.StaticMeshAssetData" ||
                                          asset->file.root_type == "toy3d.SkeletalMeshAssetData" ||
                                          asset->file.root_type == "toy3d.AnimationSequenceAssetData");
                if (entry.id == active_id_)
                {
                    entry.rerun = true;
                    continue;
                }
                if (entry.texture.valid())
                {
                    pending_work_.retire_textures.push_back(entry.texture);
                }
                entry.texture = {};
                entry.stage = Stage::Queued;
                entry.error.clear();
            }
            return;
        }
        if (pending_invalidations_.empty())
        {
            return;
        }
        // Only the changed identities and the strong dependencies that render differently
        // are affected; unrelated cached images stay usable.
        std::vector<AssetId> affected = pending_invalidations_;
        pending_invalidations_.clear();
        for (std::size_t index = 0u; index < affected.size(); ++index)
        {
            for (const auto& asset : workspace_.catalog().entries)
            {
                const bool depends = std::any_of(asset.file.dependencies.begin(), asset.file.dependencies.end(),
                                                 [&](const AssetRef& dependency)
                                                 {
                                                     return dependency.strength == AssetRefStrength::Strong &&
                                                            dependency.asset_id == affected[index];
                                                 });
                if (depends && std::find(affected.begin(), affected.end(), asset.file.asset_id) == affected.end())
                {
                    affected.push_back(asset.file.asset_id);
                }
            }
        }
        for (const auto& id : affected)
        {
            const auto* location = workspace_.catalog().index.find(id);
            const auto found = entries_.find(id);
            if (!location)
            {
                // A removed asset drops its cached image instead of requeueing a dead entry.
                if (found == entries_.end())
                {
                    continue;
                }
                Entry& removed = found->second;
                if (active_id_ == id)
                {
                    // An in-flight candidate never returns to an erased entry, and the result
                    // callback drops it, so its renderer texture and preview mesh are released
                    // here instead of leaking.
                    if (removed.candidate_texture.valid())
                    {
                        pending_work_.retire_textures.push_back(removed.candidate_texture);
                    }
                    preview_.clear_mesh();
                    active_id_ = {};
                }
                if (removed.texture.valid())
                {
                    pending_work_.retire_textures.push_back(removed.texture);
                }
                entries_.erase(found);
                continue;
            }
            // The shared loader owns decoded textures for every consumer, so a changed
            // identity is dropped there as well as in this pool's image cache.
            if (assets_)
            {
                assets_->invalidate(id);
            }
            if (location->index.root_type == "toy3d.EnvironmentAssetData")
            {
                // The live preview samples the cube, so a changed Environment re-renders it.
                ++material_preview_revision_;
            }
            if (found == entries_.end())
            {
                continue;
            }
            Entry& entry = found->second;
            if (entry.id == active_id_)
            {
                entry.rerun = true;
                continue;
            }
            if (entry.texture.valid())
            {
                pending_work_.retire_textures.push_back(entry.texture);
            }
            entry.texture = {};
            entry.stage = Stage::Queued;
            entry.error.clear();
        }
    }

    void AssetThumbnailPool::start_load(Entry& entry)
    {
        if (cpu_task_ && !cpu_task_->is_complete())
        {
            // A deleted entry abandons its running task; dispatching now would overwrite
            // that handle and leave the task unwaited by shutdown(). Retry next frame.
            return;
        }
        const auto* asset = find_asset(workspace_, entry.id);
        if (!asset && !entry.builtin_geometry)
        {
            fail(entry, "Asset was removed from the catalog.");
            return;
        }
        if (asset)
        {
            entry.path = asset->path.utf8();
        }
        if (next_request_ == std::numeric_limits<std::uint64_t>::max() || next_texture_ >= (1ull << 40))
        {
            fail(entry, "Thumbnail identifier space exhausted.");
            return;
        }
        entry.request_id = next_request_++;
        entry.candidate_texture = ImGuiTextureId(next_texture_++);
        entry.stage = Stage::Loading;
        active_id_ = entry.id;
        if (entry.builtin_geometry)
        {
            entry.persist = false;
            if (!preview_.configure_thumbnail() || !preview_.prepare(entry.builtin_geometry))
            {
                fail(entry, "Could not prepare the built-in thumbnail mesh.");
                return;
            }
            entry.stage = Stage::AwaitGpu;
            pending_work_.preview = {entry.request_id,
                                     entry.candidate_texture,
                                     {thumbnail_default_size, thumbnail_default_size},
                                     {preview_.view()}};
            return;
        }
        if (is_material_asset_type(asset->file.root_type))
        {
            AssetRef reference;
            reference.asset_id = asset->file.asset_id;
            reference.expected_type = asset->file.root_type;
            const auto prepared =
                prepare_material_thumbnail(preview_, material_preview_geometry_, reference, material_resolver_);
            if (!prepared.succeeded())
            {
                fail(entry, prepared.message);
                return;
            }
            entry.stage = Stage::AwaitGpu;
            pending_work_.preview = {entry.request_id,
                                     entry.candidate_texture,
                                     {thumbnail_default_size, thumbnail_default_size},
                                     {preview_.view()}};
            return;
        }
        auto result = std::make_shared<CpuResult>();
        result->request_id = entry.request_id;
        cpu_result_ = result;
        const AssetCatalogEntry snapshot = *asset;
        const auto catalog = workspace_.catalog();
        const bool force = entry.force;
        FileSystem* files = &workspace_.files();
        AssetPairStore* pairs = &workspace_.asset_pairs();
        // A deleted entry can abandon its worker task; never overwrite a handle that is
        // still running, or shutdown() would stop waiting for that task.
        cpu_task_ =
            dispatch_graph_task(*tasks_, "Load asset thumbnail",
                                [result, files, pairs, snapshot, catalog, force](NamedThread, const GraphEventRef&)
                                {
                                    try
                                    {
                                        auto loaded = load_thumbnail_source(*files, *pairs, catalog, snapshot, force);
                                        if (!loaded.succeeded())
                                        {
                                            result->error = loaded.status().message;
                                            return;
                                        }
                                        result->input = std::move(loaded).value();
                                    }
                                    catch (const std::exception& error)
                                    {
                                        result->error = error.what();
                                    }
                                });
    }

    void AssetThumbnailPool::start_save(Entry& entry)
    {
        const auto path = VirtualPath::parse(entry.path);
        if (!path.succeeded())
        {
            fail(entry, "Thumbnail save path is invalid.");
            return;
        }
        entry.stage = Stage::Saving;
        auto result = std::make_shared<CpuResult>();
        result->request_id = entry.request_id;
        result->saving = true;
        result->input.original = std::move(entry.original);
        result->input.pixels = std::move(entry.pixels);
        result->input.source = entry.source;
        result->input.skeletal = entry.skeletal;
        cpu_result_ = result;
        FileSystem* files = &workspace_.files();
        AssetPairStore* pairs = &workspace_.asset_pairs();
        const auto catalog = workspace_.catalog();
        const AssetId id = entry.id;
        cpu_task_ = dispatch_graph_task(
            *tasks_, "Save asset thumbnail",
            [result, files, pairs, catalog, id, path = path.value()](NamedThread, const GraphEventRef&)
            {
                try
                {
                    const auto saved = save_thumbnail_cache(*files, *pairs, catalog, id, path, result->input,
                                                            std::move(result->input.pixels));
                    if (!saved.succeeded())
                    {
                        result->error = saved.message;
                    }
                }
                catch (const std::exception& error)
                {
                    result->error = error.what();
                }
            });
    }

    void AssetThumbnailPool::start_validate(Entry& entry)
    {
        const auto path = VirtualPath::parse(entry.path);
        if (!path.succeeded())
        {
            fail(entry, "Thumbnail validation path is invalid.");
            return;
        }
        entry.stage = Stage::Validating;
        auto result = std::make_shared<CpuResult>();
        result->request_id = entry.request_id;
        result->validating = true;
        result->input.original = std::move(entry.original);
        result->input.skeletal = entry.skeletal;
        cpu_result_ = result;
        FileSystem* files = &workspace_.files();
        AssetPairStore* pairs = &workspace_.asset_pairs();
        const auto catalog = workspace_.catalog();
        const AssetId id = entry.id;
        cpu_task_ = dispatch_graph_task(
            *tasks_, "Validate asset thumbnail",
            [result, files, pairs, catalog, id, path = path.value()](NamedThread, const GraphEventRef&)
            {
                try
                {
                    const auto current = validate_thumbnail_source(
                        *pairs, *files, catalog, id, path, result->input.original, result->input.skeletal.get());
                    if (!current.succeeded())
                    {
                        result->error = current.message;
                    }
                }
                catch (const std::exception& error)
                {
                    result->error = error.what();
                }
            });
    }

    void AssetThumbnailPool::publish_texture(Entry& entry)
    {
        if (entry.texture.valid())
        {
            pending_work_.retire_textures.push_back(entry.texture);
        }
        entry.texture = entry.candidate_texture;
        entry.candidate_texture = {};
        finish(entry);
    }

    void AssetThumbnailPool::tick()
    {
        if (!initialized_)
        {
            return;
        }
        if (invalidation_pending_)
        {
            apply_invalidation();
        }
        ++frame_;
        if (material_preview_active_)
        {
            return;
        }
        if (cpu_task_ && cpu_task_->is_complete())
        {
            auto result = std::move(cpu_result_);
            const bool task_ok = cpu_task_->get_outcome() == TaskOutcome::Succeeded;
            cpu_task_.reset();
            auto it = entries_.find(active_id_);
            if (it != entries_.end())
            {
                Entry& entry = it->second;
                if (!task_ok && result->error.empty())
                {
                    result->error = "Thumbnail worker task failed.";
                }
                if (result->request_id != entry.request_id)
                {
                    fail(entry, "Thumbnail worker request does not match the active asset.");
                }
                else if (entry.rerun)
                {
                    if (result->saving || result->validating)
                    {
                        pending_work_.retire_textures.push_back(entry.candidate_texture);
                    }
                    finish(entry);
                }
                else if (!result->error.empty())
                {
                    fail(entry, std::move(result->error));
                }
                else if (!thumbnail_catalog_current(workspace_.catalog(), entry.id, entry.path,
                                                    result->input.skeletal.get()))
                {
                    fail(entry, "Thumbnail conflict: asset or dependency was moved or removed.");
                }
                else if (result->saving || result->validating)
                {
                    publish_texture(entry);
                }
                else
                {
                    if (!result->input.warning.empty())
                    {
                        TOY_LOG_WARN("Thumbnail {}: {}", entry.path, result->input.warning);
                    }
                    entry.source = result->input.source;
                    entry.original = std::move(result->input.original);
                    entry.skeletal = result->input.skeletal;
                    entry.stage = Stage::AwaitGpu;
                    if (!result->input.pixels.empty())
                    {
                        pending_work_.uploads.push_back({entry.request_id,
                                                         entry.candidate_texture,
                                                         {thumbnail_default_size, thumbnail_default_size},
                                                         std::move(result->input.pixels)});
                    }
                    else if (!preview_.configure_thumbnail() ||
                             !(result->input.skeletal && result->input.skeletal->mesh
                                   ? preview_.prepare_skeletal(*result->input.skeletal)
                                   : preview_.prepare(std::move(result->input.geometry))))
                    {
                        fail(entry, "Could not prepare the thumbnail preview mesh.");
                    }
                    else
                    {
                        pending_work_.preview = {entry.request_id,
                                                 entry.candidate_texture,
                                                 {thumbnail_default_size, thumbnail_default_size},
                                                 {preview_.view()}};
                    }
                }
            }
            else
            {
                active_id_ = {};
            }
        }
        if (active_id_.valid())
        {
            Entry& entry = entries_.at(active_id_);
            if (entry.stage == Stage::SaveQueued)
            {
                try
                {
                    start_save(entry);
                }
                catch (const std::exception& error)
                {
                    fail(entry, error.what());
                }
            }
            // A worker- or disk-bound entry does not own the single UI preview slot, so the
            // live preview may start while that entry is still in flight instead of waiting
            // out a multi-second asset load.
            if (active_id_.valid() && entries_.at(active_id_).stage != Stage::AwaitGpu)
            {
                start_material_preview();
            }
            return;
        }
        Entry* next = nullptr;
        for (auto& pair : entries_)
        {
            Entry& entry = pair.second;
            if (entry.stage != Stage::Queued || (!entry.persist && entry.last_visible_frame + 1 < frame_))
            {
                continue;
            }
            if (!next || (entry.persist && !next->persist) ||
                (entry.persist == next->persist && entry.last_visible_frame > next->last_visible_frame))
            {
                next = &entry;
            }
        }
        // Queued thumbnails are processed first (their images are already demanded by the
        // browser); the live preview shares a frame only when the entry in flight holds no
        // UI preview slot of its own.
        if (!next)
        {
            start_material_preview();
            return;
        }
        try
        {
            start_load(*next);
        }
        catch (const std::exception& error)
        {
            fail(*next, error.what());
        }
    }

    bool AssetThumbnailPool::start_material_preview()
    {
        if (!(material_preview_visible_frame_ + 2u >= frame_))
        {
            return false;
        }
        const auto material = preview_material_.lock();
        if (!material ||
            (rendered_preview_material_.lock() == material && rendered_preview_revision_ == material_preview_revision_))
        {
            return false;
        }
        if (next_request_ == std::numeric_limits<std::uint64_t>::max() || next_texture_ >= (1ull << 40))
        {
            material_preview_error_ = "Preview identifier space exhausted.";
            return false;
        }
        rendered_preview_material_ = material;
        pending_preview_revision_ = material_preview_revision_;
        TextureRef environment_cube;
        const auto environment_id = material_preview_settings_.scene.environment;
        if (environment_id.valid())
        {
            if (!assets_)
            {
                material_preview_error_ = "Preview environment: the asset loader is unavailable.";
                rendered_preview_revision_ = pending_preview_revision_;
                return false;
            }
            AssetRef reference;
            reference.asset_id = environment_id;
            reference.expected_type = "toy3d.EnvironmentAssetData";
            // The loader thread decodes the cube at the tier its asset type carries; the preview
            // starts on the frame that adopts it, so a busy thumbnail queue no longer delays it.
            const auto environment = request_texture(*assets_, reference, workspace_.catalog().index);
            if (environment.failed())
            {
                material_preview_error_ = "Preview environment: " + environment.error();
                rendered_preview_revision_ = pending_preview_revision_;
                return false;
            }
            if (!environment.ready())
            {
                return false;
            }
            environment_cube = environment.get();
        }
        const auto& geometry = material_preview_settings_.mesh == MaterialPreviewMesh::Plane ? material_preview_plane_
                               : material_preview_settings_.mesh == MaterialPreviewMesh::Cube
                                   ? material_preview_cube_
                                   : material_preview_geometry_;
        if (!preview_.prepare(geometry, material) ||
            !preview_.configure(material_preview_settings_.scene, environment_cube))
        {
            material_preview_error_ = "Could not prepare the material preview scene.";
            preview_.clear_mesh();
            rendered_preview_revision_ = pending_preview_revision_;
            return false;
        }
        material_preview_request_ = next_request_++;
        material_preview_candidate_ = ImGuiTextureId(next_texture_++);
        material_preview_active_ = true;
        material_preview_cancelled_ = false;
        pending_work_.preview = {material_preview_request_,
                                 material_preview_candidate_,
                                 material_preview_settings_.extent,
                                 {preview_.view(material_preview_settings_)},
                                 material_preview_settings_.scene.show_environment,
                                 material_preview_settings_.scene.show_shadows,
                                 material_preview_settings_.scene.exposure_ev};
        return true;
    }

    void AssetThumbnailPool::collect_render_work(UiRenderWork& work)
    {
        work = std::move(pending_work_);
        pending_work_ = {};
    }

    void AssetThumbnailPool::on_texture_result(UiTextureResult result)
    {
        if (material_preview_active_ && result.request_id == material_preview_request_ &&
            result.texture_id == material_preview_candidate_)
        {
            preview_.clear_mesh();
            material_preview_active_ = false;
            rendered_preview_revision_ = pending_preview_revision_;
            if (material_preview_cancelled_ || pending_preview_revision_ != material_preview_revision_ ||
                preview_material_.lock() != rendered_preview_material_.lock() || !result.succeeded())
            {
                pending_work_.retire_textures.push_back(result.texture_id);
                material_preview_error_ =
                    material_preview_cancelled_ || pending_preview_revision_ != material_preview_revision_
                        ? ""
                        : result.error;
            }
            else
            {
                if (material_preview_texture_.valid())
                {
                    pending_work_.retire_textures.push_back(material_preview_texture_);
                }
                material_preview_texture_ = result.texture_id;
                material_preview_error_.clear();
            }
            material_preview_candidate_ = {};
            return;
        }
        const auto it = entries_.find(active_id_);
        if (it == entries_.end() || it->second.request_id != result.request_id)
        {
            return;
        }
        Entry& entry = it->second;
        if (entry.stage != Stage::AwaitGpu || entry.candidate_texture != result.texture_id)
        {
            return;
        }
        preview_.clear_mesh();
        if (!result.succeeded())
        {
            fail(entry, std::move(result.error));
            return;
        }
        if (entry.rerun)
        {
            pending_work_.retire_textures.push_back(entry.candidate_texture);
            finish(entry);
            return;
        }
        if (!result.bgra_pixels.empty() &&
            (result.extent.width != thumbnail_default_size || result.extent.height != thumbnail_default_size ||
             result.bgra_pixels.size() !=
                 static_cast<std::size_t>(thumbnail_default_size) * thumbnail_default_size * 4))
        {
            fail(entry, "Thumbnail GPU readback dimensions are invalid.");
            return;
        }
        entry.pixels = std::move(result.bgra_pixels);
        if (entry.persist && !entry.pixels.empty())
        {
            // Validation and disk publication finish before replacing an existing image.
            entry.stage = Stage::SaveQueued;
        }
        else if (!entry.original.empty())
        {
            try
            {
                start_validate(entry);
            }
            catch (const std::exception& error)
            {
                fail(entry, error.what());
            }
        }
        else
        {
            publish_texture(entry);
        }
    }

    void AssetThumbnailPool::finish(Entry& entry)
    {
        // Drop allocation ownership, not only length: completed cache entries
        // must not retain a full model snapshot behind each small GPU image.
        entry.original = std::vector<std::uint8_t>{};
        entry.pixels = std::vector<std::uint8_t>{};
        entry.skeletal.reset();
        entry.error.clear();
        entry.candidate_texture = {};
        entry.stage = entry.rerun ? Stage::Queued : Stage::Ready;
        if (!entry.rerun)
        {
            entry.persist = false;
            entry.force = false;
        }
        else
        {
            entry.force = true;
        }
        entry.rerun = false;
        active_id_ = {};
    }

    void AssetThumbnailPool::fail(Entry& entry, std::string error)
    {
        TOY_LOG_ERROR("Thumbnail {}: {}", entry.path, error);
        if (entry.candidate_texture.valid())
        {
            pending_work_.retire_textures.push_back(entry.candidate_texture);
        }
        preview_.clear_mesh();
        finish(entry);
        entry.stage = Stage::Failed;
        entry.error = std::move(error);
        entry.persist = false;
    }

    std::vector<ImGuiTextureId> AssetThumbnailPool::texture_ids() const
    {
        std::vector<ImGuiTextureId> result;
        if (material_preview_texture_.valid())
        {
            result.push_back(material_preview_texture_);
        }
        for (const auto& pair : entries_)
        {
            if (pair.second.texture.valid())
            {
                result.push_back(pair.second.texture);
            }
        }
        return result;
    }

    void AssetThumbnailPool::shutdown()
    {
        if (cpu_task_ && tasks_)
        {
            const auto waited = tasks_->wait_until_task_completes(cpu_task_, NamedThread::GameThread);
            if (!waited.succeeded())
            {
                TOY_LOG_ERROR("Thumbnail worker could not finish during shutdown.");
            }
        }
        cpu_task_.reset();
        cpu_result_.reset();
        clear_material_preview();
        material_preview_active_ = false;
        material_preview_geometry_ = {};
        material_preview_plane_ = {};
        material_preview_cube_ = {};
        // The shared loader owns decoded cubes and outlives this pool.
        if (initialized_)
        {
            preview_.shutdown();
        }
        assets_ = nullptr;
        entries_.clear();
        pending_work_ = {};
        active_id_ = {};
        initialized_ = false;
        invalidation_pending_ = false;
        full_invalidation_ = false;
        pending_invalidations_.clear();
        tasks_ = nullptr;
    }
} // namespace toy3d
