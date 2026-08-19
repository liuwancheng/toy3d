#pragma once

#include "rendercore/render_id.h"
#include "rendercore/render_resource_update.h"
#include "rendercore/render_scene_update.h"
#include "renderscene/render_frame_completion.h"
#include "renderscene/view/scene_view.h"

#include <vector>

namespace toy3d
{
    struct FrameTiming
    {
        double delta_seconds = 0.0;
        double total_seconds = 0.0;
    };

    struct RenderFramePacket
    {
        RenderFramePacket() = default;

        RenderFramePacket(const RenderFramePacket&) = delete;
        RenderFramePacket& operator=(const RenderFramePacket&) = delete;
        RenderFramePacket(RenderFramePacket&&) = default;
        RenderFramePacket& operator=(RenderFramePacket&&) = default;

        RenderFrameId frame_id;
        FrameTiming timing;
        std::vector<RenderResourceUpdate> resource_updates;
        std::vector<RenderSceneUpdateBatch> scene_updates;
        std::vector<SceneViewFamily> view_families;
        RenderFrameCompletionRef completion;
    };
}
