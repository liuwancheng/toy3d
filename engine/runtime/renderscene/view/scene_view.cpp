#include "renderscene/view/scene_view.h"

#include <utility>

namespace toy3d
{
    bool build_scene_view(
        const SceneViewDesc& desc,
        SceneView& result,
        std::string& diagnostic)
    {
        diagnostic.clear();
        if (desc.view_rect.width == 0 || desc.view_rect.height == 0)
        {
            diagnostic = "SceneView requires a non-zero ViewRect.";
            return false;
        }
        Quaternion camera_orientation;
        if (!try_make_rotation_from_forward_up(
                desc.camera_forward, desc.camera_up, camera_orientation))
        {
            diagnostic =
                "SceneView requires finite, non-zero, non-parallel camera directions.";
            return false;
        }
        const float aspect = static_cast<float>(desc.view_rect.width) /
            static_cast<float>(desc.view_rect.height);
        PerspectiveProjectionDesc projection_desc;
        projection_desc.vertical_fov = to_radians(
            Degrees(desc.vertical_fov_degrees));
        projection_desc.aspect = aspect;
        projection_desc.near_clip = desc.near_clip;
        projection_desc.far_clip = desc.far_clip;
        SceneView built;
        built.view_rect = desc.view_rect;
        built.camera_position = desc.camera_position;
        built.camera_forward = rotate_vector(
            camera_orientation, Vector3(0.0f, 0.0f, 1.0f));
        built.near_clip = desc.near_clip;
        built.far_clip = desc.far_clip;
        if (!try_make_view_matrix(
                desc.camera_position, camera_orientation, built.view_matrix) ||
            !try_make_perspective_projection(
                projection_desc, built.projection_matrix))
        {
            diagnostic =
                "SceneView requires finite camera data, 0 < FOV < 180, and 0 < near < far.";
            return false;
        }
        built.view_projection_matrix =
            built.projection_matrix * built.view_matrix;
        if (!try_inverse(built.view_matrix, built.inverse_view_matrix) ||
            !try_inverse(
                built.projection_matrix, built.inverse_projection_matrix) ||
            !try_inverse(
                built.view_projection_matrix,
                built.inverse_view_projection_matrix) ||
            !is_finite(built.view_matrix) ||
            !is_finite(built.projection_matrix) ||
            !is_finite(built.view_projection_matrix) ||
            !is_finite(built.inverse_view_matrix) ||
            !is_finite(built.inverse_projection_matrix) ||
            !is_finite(built.inverse_view_projection_matrix))
        {
            diagnostic = "SceneView matrix construction produced a non-finite result.";
            return false;
        }
        result = std::move(built);
        return true;
    }

    SceneViewFamilyValidation validate_scene_view_family(
        const SceneViewFamily& family,
        std::string& diagnostic)
    {
        diagnostic.clear();
        if (!family.scene_id)
        {
            diagnostic = "SceneViewFamily requires a valid RenderSceneId.";
            return SceneViewFamilyValidation::InvalidArgument;
        }
        if (family.views.empty())
        {
            diagnostic = "SceneViewFamily requires one SceneView.";
            return SceneViewFamilyValidation::InvalidArgument;
        }
        if (family.views.size() != 1)
        {
            diagnostic = "Multiple SceneViews in one family are not supported yet.";
            return SceneViewFamilyValidation::Unsupported;
        }
        return SceneViewFamilyValidation::Valid;
    }
}
