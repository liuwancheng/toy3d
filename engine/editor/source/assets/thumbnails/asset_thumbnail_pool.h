#pragma once

#include "assets/animation/animation_preview_asset.h"

#include "asset/thumbnail/asset_thumbnail.h"
#include "assets/thumbnails/thumbnail_preview_scene.h"
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
    // explicit persistence for existing assets, automatic persistence on import.
    class AssetThumbnailPool final
    {
      public:
        explicit AssetThumbnailPool(EditorWorkspace& workspace);
        ~AssetThumbnailPool();
        bool initialize(SceneInterface& scene, MaterialInstanceRef material, TaskGraphInterface& tasks);
        AssetThumbnailView request(const AssetCatalogEntry& asset);
        AssetThumbnailView request_material_preview(const MaterialInstanceRef& material, std::uint64_t revision,
                                                    const MaterialPreviewSettings& settings = {});
        void clear_material_preview();
        void set_material_resolver(std::function<AssetResult<MaterialInterfaceRef>(const AssetRef&)> resolver)
        {
            material_resolver_ = std::move(resolver);
        }
        void generate(const AssetId& id);
        void invalidate();
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
            std::shared_ptr<const AnimationPreviewAsset> skeletal;
        };
        struct CpuResult;
        void start_load(Entry& entry);
        void start_save(Entry& entry);
        void fail(Entry& entry, std::string error);
        void finish(Entry& entry);
        bool make_room();

        EditorWorkspace& workspace_;
        ThumbnailPreviewScene preview_;
        std::map<AssetId, TextureRef> preview_environments_;
        MaterialPreviewSettings material_preview_settings_;
        std::uint64_t material_source_revision_ = 0u;
        StaticMeshAssetGeometry material_preview_geometry_;
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
    };
} // namespace toy3d
