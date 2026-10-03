#pragma once

#include "ui/imgui_draw_data.h"
#include "rendercore/view/scene_view.h"

namespace toy3d
{
    struct UiTextureUpload
    {
        std::uint64_t request_id = 0;
        ImGuiTextureId texture_id;
        Extent extent;
        std::vector<std::uint8_t> bgra_pixels;
    };

    struct PreviewFrameRequest
    {
        std::uint64_t request_id = 0;
        ImGuiTextureId texture_id;
        Extent extent;
        std::vector<SceneView> views;
        bool show_environment = false;
        bool render_shadows = false;
        float exposure_ev = 0.0f;
        std::vector<DebugLineVertex> debug_lines;
        bool debug_lines_depth_test = false;
    };

    struct UiRenderWork
    {
        std::vector<UiTextureUpload> uploads;
        std::vector<ImGuiTextureId> retire_textures;
        PreviewFrameRequest preview;
        PreviewFrameRequest animation_preview;
        bool release_animation_preview = false;
    };

    struct UiTextureResult
    {
        std::uint64_t request_id = 0;
        ImGuiTextureId texture_id;
        Extent extent;
        std::vector<std::uint8_t> bgra_pixels;
        std::string error;
        bool succeeded() const
        {
            return error.empty();
        }
    };
} // namespace toy3d
