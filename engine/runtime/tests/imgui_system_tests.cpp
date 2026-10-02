#include "input/input_system.h"
#include "platform/window_interface.h"
#include "ui/imgui_system.h"

#include "imgui.h"

#include <cstdint>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

namespace
{
    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    class TestPlatformInput final : public toy3d::IPlatformInput
    {
      public:
        bool init() override
        {
            return true;
        }
        void exit() override
        {
        }
        void update() override
        {
        }
        toy3d::PlatformInputCapabilities capabilities() const noexcept override
        {
            return {true, true, true, true, true};
        }
    };

    class TestWindow final : public toy3d::IWindow
    {
      public:
        TestWindow()
        {
            platform_input = std::make_unique<TestPlatformInput>();
        }

        bool should_close() override
        {
            return false;
        }
        void process_events() override
        {
        }
        void close() override
        {
        }
        toy3d::Extent get_display_size() const override
        {
            return display_;
        }
        toy3d::Extent get_framebuffer_size() const override
        {
            return framebuffer_;
        }

        void set_extents(toy3d::Extent display, toy3d::Extent framebuffer)
        {
            display_ = display;
            framebuffer_ = framebuffer;
        }

      private:
        toy3d::Extent display_{800u, 600u};
        toy3d::Extent framebuffer_{1600u, 1200u};
    };
} // namespace

