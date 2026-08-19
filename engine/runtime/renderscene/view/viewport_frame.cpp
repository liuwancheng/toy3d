#include "renderscene/view/viewport_frame.h"

#include <set>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool is_zero_extent(const SceneOutputExtent& extent)
        {
            return extent.width == 0 && extent.height == 0;
        }

        bool view_rect_fits_output(
            const SceneViewRect& rect,
            const SceneOutputExtent& extent)
        {
            return rect.x <= extent.width && rect.y <= extent.height &&
                rect.width <= extent.width - rect.x &&
                rect.height <= extent.height - rect.y;
        }
    }

    ViewportFrameValidation validate_viewport_frame(
        const ViewportFrame& frame,
        std::string& diagnostic)
    {
        diagnostic.clear();
        if (!frame.viewport_id)
        {
            diagnostic = "ViewportFrame requires a valid ViewportId.";
            return ViewportFrameValidation::InvalidArgument;
        }

        std::set<std::uint64_t> output_ids;
        std::size_t present_count = 0;
        for (const SceneViewFamilyFrame& scene_frame : frame.scene_frames)
        {
            if (!scene_frame.output.output_id)
            {
                diagnostic = "Every SceneOutput requires a valid SceneOutputId.";
                return ViewportFrameValidation::InvalidArgument;
            }
            if (!output_ids.emplace(scene_frame.output.output_id.value()).second)
            {
                diagnostic = "A ViewportFrame cannot write one SceneOutputId more than once.";
                return ViewportFrameValidation::InvalidArgument;
            }
            switch (scene_frame.output.type)
            {
            case SceneOutputType::Present:
                ++present_count;
                if (present_count > 1)
                {
                    diagnostic = "A ViewportFrame supports at most one Present output.";
                    return ViewportFrameValidation::Unsupported;
                }
                break;
            case SceneOutputType::Offscreen:
                break;
            default:
                diagnostic = "SceneOutput has an invalid output type.";
                return ViewportFrameValidation::InvalidArgument;
            }

            std::string family_diagnostic;
            const SceneViewFamilyValidation family_validation =
                validate_scene_view_family(
                    scene_frame.view_family, family_diagnostic);
            if (family_validation != SceneViewFamilyValidation::Valid)
            {
                diagnostic = std::move(family_diagnostic);
                return family_validation == SceneViewFamilyValidation::Unsupported
                    ? ViewportFrameValidation::Unsupported
                    : ViewportFrameValidation::InvalidArgument;
            }
            const SceneOutputExtent& extent = scene_frame.output.extent;
            if ((extent.width == 0) != (extent.height == 0))
            {
                diagnostic = "SceneOutput extent must have both dimensions zero or both non-zero.";
                return ViewportFrameValidation::InvalidArgument;
            }
            if (is_zero_extent(extent))
            {
                continue;
            }
            if (!view_rect_fits_output(
                    scene_frame.view_family.views[0].view_rect, extent))
            {
                diagnostic = "SceneView ViewRect must fit inside its non-zero SceneOutput extent.";
                return ViewportFrameValidation::InvalidArgument;
            }
        }
        return ViewportFrameValidation::Valid;
    }

    ViewportFrameValidation validate_viewport_frames(
        const std::vector<ViewportFrame>& frames,
        std::string& diagnostic)
    {
        diagnostic.clear();
        std::set<std::uint64_t> viewport_ids;
        std::set<std::uint64_t> output_ids;
        for (const ViewportFrame& frame : frames)
        {
            if (!frame.viewport_id ||
                !viewport_ids.emplace(frame.viewport_id.value()).second)
            {
                diagnostic = frame.viewport_id
                    ? "A RenderFramePacket cannot contain one ViewportId more than once."
                    : "ViewportFrame requires a valid ViewportId.";
                return ViewportFrameValidation::InvalidArgument;
            }
            const ViewportFrameValidation validation =
                validate_viewport_frame(frame, diagnostic);
            if (validation != ViewportFrameValidation::Valid)
            {
                return validation;
            }
            for (const SceneViewFamilyFrame& scene_frame : frame.scene_frames)
            {
                if (!output_ids.emplace(
                        scene_frame.output.output_id.value()).second)
                {
                    diagnostic = "A RenderFramePacket cannot write one SceneOutputId from multiple viewports.";
                    return ViewportFrameValidation::InvalidArgument;
                }
            }
        }
        return ViewportFrameValidation::Valid;
    }
}
