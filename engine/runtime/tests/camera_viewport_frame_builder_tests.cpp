#include "gamescene/camera_viewport_frame_builder.h"

#include "gamescene/component/camera_component.h"
#include "gamescene/world.h"

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

    bool nearly_equal(float lhs, float rhs)
    {
        return std::abs(lhs - rhs) <= 1.0e-5f;
    }
}

int main()
{
    using namespace toy3d;

    World world;
    SceneComponent& camera_parent =
        world.create_actor().create_scene_component();
    Transform parent_transform;
    parent_transform.translation = {10.0f, 0.0f, 0.0f};
    check(try_make_quaternion_from_axis_angle(
            Vector3(1.0f, 0.0f, 0.0f),
            to_radians(Degrees(90.0f)),
            parent_transform.rotation),
        "The test camera parent rotation must build");
    parent_transform.scale = {2.0f, 3.0f, 4.0f};
    check(camera_parent.set_local_transform(parent_transform),
        "The test camera parent must accept its hierarchy transform");
    CameraComponent& camera =
        world.create_actor().create_scene_component<CameraComponent>();
    check(camera.attach_to(&camera_parent, AttachmentRule::KeepRelative),
        "The test camera must attach inside the same World hierarchy");
    check(camera.set_perspective(75.0f, 0.25f, 500.0f),
        "The test camera must accept a finite perspective projection");

    Transform transform;
    transform.scale = {2.0f, 3.0f, 4.0f};
    check(try_make_quaternion_from_axis_angle(
            Vector3(0.0f, 1.0f, 0.0f),
            to_radians(Degrees(45.0f)),
            transform.rotation),
        "The test camera local rotation must build");
    check(camera.set_local_transform(transform),
        "The test camera must accept its Game Thread transform");

    CameraViewportFrameDesc desc;
    desc.viewport_id = ViewportId(1);
    desc.output.output_id = SceneOutputId(1);
    desc.output.type = SceneOutputType::Present;
    desc.output.extent = {1280, 720};
    desc.view_rect = {0, 0, 1280, 720};

    ViewportFrame frame;
    frame.viewport_id = ViewportId(99);
    std::string diagnostic;
    check(build_camera_viewport_frame(camera, desc, frame, diagnostic) ==
            ViewportFrameValidation::InvalidArgument &&
            frame.viewport_id == ViewportId(99),
        "A dirty Camera transform must fail without replacing the previous output");

    world.update_transforms();
    check(build_camera_viewport_frame(camera, desc, frame, diagnostic) ==
            ViewportFrameValidation::Valid && diagnostic.empty(),
        "An updated finite perspective Camera must build one owned ViewportFrame");
    check(frame.viewport_id == desc.viewport_id &&
            frame.scene_frames.size() == 1 &&
            frame.scene_frames[0].view_family.scene_id ==
                world.render_scene_id() &&
            frame.scene_frames[0].view_family.views.size() == 1 &&
            frame.scene_frames[0].output.output_id == desc.output.output_id,
        "The built frame must own the requested viewport, scene, View, and output identities");
    const SceneView& view = frame.scene_frames[0].view_family.views[0];
    const float expected_vertical_projection_scale =
        1.0f / tan(to_radians(Degrees(75.0f)) * 0.5f);
    const float expected_forward_component = std::sqrt(0.5f);
    check(nearly_equal(view.camera_position.x, 10.0f) &&
            nearly_equal(view.camera_position.y, 0.0f) &&
            nearly_equal(view.camera_position.z, 0.0f) &&
            nearly_equal(
                view.camera_forward.x, expected_forward_component) &&
            nearly_equal(
                view.camera_forward.y, -expected_forward_component) &&
            nearly_equal(view.camera_forward.z, 0.0f) &&
            nearly_equal(view.inverse_view_matrix.at(1, 0), 0.0f) &&
            nearly_equal(view.inverse_view_matrix.at(1, 1), 0.0f) &&
            nearly_equal(view.inverse_view_matrix.at(1, 2), 1.0f) &&
            nearly_equal(
                view.projection_matrix.at(1, 1),
                expected_vertical_projection_scale) &&
            nearly_equal(view.near_clip, 0.25f) &&
            nearly_equal(view.far_clip, 500.0f),
        "The SceneView snapshot must derive scale-independent hierarchy axes and projection from the CameraComponent");

    ViewportFrame previous = frame;
    CameraViewportFrameDesc invalid = desc;
    invalid.output.output_id = {};
    check(build_camera_viewport_frame(camera, invalid, frame, diagnostic) ==
            ViewportFrameValidation::InvalidArgument &&
            frame.viewport_id == previous.viewport_id &&
            frame.scene_frames[0].output.output_id ==
                previous.scene_frames[0].output.output_id,
        "An invalid output description must fail atomically");

    CameraViewportFrameDesc minimized = desc;
    minimized.output.type = SceneOutputType::Offscreen;
    minimized.output.extent = {};
    minimized.output.output_id = SceneOutputId(2);
    check(build_camera_viewport_frame(camera, minimized, frame, diagnostic) ==
            ViewportFrameValidation::Valid &&
            frame.scene_frames[0].output.type == SceneOutputType::Offscreen &&
            frame.scene_frames[0].output.extent.width == 0 &&
            frame.scene_frames[0].view_family.views[0].view_rect.width == 1280,
        "A minimized output must preserve a valid owned Camera view while carrying the true zero extent");

    if (failure_count != 0)
    {
        std::cerr << failure_count <<
            " camera ViewportFrame builder check(s) failed.\n";
        return 1;
    }
    std::cout << "Camera ViewportFrame builder checks passed.\n";
    return 0;
}
