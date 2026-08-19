#pragma once

#include "math/math.h"
#include "rendercore/render_id.h"

#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    struct SceneViewRect
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    struct SceneViewDesc
    {
        vec3 camera_position{0.0f};
        vec3 camera_forward{0.0f, 0.0f, 1.0f};
        vec3 camera_up{0.0f, 1.0f, 0.0f};
        float vertical_fov_degrees = 60.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
        SceneViewRect view_rect;
    };

    struct SceneView
    {
        SceneViewRect view_rect;
        vec3 camera_position{0.0f};
        vec3 camera_forward{0.0f, 0.0f, 1.0f};
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
        mat4x4 view_matrix{1.0f};
        mat4x4 projection_matrix{1.0f};
        mat4x4 view_projection_matrix{1.0f};
        mat4x4 inverse_view_matrix{1.0f};
        mat4x4 inverse_projection_matrix{1.0f};
        mat4x4 inverse_view_projection_matrix{1.0f};
    };

    bool build_scene_view(
        const SceneViewDesc& desc,
        SceneView& result,
        std::string& diagnostic);

    struct SceneViewFamily
    {
        RenderSceneId scene_id;
        std::vector<SceneView> views;
    };

    enum class SceneViewFamilyValidation
    {
        Valid,
        InvalidArgument,
        Unsupported
    };

    SceneViewFamilyValidation validate_scene_view_family(
        const SceneViewFamily& family,
        std::string& diagnostic);
}
