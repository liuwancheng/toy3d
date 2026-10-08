#pragma once

#include "assets/preview/mesh_preview_asset.h"

#include "asset/thumbnail/asset_thumbnail.h"
#include "asset_loader/asset_loader.h"
#include "assets/preview/asset_preview_scene.h"
#include "threading/task_graph/task_graph_interface.h"
#include "ui/ui_texture_work.h"

#include <map>
#include <functional>
#include <memory>
#include <string>

namespace toy3d
{
    class EditorWorkspace;
    struct AssetCatalogEntry;

    struct AssetThumbnailView
    {
        ImGuiTextureId texture_id;
        bool busy = false;
        std::string error;
    };

    // Editor policy: bounded visible-item cache, one CPU/GPU job in flight,
    // worker-owned disk validation/publication before GT image adoption.
    class AssetThumbnailPool final
    {
      public:
        explicit AssetThumbnailPool(EditorWorkspace& workspace);
        ~AssetThumbnailPool();
        bool initialize(SceneInterface& scene, MaterialInstanceRef material, TaskGraphInterface& tasks);
        // GT composition root injects the shared asset loader before initialize().
        void set_asset_loader(AssetLoader& assets)
        {
            assets_ = &assets;
        }
        AssetThumbnailView request(const AssetCatalogEntry& asset);
        AssetThumbnailView request_builtin_mesh(const std::string& kind, StaticMeshRef geometry);
        AssetThumbnailView request_material_preview(const MaterialInstanceRef& material, std::uint64_t revision,
                                                    const MaterialPreviewSettings& settings = {});
        void clear_material_preview();
        // GT composition root supplies the existing placement prototypes; the Pool stores CPU copies only.
        void set_material_preview_meshes(const StaticMeshRef& plane, const StaticMeshRef& cube);
        void set_material_resolver(std::function<AssetResult<MaterialInterfaceRef>(const AssetRef&)> resolver)
        {
            material_resolver_ = std::move(resolver);
            preview_.set_material_resolver(material_resolver_);
        }
        void generate(const AssetId& id);
        // Full invalidation for catalog-wide changes (rescan, project switch).
        void invalidate();
        // Invalidates one identity and its strong dependency closure; unrelated cached
        // images stay usable, so saving an asset no longer requeues the whole cache.
        // Unlike the full path this leaves the live preview revision alone; only a changed
        // Environment re-renders it.
        void invalidate(const AssetId& changed);
        void tick();
        void collect_render_work(UiRenderWork& work);
        void on_texture_result(UiTextureResult result);
        std::vector<ImGuiTextureId> texture_ids() const;
        void shutdown();

      private:
        enum class Stage
        {
            Queued,
            Loading,
            AwaitGpu,
            Validating,
            SaveQueued,
            Saving,
            Ready,
            Failed
        };
        struct Entry
        {
            AssetId id;
            std::string path;
            Stage stage = Stage::Queued;
            ImGuiTextureId texture;
            ImGuiTextureId candidate_texture;
            std::uint64_t request_id = 0;
            std::uint64_t last_visible_frame = 0;
            bool persist = false;
            bool force = false;
            bool rerun = false;
            std::string error;
            AssetThumbnailSource source;
            std::vector<std::uint8_t> original;
            std::vector<std::uint8_t> pixels;
            std::shared_ptr<const MeshPreviewAsset> skeletal;
            StaticMeshRef builtin_geometry;
        };
        struct CpuResult;
        void start_load(Entry& entry);
        void start_save(Entry& entry);
        void start_validate(Entry& entry);
        void publish_texture(Entry& entry);
        void fail(Entry& entry, std::string error);
        void finish(Entry& entry);
        bool make_room();
        void apply_invalidation();
        // Submits the live material preview frame when one is due; reports whether this
        // frame's single UI preview slot was taken.
        bool start_material_preview();

        EditorWorkspace& workspace_;
        AssetPreviewScene preview_;
        AssetLoader* assets_ = nullptr;
        MaterialPreviewSettings material_preview_settings_;
        std::uint64_t material_source_revision_ = 0u;
        StaticMeshAssetGeometry material_preview_geometry_;
        StaticMeshAssetGeometry material_preview_plane_;
        StaticMeshAssetGeometry material_preview_cube_;
        std::function<AssetResult<MaterialInterfaceRef>(const AssetRef&)> material_resolver_;
        // Observing the asset-editor owner avoids extending its final-release lifetime.
        std::weak_ptr<MaterialInstance> preview_material_;
        std::weak_ptr<MaterialInstance> rendered_preview_material_;
        ImGuiTextureId material_preview_texture_;
        ImGuiTextureId material_preview_candidate_;
        std::uint64_t material_preview_revision_ = 0u;
        std::uint64_t rendered_preview_revision_ = 0u;
        std::uint64_t pending_preview_revision_ = 0u;
        std::uint64_t material_preview_request_ = 0u;
        std::uint64_t material_preview_visible_frame_ = 0u;
        std::string material_preview_error_;
        bool material_preview_active_ = false;
        bool material_preview_cancelled_ = false;
        TaskGraphInterface* tasks_ = nullptr;
        GraphEventRef cpu_task_;
        std::shared_ptr<CpuResult> cpu_result_;
        std::map<AssetId, Entry> entries_;
        UiRenderWork pending_work_;
        AssetId active_id_;
        std::uint64_t frame_ = 0;
        std::uint64_t next_request_ = 1;
        std::uint64_t next_texture_ = 3;
        bool initialized_ = false;
        bool invalidation_pending_ = false;
        // Full rescans clear the whole cache; targeted callbacks queue identities that
        // are resolved to their strong dependency closure on the next pre-UI tick.
        bool full_invalidation_ = false;
        std::vector<AssetId> pending_invalidations_;
    };
} // namespace toy3d
