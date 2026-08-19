#include "gamescene/camera_viewport_frame_builder.h"

#include "gamescene/component/camera_component.h"
#include "gamescene/world.h"

#include <utility>

namespace toy3d
{
    ViewportFrameValidation build_camera_viewport_frame(
        const CameraComponent& camera,
        const CameraViewportFrameDesc& desc,
        ViewportFrame& result,
        std::string& diagnostic)
    {
        diagnostic.clear();
        if (camera.is_transform_dirty())
        {
            diagnostic =
                "Camera world transforms must be updated before building a ViewportFrame.";
            return ViewportFrameValidation::InvalidArgument;
        }
        if (camera.projection_mode() != CameraProjectionMode::Perspective)
        {
            diagnostic =
                "Only finite perspective CameraComponent views are currently supported.";
            return ViewportFrameValidation::Unsupported;
        }

        const mat4x4& camera_transform = camera.world_transform();
        SceneViewDesc view_desc;
        view_desc.camera_position = vec3(camera_transform[3]);
        view_desc.camera_forward = vec3(camera_transform[2]);
        view_desc.camera_up = vec3(camera_transform[1]);
        view_desc.vertical_fov_degrees = camera.vertical_fov_degrees();
        view_desc.near_clip = camera.near_clip();
        view_desc.far_clip = camera.far_clip();
        view_desc.view_rect = desc.view_rect;

        SceneView view;
        if (!build_scene_view(view_desc, view, diagnostic))
        {
            return ViewportFrameValidation::InvalidArgument;
        }

        ViewportFrame built;
        built.viewport_id = desc.viewport_id;
        SceneViewFamilyFrame scene_frame;
        scene_frame.view_family.scene_id = camera.world().render_scene_id();
        scene_frame.view_family.views.push_back(std::move(view));
        scene_frame.output = desc.output;
        built.scene_frames.push_back(std::move(scene_frame));

        const ViewportFrameValidation validation =
            validate_viewport_frame(built, diagnostic);
        if (validation != ViewportFrameValidation::Valid)
        {
            return validation;
        }
        result = std::move(built);
        return ViewportFrameValidation::Valid;
    }
}
