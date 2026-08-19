#pragma once

#include "renderscene/output/scene_output.h"
#include "renderscene/view/scene_view.h"

#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    struct ImGuiDrawPacket;

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
