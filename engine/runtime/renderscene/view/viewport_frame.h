#pragma once

#include "rendercore/render_id.h"
#include "renderscene/view/scene_view.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    struct ImGuiDrawPacket;

    enum class SceneOutputType
    {
        Present,
        Offscreen
    };

    struct SceneOutputExtent
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    struct SceneOutput
    {
        SceneOutputId output_id;
        SceneOutputType type = SceneOutputType::Present;
        SceneOutputExtent extent;
    };

    struct SceneViewFamilyFrame
    {
        SceneViewFamily view_family;
        SceneOutput output;
    };

    struct ViewportFrame
    {
        ViewportId viewport_id;
        std::vector<SceneViewFamilyFrame> scene_frames;
        std::shared_ptr<const ImGuiDrawPacket> imgui;
    };

    enum class ViewportFrameValidation
    {
        Valid,
        InvalidArgument,
        Unsupported
    };

    ViewportFrameValidation validate_viewport_frame(
        const ViewportFrame& frame,
        std::string& diagnostic);

    ViewportFrameValidation validate_viewport_frames(
        const std::vector<ViewportFrame>& frames,
        std::string& diagnostic);
}
