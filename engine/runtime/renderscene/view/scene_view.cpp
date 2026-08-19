#include "renderscene/view/scene_view.h"

#include "glm/gtc/matrix_inverse.hpp"

#include <cmath>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool is_finite(const vec3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool is_finite(const mat4x4& value)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                for (std::size_t row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(value[column][row]))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        mat4x4 make_left_handed_view_matrix(
            const vec3& position,
            const vec3& right,
            const vec3& up,
            const vec3& forward)
        {
            mat4x4 result(1.0f);
            result[0] = {right.x, up.x, forward.x, 0.0f};
            result[1] = {right.y, up.y, forward.y, 0.0f};
            result[2] = {right.z, up.z, forward.z, 0.0f};
            result[3] = {
                -glm::dot(right, position),
                -glm::dot(up, position),
                -glm::dot(forward, position),
                1.0f};
            return result;
        }

        mat4x4 make_reversed_z_perspective_matrix(
            float vertical_fov_radians,
            float aspect,
            float near_clip,
            float far_clip)
        {
            const float inverse_tan_half_fov =
                1.0f / std::tan(vertical_fov_radians * 0.5f);
            mat4x4 result(0.0f);
            // GLM indexes column then row. These entries encode column-vector
            // left-handed projection with w=z and D3D-style 0..1 reversed-Z.
            result[0][0] = inverse_tan_half_fov / aspect;
            result[1][1] = inverse_tan_half_fov;
            result[2][2] = near_clip / (near_clip - far_clip);
            result[2][3] = 1.0f;
            result[3][2] =
                (near_clip * far_clip) / (far_clip - near_clip);
            return result;
        }
    }

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
        if (!is_finite(desc.camera_position) ||
            !is_finite(desc.camera_forward) ||
            !is_finite(desc.camera_up) ||
            !std::isfinite(desc.vertical_fov_degrees) ||
            !std::isfinite(desc.near_clip) ||
            !std::isfinite(desc.far_clip) ||
            desc.vertical_fov_degrees <= 0.0f ||
            desc.vertical_fov_degrees >= 180.0f ||
            desc.near_clip <= 0.0f ||
            desc.far_clip <= desc.near_clip)
        {
            diagnostic = "SceneView requires finite camera data, 0 < FOV < 180, and 0 < near < far.";
            return false;
        }

        constexpr float minimum_direction_length = 1.0e-6f;
        const float forward_length = glm::length(desc.camera_forward);
        const float up_length = glm::length(desc.camera_up);
        if (forward_length <= minimum_direction_length ||
            up_length <= minimum_direction_length)
        {
            diagnostic = "SceneView camera forward and up directions must be non-zero.";
            return false;
        }
        const vec3 forward = desc.camera_forward / forward_length;
        const vec3 requested_up = desc.camera_up / up_length;
        const vec3 right_candidate = glm::cross(requested_up, forward);
        const float right_length = glm::length(right_candidate);
        if (right_length <= minimum_direction_length)
        {
            diagnostic = "SceneView camera forward and up directions must not be parallel.";
            return false;
        }
        const vec3 right = right_candidate / right_length;
        const vec3 up = glm::cross(forward, right);
        const float aspect = static_cast<float>(desc.view_rect.width) /
            static_cast<float>(desc.view_rect.height);
        const float vertical_fov_radians = desc.vertical_fov_degrees * DEG2RAD;

        SceneView built;
        built.view_rect = desc.view_rect;
        built.camera_position = desc.camera_position;
        built.camera_forward = forward;
        built.near_clip = desc.near_clip;
        built.far_clip = desc.far_clip;
        built.view_matrix = make_left_handed_view_matrix(
            desc.camera_position, right, up, forward);
        built.projection_matrix = make_reversed_z_perspective_matrix(
            vertical_fov_radians, aspect, desc.near_clip, desc.far_clip);
        built.view_projection_matrix =
            built.projection_matrix * built.view_matrix;
        built.inverse_view_matrix = glm::inverse(built.view_matrix);
        built.inverse_projection_matrix = glm::inverse(built.projection_matrix);
        built.inverse_view_projection_matrix =
            glm::inverse(built.view_projection_matrix);
        if (!is_finite(built.view_matrix) ||
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