int main()
{
    using namespace toy3d;

    static_assert(offsetof(ImGuiVertex, position) == 0u, "ImGui position must be the first vertex field");
    static_assert(offsetof(ImGuiVertex, uv) == sizeof(float) * 2u, "ImGui UV must follow position");
    static_assert(offsetof(ImGuiVertex, color) == sizeof(float) * 4u, "ImGui packed color must follow UV");
    static_assert(sizeof(ImGuiVertex) == sizeof(float) * 4u + sizeof(std::uint32_t),
                  "ImGui vertex layout must remain tightly packed");
    static_assert(sizeof(ImWchar) == sizeof(std::uint32_t),
                  "Toy3d text input requires ImGui to preserve non-BMP Unicode scalars");

    check(is_unicode_scalar(0x41u), "ASCII must be a Unicode scalar");
    check(is_unicode_scalar(0x1F642u), "non-BMP input must be accepted");
    check(!is_unicode_scalar(0xD800u), "surrogates must be rejected");
    check(!is_unicode_scalar(0x110000u), "out-of-range code points must be rejected");

    InputSystem& input = InputSystem::get_instance();
    check(input.init(), "input initialization must succeed");
    int keyboard_callbacks = 0;
    InputBindingContext& context = input.create_binding_context("imgui_test");
    context.create_action("keyboard")
        .add_binding(KeyCode::A, KeyStatus::Pressed,
                     [&keyboard_callbacks](const InputEvent&)
                     {
                         ++keyboard_callbacks;
                     });
    input.activate_context("imgui_test", true);
    input.set_capture_policy({true, false, false});
    KeyEvent key_pressed;
    key_pressed.type = InputEventType::KeyPressed;
    key_pressed.key_code = KeyCode::A;
    input.get_keyboard_device()->set_key_status(KeyCode::A, KeyStatus::Pressed);
    input.process_event(key_pressed);
    check(keyboard_callbacks == 1, "mouse capture must not suppress keyboard gameplay mapping");
    int hold_callbacks = 0;
    input.bind_axis("imgui_test", "hold", KeyCode::A, 1.0f,
                    [&hold_callbacks](const InputEvent&)
                    {
                        ++hold_callbacks;
                    });
    input.set_capture_policy({false, true, true});
    input.get_keyboard_device()->set_key_status(KeyCode::A, KeyStatus::Hold);
    input.update();
    check(hold_callbacks == 0, "UI capture suppresses Hold callbacks as well as physical events");
    input.set_capture_policy({false, false, true});
    input.update();
    check(hold_callbacks == 1, "Focused PIE can receive Hold while text events remain captured");
    check(input.begin_play_session() && !input.begin_play_session(), "Gameplay input rejects overlapping sessions");
    int play_callbacks = 0;
    input.create_binding_context("play_fixture");
    input.bind_action("play_fixture", "press", KeyCode::A, KeyStatus::Pressed,
                      [&play_callbacks](const InputEvent&)
                      {
                          ++play_callbacks;
                      });
    input.activate_context("play_fixture", true);
    input.process_event(key_pressed);
    check(play_callbacks == 1, "Session context receives running events");
    input.end_play_session();
    input.process_event(key_pressed);
    check(play_callbacks == 1 && keyboard_callbacks == 3,
          "Stop removes gameplay callbacks while retaining host context");

    input.get_keyboard_device()->set_key_status(KeyCode::A, KeyStatus::Hold);
    input.get_mouse_device()->set_key_status(KeyCode::MOUSE_LEFT, KeyStatus::Hold);
    WindowFocusEvent focus_lost(false);
    input.process_event(focus_lost);
    check(input.get_keyboard_device()->get_key_status(KeyCode::A) == KeyStatus::None,
          "focus loss must clear held keyboard state");
    check(input.get_mouse_device()->get_key_status(KeyCode::MOUSE_LEFT) == KeyStatus::None,
          "focus loss must clear held mouse state");

    TestWindow window;
    ImGuiSystem imgui;
    const ImGuiSystemStatus initialized = imgui.initialize(window);
    check(initialized.succeeded(), "ImGuiSystem initialization must succeed");
    check(imgui.font_atlas().valid(), "font atlas snapshot must be valid RGBA32 data");
    check((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0, "Docking must be enabled");
    const TextInputEvent non_bmp_text(0x1F642u);
    input.process_event(non_bmp_text);

    check(imgui.begin_frame(window, 1.0 / 60.0), "a positive display/framebuffer extent must start a UI frame");
    check(!ImGui::GetIO().InputQueueCharacters.empty() &&
              ImGui::GetIO().InputQueueCharacters.back() == static_cast<ImWchar>(0x1F642u),
          "non-BMP Unicode input must reach ImGui without replacement");
    ImGui::Begin("Toy3d ImGui test");
    ImGui::TextUnformatted("Frame N payload");
    ImGui::End();
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(10.0F, 10.0F), ImVec2(50.0F, 50.0F),
                                                  IM_COL32(255, 255, 255, 255));
    ImGuiSnapshotResult frame_n = imgui.end_frame();
    if (!frame_n.succeeded())
    {
        std::cerr << frame_n.diagnostic << '\n';
    }
    check(frame_n.succeeded(), "basic widget snapshot must succeed");
    check(frame_n.draw_data != nullptr && !frame_n.draw_data->empty(),
          "basic widget snapshot must contain draw commands");
    if (frame_n.draw_data)
    {
        check(frame_n.draw_data->index_stride == sizeof(ImDrawIdx),
              "snapshot must preserve the configured ImDrawIdx width");
        check(frame_n.draw_data->framebuffer_width == 1600u && frame_n.draw_data->framebuffer_height == 1200u,
              "snapshot must preserve high-DPI framebuffer metadata");
        check(frame_n.draw_data->framebuffer_scale[0] == 2.0F && frame_n.draw_data->framebuffer_scale[1] == 2.0F,
              "snapshot must preserve framebuffer scale");
    }
    const std::size_t frame_n_vertices = frame_n.draw_data ? frame_n.draw_data->vertices.size() : 0u;
    check(imgui.begin_frame(window, 1.0 / 60.0), "the next UI frame must start after snapshot publication");
    ImGuiSnapshotResult empty_next_frame = imgui.end_frame();
    check(empty_next_frame.succeeded(), "empty UI frame must remain valid");
    check(frame_n.draw_data && frame_n.draw_data->vertices.size() == frame_n_vertices,
          "frame N snapshot must survive frame N+1 NewFrame");

    check(imgui.begin_frame(window, 1.0 / 60.0), "unknown texture test frame must start");
    ImGui::GetForegroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(2u)),
                                             ImVec2(0.0F, 0.0F), ImVec2(20.0F, 20.0F));
    ImGuiSnapshotResult unknown_texture = imgui.end_frame();
    check(!unknown_texture.succeeded() && unknown_texture.draw_data == nullptr,
          "unknown texture identity must reject the whole UI payload");
    check(unknown_texture.diagnostic.find("texture_id=2; kind=scene_viewport; elements=6") != std::string::npos &&
              unknown_texture.diagnostic.find("viewport_allowed=0") != std::string::npos,
          "Missing viewport registration reports actual identity, category and draw size");

    check(imgui.begin_frame(window, 1.0 / 60.0), "named texture diagnostic frame must start");
    ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_Always);
    ImGui::Begin("Texture diagnostic fixture");
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(3u)), ImVec2(20, 20));
    ImGui::End();
    const auto named_texture = imgui.end_frame({}, {ImGuiTextureId(4)});
    check(!named_texture.succeeded() &&
              named_texture.diagnostic.find("window='Texture diagnostic fixture'") != std::string::npos &&
              named_texture.diagnostic.find("texture_id=3; kind=user_image") != std::string::npos &&
              named_texture.diagnostic.find("additional_count=1; additional=[4]") != std::string::npos,
          "Unregistered image diagnostic identifies its window and allowed image IDs");

    check(imgui.begin_frame(window, 1.0 / 60.0), "registered viewport texture test frame must start");
    ImGui::GetForegroundDrawList()->AddImage(
        reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(IMGUI_SCENE_VIEWPORT_TEXTURE_ID.value())),
        ImVec2(0.0F, 0.0F), ImVec2(20.0F, 20.0F));
    ImGuiSnapshotResult viewport_texture = imgui.end_frame(IMGUI_SCENE_VIEWPORT_TEXTURE_ID);
    check(viewport_texture.succeeded() && viewport_texture.draw_data && !viewport_texture.draw_data->commands.empty() &&
              viewport_texture.draw_data->commands.back().texture_id == IMGUI_SCENE_VIEWPORT_TEXTURE_ID,
          "registered viewport texture identity must survive the UI snapshot");

    check(imgui.begin_frame(window, 1.0 / 60.0), "multiple logical image frame must start");
    ImGui::GetForegroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(3)),
                                             ImVec2(0, 0), ImVec2(20, 20));
    ImGui::GetForegroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(4)),
                                             ImVec2(30, 0), ImVec2(50, 20));
    const auto images = imgui.end_frame({}, {ImGuiTextureId(3), ImGuiTextureId(4)});
    check(images.succeeded() && images.draw_data && images.draw_data->commands.size() >= 2 &&
              images.draw_data->commands[images.draw_data->commands.size() - 2].texture_id == ImGuiTextureId(3) &&
              images.draw_data->commands.back().texture_id == ImGuiTextureId(4),
          "multiple IDs must preserve image command identity");
    check(imgui.begin_frame(window, 1.0 / 60.0), "invalid registry frame must start");
    const auto duplicates = imgui.end_frame({}, {ImGuiTextureId(3), ImGuiTextureId(3)});
    check(!duplicates.succeeded(), "duplicate logical IDs must reject registry publication");
    check(imgui.begin_frame(window, 1.0 / 60.0), "callback rejection test frame must start");
    ImGui::GetForegroundDrawList()->AddCallback(
        [](const ImDrawList*, const ImDrawCmd*)
        {
        },
        nullptr);
    ImGuiSnapshotResult callback = imgui.end_frame();
    check(!callback.succeeded() && callback.draw_data == nullptr,
          "ordinary render callbacks must reject the whole UI payload");

    check(imgui.begin_frame(window, 1.0 / 60.0), "reset-state callback test frame must start");
    ImGui::GetForegroundDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(10.0F, 10.0F), ImVec2(30.0F, 30.0F), IM_COL32(255, 0, 0, 255));
    ImGuiSnapshotResult reset_callback = imgui.end_frame();
    check(reset_callback.succeeded() && reset_callback.draw_data != nullptr,
          "reset-render-state sentinel must remain in a valid payload");

    window.set_extents({0u, 0u}, {0u, 0u});
    check(!imgui.begin_frame(window, 1.0 / 60.0), "zero extent must not create a renderable UI frame");

    imgui.shutdown();
    input.exit();
    return failures == 0 ? 0 : 1;
}
