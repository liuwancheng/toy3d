#include "renderscene/view/scene_view.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    bool nearly_equal(float left, float right, float tolerance = 1.0e-5f)
    {
        return std::fabs(left - right) <= tolerance;
    }

    bool matrix_nearly_identity(const toy3d::mat4x4& value)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t row = 0; row < 4; ++row)
            {
                const float expected = column == row ? 1.0f : 0.0f;
                if (!nearly_equal(value[column][row], expected, 1.0e-4f))
                {
                    return false;
                }
            }
        }
        return true;
    }
}

int main()
{
    using namespace toy3d;

    SceneViewDesc desc;
    desc.camera_position = {3.0f, 2.0f, -5.0f};
    desc.camera_forward = {0.0f, 0.0f, 4.0f};
    desc.camera_up = {0.0f, 2.0f, 0.0f};
    desc.vertical_fov_degrees = 60.0f;
    desc.near_clip = 0.1f;
    desc.far_clip = 1000.0f;
    desc.view_rect = {10, 20, 1920, 1080};

    SceneView view;
    std::string diagnostic;
    check(build_scene_view(desc, view, diagnostic) && diagnostic.empty(),
        "A finite left-handed perspective SceneView must build");
    check(view.view_rect.x == 10 && view.view_rect.y == 20 &&
            view.view_rect.width == 1920 && view.view_rect.height == 1080 &&
            nearly_equal(view.camera_forward.z, 1.0f),
        "SceneView must retain its ViewRect and normalized forward direction");

    const vec4 camera_in_view = view.view_matrix * vec4(desc.camera_position, 1.0f);
    const vec4 forward_in_view = view.view_matrix *
        vec4(desc.camera_position + vec3(0.0f, 0.0f, 1.0f), 1.0f);
    check(nearly_equal(camera_in_view.x, 0.0f) &&
            nearly_equal(camera_in_view.y, 0.0f) &&
            nearly_equal(camera_in_view.z, 0.0f) &&
            nearly_equal(forward_in_view.z, 1.0f),
        "The view matrix must use +Z as left-handed camera forward");

    const vec4 near_clip = view.projection_matrix *
        vec4(0.0f, 0.0f, desc.near_clip, 1.0f);
    const vec4 far_clip = view.projection_matrix *
        vec4(0.0f, 0.0f, desc.far_clip, 1.0f);
    check(nearly_equal(near_clip.z / near_clip.w, 1.0f) &&
            nearly_equal(far_clip.z / far_clip.w, 0.0f, 1.0e-6f),
        "The projection matrix must map finite near to 1 and far to 0");

    const float tan_half_fov = std::tan(30.0f * DEG2RAD);
    const float aspect = 1920.0f / 1080.0f;
    const vec4 right_edge_clip = view.projection_matrix *
        vec4(desc.near_clip * tan_half_fov * aspect, 0.0f,
            desc.near_clip, 1.0f);
    check(nearly_equal(right_edge_clip.x / right_edge_clip.w, 1.0f),
        "The perspective matrix must derive horizontal scale from ViewRect aspect");
    check(matrix_nearly_identity(view.inverse_view_matrix * view.view_matrix) &&
            matrix_nearly_identity(
                view.inverse_projection_matrix * view.projection_matrix) &&
            matrix_nearly_identity(
                view.inverse_view_projection_matrix *
                    view.view_projection_matrix),
        "SceneView must retain finite inverses for View binding data");

    SceneView unchanged = view;
    SceneViewDesc invalid = desc;
    invalid.view_rect.width = 0;
    check(!build_scene_view(invalid, unchanged, diagnostic) &&
            !diagnostic.empty() && unchanged.view_rect.width == view.view_rect.width,
        "A zero ViewRect must fail without partially replacing the output SceneView");
    invalid = desc;
    invalid.camera_up = invalid.camera_forward;
    check(!build_scene_view(invalid, unchanged, diagnostic) && !diagnostic.empty(),
        "Parallel camera forward and up directions must be rejected");
    invalid = desc;
    invalid.far_clip = invalid.near_clip;
    check(!build_scene_view(invalid, unchanged, diagnostic) && !diagnostic.empty(),
        "Finite perspective requires far greater than near");

    SceneViewFamily family;
    family.scene_id = RenderSceneId(1);
    check(validate_scene_view_family(family, diagnostic) ==
            SceneViewFamilyValidation::InvalidArgument && !diagnostic.empty(),
        "A SceneViewFamily without a view must fail explicitly");
    family.views.push_back(view);
    check(validate_scene_view_family(family, diagnostic) ==
            SceneViewFamilyValidation::Valid && diagnostic.empty(),
        "The first milestone must accept exactly one SceneView per family");
    family.views.push_back(view);
    check(validate_scene_view_family(family, diagnostic) ==
            SceneViewFamilyValidation::Unsupported &&
            diagnostic.find("not supported") != std::string::npos,
        "Multiple SceneViews must return an explicit unsupported diagnostic");
    family.views.resize(1);
    family.scene_id = {};
    check(validate_scene_view_family(family, diagnostic) ==
            SceneViewFamilyValidation::InvalidArgument && !diagnostic.empty(),
        "A SceneViewFamily must identify its persistent RenderScene");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " SceneView check(s) failed.\n";
        return 1;
    }
    std::cout << "SceneView checks passed.\n";
    return 0;
}
