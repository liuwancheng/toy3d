#include "thumbnails/asset_thumbnail_pool.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

#include "image_codec/png_codec.h"
#include "logging/logger.h"
#include "task_graph/graph_task.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    namespace
    {
        constexpr std::size_t cache_capacity = 128;
        constexpr std::size_t asset_byte_limit = 64u * 1024u * 1024u;

        bool same_source(const AssetThumbnailSource& a, const AssetThumbnailSource& b)
        {
            return a.preview_version == b.preview_version && a.content_hash == b.content_hash;
        }

        bool project_path(const std::string& path)
        {
            return path.compare(0, 9, "/Project/") == 0;
        }

        const AssetCatalogEntry* find_asset(const EditorWorkspace& workspace, const AssetId& id)
        {
            for (const auto& asset : workspace.catalog().entries)
                if (asset.file.asset_id == id) return &asset;
            return nullptr;
        }
    }

    struct AssetThumbnailPool::CpuResult
    {
        std::uint64_t request_id = 0;
        bool saving = false;
        std::string error;
        std::string warning;
        AssetThumbnailSource source;
        StaticMeshAssetGeometry geometry;
        Extent extent;
        std::vector<std::uint8_t> pixels;
        std::vector<std::uint8_t> original;
        std::vector<std::uint8_t> candidate;
    };

    AssetThumbnailPool::AssetThumbnailPool(EditorWorkspace& workspace) : workspace_(workspace) {}
    AssetThumbnailPool::~AssetThumbnailPool() { shutdown(); }

    bool AssetThumbnailPool::initialize(SceneInterface& scene, MaterialInstanceRef material, TaskGraphInterface& tasks)
    {
        tasks_ = &tasks;
        initialized_ = preview_.initialize(scene, std::move(material));
        if (!initialized_) preview_.shutdown();
        return initialized_;
    }

    bool AssetThumbnailPool::make_room()
    {
        if (entries_.size() < cache_capacity) return true;
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
        {
            if (it->first == active_id_ || it->second.persist || it->second.last_visible_frame == frame_) continue;
            if (oldest == entries_.end() || it->second.last_visible_frame < oldest->second.last_visible_frame) oldest = it;
        }
        if (oldest == entries_.end()) return false;
        if (oldest->second.texture.valid()) pending_work_.retire_textures.push_back(oldest->second.texture);
        entries_.erase(oldest);
        return true;
    }

    AssetThumbnailView AssetThumbnailPool::request(const AssetCatalogEntry& asset)
    {
        if (!initialized_ || asset.file.root_type != "toy3d.StaticMeshAssetData") return {};
        auto it = entries_.find(asset.file.asset_id);
        if (it == entries_.end())
        {
            if (!make_room()) return {{}, true, {}};
            Entry entry;
            entry.id = asset.file.asset_id;
            entry.path = asset.path.utf8();
            it = entries_.emplace(entry.id, std::move(entry)).first;
        }
        Entry& entry = it->second;
        entry.last_visible_frame = frame_;
        return {entry.texture, entry.stage != Stage::Ready && entry.stage != Stage::Failed, entry.error};
    }

    void AssetThumbnailPool::generate(const AssetId& id, bool save)
    {
        const auto* asset = find_asset(workspace_, id);
        if (!asset || asset->file.root_type != "toy3d.StaticMeshAssetData") return;
        request(*asset);
        const auto it = entries_.find(id);
        if (it == entries_.end()) { TOY_LOG_WARN("Thumbnail cache is busy; retry generation."); return; }
        Entry& entry = it->second;
        entry.persist = entry.persist || (save && project_path(entry.path));
        if (save && !project_path(entry.path)) TOY_LOG_WARN("Engine thumbnails are generated in memory only.");
        if (entry.id == active_id_) { entry.rerun = true; return; }
        entry.force = true;
        entry.stage = Stage::Queued;
        entry.error.clear();
    }

    void AssetThumbnailPool::invalidate()
    {
        for (auto& pair : entries_)
        {
            Entry& entry = pair.second;
            if (entry.id == active_id_) { entry.rerun = true; entry.persist = false; continue; }
            if (entry.texture.valid()) pending_work_.retire_textures.push_back(entry.texture);
            entry.texture = {};
            entry.stage = Stage::Queued;
            entry.error.clear();
        }
    }

    void AssetThumbnailPool::start_load(Entry& entry)
    {
        const auto* asset = find_asset(workspace_, entry.id);
        if (!asset) { fail(entry, "Asset was removed from the catalog."); return; }
        entry.path = asset->path.utf8();
        if (next_request_ == std::numeric_limits<std::uint64_t>::max() ||
            next_texture_ == std::numeric_limits<std::uint64_t>::max())
        { fail(entry, "Thumbnail identifier space exhausted."); return; }
        entry.request_id = next_request_++;
        entry.candidate_texture = ImGuiTextureId(next_texture_++);
        entry.stage = Stage::Loading;
        active_id_ = entry.id;
        auto result = std::make_shared<CpuResult>();
        result->request_id = entry.request_id;
        cpu_result_ = result;
        const auto path = asset->path;
        const AssetId id = entry.id;
        const bool force = entry.force || entry.persist;
        FileSystem* files = &workspace_.files();
        cpu_task_ = dispatch_graph_task(*tasks_, "Load asset thumbnail",
            [result, files, path, id, force](NamedThread, const GraphEventRef&)
            {
                try
                {
                    const auto index = inspect_asset(*files, path);
                    if (!index.succeeded()) { result->error = index.status().message; return; }
                    if (!(index.value().asset_id == id) || index.value().root_type != "toy3d.StaticMeshAssetData")
                    { result->error = "Asset identity/type changed during thumbnail load."; return; }
                    const AssetSegment* source_segment = nullptr;
                    const AssetSegment* thumbnail_segment = nullptr;
                    for (const auto& segment : index.value().segments)
                    {
                        if (segment.name == "thumbnail_source" && segment.kind == 2 && !segment.required) source_segment = &segment;
                        if (segment.name == "thumbnail" && segment.kind == 2 && !segment.required) thumbnail_segment = &segment;
                    }
                    if (!force && source_segment && thumbnail_segment)
                    {
                        const auto source_bytes = read_asset_segment(*files, path, id, *source_segment, 1024);
                        const auto image_bytes = read_asset_segment(*files, path, id, *thumbnail_segment, thumbnail_max_bytes + 1024);
                        if (source_bytes.succeeded() && image_bytes.succeeded())
                        {
                            const auto source = decode_thumbnail_source(source_bytes.value());
                            const auto thumbnail = decode_asset_thumbnail(image_bytes.value());
                            if (source.succeeded() && thumbnail.succeeded() && same_source(source.value(), thumbnail.value().source) &&
                                thumbnail.value().generator_version == thumbnail_generator_version)
                            {
                                Rgba8Image decoded;
                                const auto status = decode_png(thumbnail.value().png, decoded);
                                if (status.succeeded() && decoded.width == thumbnail.value().width && decoded.height == thumbnail.value().height)
                                {
                                    result->source = source.value();
                                    result->extent = {decoded.width, decoded.height};
                                    result->pixels = std::move(decoded.pixels);
                                    for (std::size_t i = 0; i < result->pixels.size(); i += 4)
                                    {
                                        std::swap(result->pixels[i], result->pixels[i + 2]);
                                        result->pixels[i + 3] = 255;
                                    }
                                    return;
                                }
                            }
                        }
                        result->warning = "Cached thumbnail is invalid or stale; generating a replacement in memory.";
                    }
                    const auto bytes = files->read_binary(path, asset_byte_limit);
                    if (!bytes.succeeded()) { result->error = bytes.status().message; return; }
                    const auto source = calculate_static_mesh_thumbnail_source(bytes.value());
                    if (!source.succeeded()) { result->error = source.status().message; return; }
                    const auto geometry = decode_static_mesh_asset(bytes.value());
                    if (!geometry.succeeded()) { result->error = geometry.status().message; return; }
                    const auto original_index = inspect_asset_bytes(bytes.value());
                    if (!original_index.succeeded() || !(original_index.value().asset_id == id))
                    { result->error = "Asset identity changed during thumbnail generation."; return; }
                    result->source = source.value();
                    result->geometry = geometry.value();
                    result->original = bytes.value();
                    result->extent = {thumbnail_default_size, thumbnail_default_size};
                }
                catch (const std::exception& error) { result->error = error.what(); }
            });
    }

    void AssetThumbnailPool::start_save(Entry& entry)
    {
        entry.stage = Stage::Saving;
        auto result = std::make_shared<CpuResult>();
        result->request_id = entry.request_id;
        result->saving = true;
        result->original = std::move(entry.original);
        result->pixels = std::move(entry.pixels);
        result->source = entry.source;
        cpu_result_ = result;
        cpu_task_ = dispatch_graph_task(*tasks_, "Encode asset thumbnail",
            [result](NamedThread, const GraphEventRef&)
            {
                try
                {
                    Rgba8Image image;
                    image.width = image.height = thumbnail_default_size;
                    image.pixels = std::move(result->pixels);
                    for (std::size_t i = 0; i < image.pixels.size(); i += 4)
                    {
                        std::swap(image.pixels[i], image.pixels[i + 2]);
                        image.pixels[i + 3] = 255;
                    }
                    AssetThumbnailData thumbnail;
                    thumbnail.source = result->source;
                    thumbnail.width = image.width;
                    thumbnail.height = image.height;
                    const auto encoded = encode_png(image, thumbnail.png);
                    if (!encoded.succeeded()) { result->error = encoded.message; return; }
                    const auto source_segment = encode_thumbnail_source(result->source);
                    const auto image_segment = encode_asset_thumbnail(thumbnail);
                    if (!source_segment.succeeded() || !image_segment.succeeded())
                    { result->error = "Thumbnail segment encoding failed."; return; }
                    const auto replaced = replace_asset_segments(result->original, {source_segment.value(), image_segment.value()});
                    if (!replaced.succeeded()) { result->error = replaced.status().message; return; }
                    result->candidate = replaced.value();
                }
                catch (const std::exception& error) { result->error = error.what(); }
            });
    }

    void AssetThumbnailPool::tick()
    {
        if (!initialized_) return;
        ++frame_;
        if (cpu_task_ && cpu_task_->is_complete())
        {
            auto result = std::move(cpu_result_);
            const bool task_ok = cpu_task_->get_outcome() == TaskOutcome::Succeeded;
            cpu_task_.reset();
            auto it = entries_.find(active_id_);
            if (it != entries_.end())
            {
                Entry& entry = it->second;
                if (!task_ok && result->error.empty()) result->error = "Thumbnail worker task failed.";
                if (!result->error.empty()) fail(entry, std::move(result->error));
                else if (entry.rerun) finish(entry);
                else if (result->saving)
                {
                    const auto* asset = find_asset(workspace_, entry.id);
                    const auto path = VirtualPath::parse(entry.path);
                    if (!asset || asset->path.utf8() != entry.path || !path.succeeded() || !project_path(entry.path))
                        fail(entry, "Thumbnail save rejected: asset was moved or removed.");
                    else
                    {
                        const auto current = workspace_.files().read_binary(path.value(), asset_byte_limit);
                        if (!current.succeeded()) fail(entry, current.status().message);
                        else if (sha256(current.value()) != sha256(result->original))
                            fail(entry, "Thumbnail save conflict: asset changed; refresh and regenerate.");
                        else
                        {
                            const auto saved = workspace_.files().write_binary_atomic(path.value(), result->candidate, FilePublishMode::Replace);
                            if (!saved.succeeded()) fail(entry, saved.message);
                            else
                            {
                                TOY_LOG_INFO("Saved asset thumbnail: {}", entry.path);
                                if (!workspace_.refresh()) TOY_LOG_WARN("Thumbnail saved, but asset catalog refresh failed: {}", workspace_.error());
                                finish(entry);
                            }
                        }
                    }
                }
                else
                {
                    if (!result->warning.empty()) TOY_LOG_WARN("Thumbnail {}: {}", entry.path, result->warning);
                    entry.source = result->source;
                    entry.original = std::move(result->original);
                    entry.stage = Stage::AwaitGpu;
                    if (!result->pixels.empty())
                        pending_work_.uploads.push_back({entry.request_id, entry.candidate_texture, result->extent, std::move(result->pixels)});
                    else if (!preview_.prepare(std::move(result->geometry))) fail(entry, "Could not prepare the thumbnail preview mesh.");
                    else
                    {
                        pending_work_.preview = {entry.request_id, entry.candidate_texture, result->extent, {preview_.view()}};
                    }
                }
            }
            else active_id_ = {};
        }
        if (active_id_.valid())
        {
            Entry& entry = entries_.at(active_id_);
            if (entry.stage == Stage::SaveQueued)
            {
                try { start_save(entry); }
                catch (const std::exception& error) { fail(entry, error.what()); }
            }
            return;
        }
        Entry* next = nullptr;
        for (auto& pair : entries_)
        {
            Entry& entry = pair.second;
            if (entry.stage != Stage::Queued || (!entry.persist && entry.last_visible_frame + 1 < frame_)) continue;
            if (!next || (entry.persist && !next->persist) ||
                (entry.persist == next->persist && entry.last_visible_frame > next->last_visible_frame)) next = &entry;
        }
        if (next)
        {
            try { start_load(*next); }
            catch (const std::exception& error) { fail(*next, error.what()); }
        }
    }

    void AssetThumbnailPool::collect_render_work(UiRenderWork& work)
    {
        work = std::move(pending_work_);
        pending_work_ = {};
    }

    void AssetThumbnailPool::on_texture_result(UiTextureResult result)
    {
        const auto it = entries_.find(active_id_);
        if (it == entries_.end() || it->second.request_id != result.request_id) return;
        Entry& entry = it->second;
        if (entry.stage != Stage::AwaitGpu || entry.candidate_texture != result.texture_id) return;
        preview_.clear_mesh();
        if (!result.succeeded()) { fail(entry, std::move(result.error)); return; }
        if (entry.rerun)
        {
            pending_work_.retire_textures.push_back(entry.candidate_texture);
            finish(entry);
            return;
        }
        if (entry.texture.valid()) pending_work_.retire_textures.push_back(entry.texture);
        entry.texture = entry.candidate_texture;
        entry.candidate_texture = {};
        if (entry.persist && !result.bgra_pixels.empty())
        {
            if (result.extent.width != thumbnail_default_size || result.extent.height != thumbnail_default_size ||
                result.bgra_pixels.size() != static_cast<std::size_t>(thumbnail_default_size) * thumbnail_default_size * 4)
            { fail(entry, "Thumbnail GPU readback dimensions are invalid."); return; }
            entry.pixels = std::move(result.bgra_pixels);
            entry.stage = Stage::SaveQueued;
        }
        else finish(entry);
    }

    void AssetThumbnailPool::finish(Entry& entry)
    {
        // Drop allocation ownership, not only length: completed cache entries
        // must not retain a full model snapshot behind each small GPU image.
        entry.original = std::vector<std::uint8_t>{};
        entry.pixels = std::vector<std::uint8_t>{};
        entry.error.clear();
        entry.candidate_texture = {};
        entry.stage = entry.rerun ? Stage::Queued : Stage::Ready;
        if (!entry.rerun) { entry.persist = false; entry.force = false; }
        else entry.force = true;
        entry.rerun = false;
        active_id_ = {};
    }

    void AssetThumbnailPool::fail(Entry& entry, std::string error)
    {
        TOY_LOG_ERROR("Thumbnail {}: {}", entry.path, error);
        if (entry.candidate_texture.valid()) pending_work_.retire_textures.push_back(entry.candidate_texture);
        preview_.clear_mesh();
        finish(entry);
        entry.stage = Stage::Failed;
        entry.error = std::move(error);
        entry.persist = false;
    }

    std::vector<ImGuiTextureId> AssetThumbnailPool::texture_ids() const
    {
        std::vector<ImGuiTextureId> result;
        for (const auto& pair : entries_)
            if (pair.second.texture.valid()) result.push_back(pair.second.texture);
        return result;
    }

    void AssetThumbnailPool::shutdown()
    {
        if (cpu_task_ && tasks_)
        {
            const auto waited = tasks_->wait_until_task_completes(cpu_task_, NamedThread::GameThread);
            if (!waited.succeeded()) TOY_LOG_ERROR("Thumbnail worker could not finish during shutdown.");
        }
        cpu_task_.reset();
        cpu_result_.reset();
        if (initialized_) preview_.shutdown();
        entries_.clear();
        pending_work_ = {};
        active_id_ = {};
        initialized_ = false;
        tasks_ = nullptr;
    }
}
