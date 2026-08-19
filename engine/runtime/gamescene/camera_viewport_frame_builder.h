#pragma once

#include "renderscene/view/viewport_frame.h"

#include <string>

namespace toy3d
{
    class CameraComponent;

    struct CameraViewportFrameDesc
    {
        ViewportId viewport_id;
        SceneOutput output;
        SceneViewRect view_rect;
    };

    ViewportFrameValidation build_camera_viewport_frame(
        const CameraComponent& camera,
        const CameraViewportFrameDesc& desc,
        ViewportFrame& result,
        std::string& diagnostic);
}
