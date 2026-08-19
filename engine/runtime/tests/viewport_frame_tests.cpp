#include "renderscene/view/viewport_frame.h"

#include <iostream>
#include <string>
#include <vector>

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

    toy3d::SceneView make_view(
        std::uint32_t x = 0,
        std::uint32_t y = 0,
        std::uint32_t width = 1280,
        std::uint32_t height = 720)
    {
        toy3d::SceneViewDesc desc;
        desc.view_rect = {x, y, width, height};
        toy3d::SceneView view;
        std::string diagnostic;
        if (!toy3d::build_scene_view(desc, view, diagnostic))
        {
            std::cerr << "FAIL: Test SceneView setup failed: " << diagnostic << '\n';
            ++failure_count;
        }
        return view;
    }

    toy3d::SceneViewFamilyFrame make_scene_frame(
        toy3d::RenderSceneId scene_id,
        toy3d::SceneOutputId output_id,
        toy3d::SceneOutputType type = toy3d::SceneOutputType::Present)
    {
        toy3d::SceneViewFamilyFrame frame;
        frame.view_family.scene_id = scene_id;
        frame.view_family.views.push_back(make_view());
        frame.output.output_id = output_id;
        frame.output.type = type;
        frame.output.extent = {1280, 720};
        return frame;
    }
}

int main()
{
    using namespace toy3d;

    const RenderSceneId scene_id(1);
    ViewportFrame frame;
    frame.viewport_id = ViewportId(1);
    frame.scene_frames.push_back(
        make_scene_frame(scene_id, SceneOutputId(1)));
    std::string diagnostic;
    check(validate_viewport_frame(frame, diagnostic) ==
            ViewportFrameValidation::Valid && diagnostic.empty(),
        "One Present family with one View must form a valid ViewportFrame");

    frame.scene_frames.push_back(make_scene_frame(
        scene_id, SceneOutputId(2), SceneOutputType::Offscreen));
    check(validate_viewport_frame(frame, diagnostic) ==
            ViewportFrameValidation::Valid,
        "One viewport may contain one Present and additional Offscreen outputs");

    ViewportFrame ui_only;
    ui_only.viewport_id = ViewportId(2);
    check(validate_viewport_frame(ui_only, diagnostic) ==
            ViewportFrameValidation::Valid,
        "A ViewportFrame may contain no Scene family for UI-only rendering");

    ViewportFrame minimized;
    minimized.viewport_id = ViewportId(3);
    minimized.scene_frames.push_back(
        make_scene_frame(scene_id, SceneOutputId(3)));
    minimized.scene_frames[0].output.extent = {};
    check(validate_viewport_frame(minimized, diagnostic) ==
            ViewportFrameValidation::Valid,
        "A zero SceneOutput extent must remain a valid skipped output");
    minimized.scene_frames[0].output.extent = {1280, 0};
    check(validate_viewport_frame(minimized, diagnostic) ==
            ViewportFrameValidation::InvalidArgument && !diagnostic.empty(),
        "A partially zero SceneOutput extent must be rejected");

    ViewportFrame duplicate_output = frame;
    duplicate_output.scene_frames[1].output.output_id =
        duplicate_output.scene_frames[0].output.output_id;
    check(validate_viewport_frame(duplicate_output, diagnostic) ==
            ViewportFrameValidation::InvalidArgument,
        "Two families in one viewport cannot write the same SceneOutputId");

    ViewportFrame two_present = frame;
    two_present.scene_frames[1].output.type = SceneOutputType::Present;
    check(validate_viewport_frame(two_present, diagnostic) ==
            ViewportFrameValidation::Unsupported,
        "A second Present output must return explicit Unsupported");

    ViewportFrame invalid_type = frame;
    invalid_type.scene_frames[0].output.type =
        static_cast<SceneOutputType>(255);
    check(validate_viewport_frame(invalid_type, diagnostic) ==
            ViewportFrameValidation::InvalidArgument,
        "An out-of-domain SceneOutputType must be rejected");

    ViewportFrame out_of_bounds = frame;
    out_of_bounds.scene_frames[0].view_family.views[0] =
        make_view(1000, 0, 400, 720);
    check(validate_viewport_frame(out_of_bounds, diagnostic) ==
            ViewportFrameValidation::InvalidArgument,
        "A non-zero output must contain its SceneView ViewRect");

    ViewportFrame multiple_views = frame;
    multiple_views.scene_frames[0].view_family.views.push_back(make_view());
    check(validate_viewport_frame(multiple_views, diagnostic) ==
            ViewportFrameValidation::Unsupported,
        "Viewport validation must preserve multi-View Unsupported");

    std::vector<ViewportFrame> packet_frames;
    packet_frames.push_back(frame);
    ViewportFrame second_viewport;
    second_viewport.viewport_id = ViewportId(4);
    second_viewport.scene_frames.push_back(make_scene_frame(
        scene_id, SceneOutputId(4), SceneOutputType::Offscreen));
    packet_frames.push_back(second_viewport);
    check(validate_viewport_frames(packet_frames, diagnostic) ==
            ViewportFrameValidation::Valid,
        "Distinct viewports and outputs must validate as one packet snapshot");
    packet_frames[1].viewport_id = packet_frames[0].viewport_id;
    check(validate_viewport_frames(packet_frames, diagnostic) ==
            ViewportFrameValidation::InvalidArgument,
        "One packet cannot contain duplicate ViewportId values");
    packet_frames[1].viewport_id = ViewportId(4);
    packet_frames[1].scene_frames[0].output.output_id =
        packet_frames[0].scene_frames[0].output.output_id;
    check(validate_viewport_frames(packet_frames, diagnostic) ==
            ViewportFrameValidation::InvalidArgument,
        "One packet cannot write the same SceneOutputId from two viewports");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " ViewportFrame check(s) failed.\n";
        return 1;
    }
    std::cout << "ViewportFrame checks passed.\n";
    return 0;
}
