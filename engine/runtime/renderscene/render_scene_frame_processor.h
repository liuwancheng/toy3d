#pragma once

#include "renderscene/render_frame_dispatcher.h"
#include "renderscene/resources/render_resource_cache.h"
#include "renderscene/scene/render_scene.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace toy3d
{
    struct RenderSceneFrameApplyResult
    {
        RenderSceneId scene_id;
        RenderSceneApplyResult result;
    };

    struct RenderSceneFrameReport
    {
        RenderFrameId frame_id;
        RenderResourceApplyResult resource_result;
        std::vector<RenderSceneFrameApplyResult> scene_results;
        ViewportFrameValidation viewport_validation =
            ViewportFrameValidation::Valid;
        std::string viewport_diagnostic;

        bool has_diagnostics() const;
    };

    class RenderSceneFrameProcessor final : public RenderFrameProcessor
    {
    public:
        explicit RenderSceneFrameProcessor(
            RenderResourcePlaceholders placeholders);

        RenderFrameExecutionStatus initialize() override;
        RenderFrameExecutionStatus process_frame(
            const RenderFramePacket& packet) override;
        RenderFrameExecutionStatus flush() override;
        RenderFrameExecutionStatus shutdown() override;

        // These views remain on the rendering execution thread selected by the
        // dispatcher; they do not make processor state cross-thread readable.
        const RenderResourceCache& resource_cache() const { return resource_cache_; }
        const RenderScene* find_scene(RenderSceneId scene_id) const;
        const RenderSceneFrameReport& last_report() const { return last_report_; }

    private:
        enum class State
        {
            Created,
            Initialized,
            Stopped
        };

        State state_ = State::Created;
        RenderResourceCache resource_cache_;
        std::unordered_map<std::uint64_t, RenderScene> scenes_;
        RenderSceneFrameReport last_report_;
    };
}
