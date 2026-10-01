#pragma once

#include "asset_identity.h"
#include "task_graph/task_graph_interface.h"
#include "texture_asset/texture_asset.h"
#include "panels/texture_preview_image.h"
#include "ui/ui_texture_work.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;

    // A read-only editor view. Asset bytes stay on the CPU; the renderer owns
    // uploaded images and this panel keeps only their logical UI identity.
    class TexturePreviewPanel final
    {
    public:
        explicit TexturePreviewPanel(EditorWorkspace& workspace);
        ~TexturePreviewPanel();
        void request_open(const AssetId& id, bool focus = true);
        void invalidate();
        void tick();
        void draw();
        void collect_render_work(UiRenderWork& work);
        void on_texture_result(UiTextureResult result);
        std::vector<ImGuiTextureId> texture_ids() const;
        const AssetId& asset_id() const { return asset_id_; }
        const std::string& error() const { return error_; }
        void shutdown();

    private:
        struct CpuResult;
        void set_channel(TexturePreviewChannel channel);
        void set_mip(std::uint32_t mip);
        void close();
        void reject_preview(std::string error);

        EditorWorkspace& workspace_;
        AssetId asset_id_;
        std::string path_;
        std::string error_;
        std::shared_ptr<const Texture2DAsset> asset_;
        std::shared_ptr<CpuResult> cpu_result_;
        // Decoded candidates stay separate from the displayed asset until GPU upload succeeds.
        std::shared_ptr<CpuResult> candidate_result_;
        GraphEventRef cpu_task_;
        UiRenderWork pending_work_;
        ImGuiTextureId texture_id_;
        ImGuiTextureId candidate_id_;
        Extent image_extent_;
        std::uint64_t revision_ = 0;
        std::uint64_t candidate_revision_ = 0;
        std::uint64_t next_request_ = 1;
        // Remains representable as a conventional 64-bit user-space pointer
        // when ImGui encodes logical image IDs in ImTextureID.
        std::uint64_t next_texture_ = 1ull << 40;
        std::uint32_t mip_ = 0;
        TexturePreviewChannel channel_ = TexturePreviewChannel::RGBA;
        AssetId requested_asset_id_;
        std::uint32_t requested_mip_ = 0;
        TexturePreviewChannel requested_channel_ = TexturePreviewChannel::RGBA;
        bool reload_requested_ = false;
        float zoom_ = 0.0f;
        float pan_x_ = 0.0f;
        float pan_y_ = 0.0f;
        bool open_ = false;
        bool focus_requested_ = false;
        bool needs_prepare_ = false;
    };
}
