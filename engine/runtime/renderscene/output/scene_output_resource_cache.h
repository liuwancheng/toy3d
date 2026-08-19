#pragma once

#include "drivers/rhi/rhi_device.h"
#include "renderscene/output/scene_output_update.h"
#include "renderscene/render_frame_dispatcher.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace toy3d
{
    struct SceneOutputResource
    {
        SceneOutputId output_id;
        SceneOutputRevision revision;
        SceneOutputExtent extent;
        RHITextureRef texture;
        RHITextureViewRef render_target_view;
        RHITextureViewRef shader_resource_view;
    };

    using SceneOutputResourceRef =
        std::shared_ptr<const SceneOutputResource>;

    struct SceneOutputApplyResult
    {
        RHIStatus status;
        std::size_t updated_count = 0;
        std::size_t released_count = 0;

        bool succeeded() const { return status.succeeded(); }
    };

    RenderFrameExecutionStatus scene_output_apply_execution_status(
        const RHIStatus& status);

    class SceneOutputResourceCache final
    {
    public:
        explicit SceneOutputResourceCache(RHIDevice& device);

        SceneOutputApplyResult apply_updates(
            const std::vector<SceneOutputUpdate>& updates);
        SceneOutputResourceRef find(SceneOutputId output_id) const;
        SceneOutputRevision latest_revision(SceneOutputId output_id) const;
        bool is_released(SceneOutputId output_id) const;

    private:
        struct Entry
        {
            SceneOutputRevision latest_revision;
            bool released = false;
            SceneOutputResourceRef resource;
        };

        RHIResult<SceneOutputResourceRef> create_resource(
            const SceneOutputUpdate& update);

        RHIDevice& device_;
        std::unordered_map<std::uint64_t, Entry> entries_;
    };
}
