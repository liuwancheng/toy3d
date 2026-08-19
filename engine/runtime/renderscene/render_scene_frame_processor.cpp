#include "renderscene/render_scene_frame_processor.h"

#include <utility>

namespace toy3d
{
    bool RenderSceneFrameReport::has_diagnostics() const
    {
        if (!resource_result.succeeded())
        {
            return true;
        }
        for (const RenderSceneFrameApplyResult& scene_result : scene_results)
        {
            if (!scene_result.result.succeeded())
            {
                return true;
            }
        }
        return viewport_validation != ViewportFrameValidation::Valid;
    }

    RenderSceneFrameProcessor::RenderSceneFrameProcessor(
        RenderResourcePlaceholders placeholders)
        : resource_cache_(std::move(placeholders))
    {
    }

    RenderFrameExecutionStatus RenderSceneFrameProcessor::initialize()
    {
        if (state_ != State::Created)
        {
            return RenderFrameExecutionStatus::fatal_failure(
                "RenderSceneFrameProcessor can only be initialized once.");
        }
        if (!resource_cache_.is_valid())
        {
            state_ = State::Stopped;
            return RenderFrameExecutionStatus::fatal_failure(
                "RenderSceneFrameProcessor requires valid resource placeholders.");
        }
        state_ = State::Initialized;
        return RenderFrameExecutionStatus::success();
    }

    RenderFrameExecutionStatus RenderSceneFrameProcessor::process_frame(
        const RenderFramePacket& packet)
    {
        if (state_ != State::Initialized)
        {
            return RenderFrameExecutionStatus::fatal_failure(
                "RenderSceneFrameProcessor cannot process a frame outside its initialized lifetime.");
        }

        last_report_ = {};
        last_report_.frame_id = packet.frame_id;
        last_report_.resource_result =
            resource_cache_.apply_updates(packet.resource_updates);

        last_report_.scene_results.reserve(packet.scene_updates.size());
        for (const RenderSceneUpdateBatch& batch : packet.scene_updates)
        {
            RenderSceneFrameApplyResult scene_result;
            scene_result.scene_id = batch.scene_id;
            if (!batch.scene_id)
            {
                RenderScene invalid_scene(RenderSceneId{});
                scene_result.result = invalid_scene.apply_updates(batch);
                last_report_.scene_results.push_back(std::move(scene_result));
                continue;
            }

            auto iterator = scenes_.find(batch.scene_id.value());
            if (iterator == scenes_.end())
            {
                iterator = scenes_.emplace(
                    batch.scene_id.value(), RenderScene(batch.scene_id)).first;
            }
            scene_result.result = iterator->second.apply_updates(batch);
            last_report_.scene_results.push_back(std::move(scene_result));
        }

        last_report_.viewport_validation = validate_viewport_frames(
            packet.viewport_frames,
            last_report_.viewport_diagnostic);
        if (last_report_.viewport_validation !=
            ViewportFrameValidation::Valid)
        {
            return RenderFrameExecutionStatus::frame_failure(
                last_report_.viewport_diagnostic);
        }
        for (const ViewportFrame& viewport_frame : packet.viewport_frames)
        {
            for (const SceneViewFamilyFrame& scene_frame :
                viewport_frame.scene_frames)
            {
                if (scene_frame.output.extent.width == 0 &&
                    scene_frame.output.extent.height == 0)
                {
                    continue;
                }
                if (scenes_.find(scene_frame.view_family.scene_id.value()) ==
                    scenes_.end())
                {
                    last_report_.viewport_validation =
                        ViewportFrameValidation::InvalidArgument;
                    last_report_.viewport_diagnostic =
                        "A non-zero SceneOutput references an unknown RenderSceneId.";
                    return RenderFrameExecutionStatus::frame_failure(
                        last_report_.viewport_diagnostic);
                }
            }
        }

        // Apply diagnostics are content/protocol diagnostics. Valid objects in
        // the same packet remain usable, so they do not abort the whole frame.
        return RenderFrameExecutionStatus::success();
    }

    RenderFrameExecutionStatus RenderSceneFrameProcessor::flush()
    {
        if (state_ != State::Initialized)
        {
            return RenderFrameExecutionStatus::fatal_failure(
                "RenderSceneFrameProcessor cannot flush outside its initialized lifetime.");
        }
        return RenderFrameExecutionStatus::success();
    }

    RenderFrameExecutionStatus RenderSceneFrameProcessor::shutdown()
    {
        if (state_ == State::Stopped)
        {
            return RenderFrameExecutionStatus::success();
        }
        scenes_.clear();
        state_ = State::Stopped;
        return RenderFrameExecutionStatus::success();
    }

    const RenderScene* RenderSceneFrameProcessor::find_scene(
        RenderSceneId scene_id) const
    {
        const auto iterator = scenes_.find(scene_id.value());
        return scene_id && iterator != scenes_.end() ? &iterator->second : nullptr;
    }
}
