#include "application/application.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "platform/platform_defines.h"
#if WITH_WIN
#include <windows.h>
#include "platform/win/win32_window.h"
#endif

#include "assets/animation/animation_editor_panel.h"
#include "assets/thumbnails/asset_thumbnail_pool.h"
#include "config/command_line_parser.h"
#include "engine.h"
#include "gamescene/world/world.h"
#include "image/png_codec.h"
#include "imgui.h"
#include "panels/content_browser_panel.h"
#include "rendercore/frame_synchronization.h"
#include "scene/placement/actor_factory.h"
#include "scene/editor_selection.h"
#include "workspace/editor_workspace.h"

namespace
{
    using namespace toy3d;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    AssetId id_for(const EditorWorkspace& workspace, const std::string& name)
    {
        for (const auto& entry : workspace.catalog().entries)
        {
            if (entry.path.utf8() == "/Project/" + name + ".asset")
            {
                return entry.file.asset_id;
            }
        }
        throw std::runtime_error("Missing Manny test asset: " + name);
    }

    struct TestState
    {
        bool complete = false;
        std::string error;
    };

    // --------------------------------------------------------------------------
    // AnimationPreviewApplication: production preview and thumbnail coexistence
    // --------------------------------------------------------------------------
    class AnimationPreviewApplication final : public Application
    {
      public:
        AnimationPreviewApplication(EditorWorkspace& workspace, TestState& state)
            : workspace_(workspace), panel_(workspace), pool_(workspace), state_(state),
              skeleton_(id_for(workspace, "SKM_Manny_Skeleton")), simple_(id_for(workspace, "SKM_Manny_Simple")),
              full_(id_for(workspace, "SKM_Manny")), run_(id_for(workspace, "MM_Run_Fwd"))
        {
        }

      private:
        bool on_initialize() override
        {
            ImGui::GetIO().IniFilename = nullptr;
            author_revision_ = world().content_revision();
            return factory_.initialize();
        }
        bool starts_world_play() const override
        {
            return false;
        }
        bool uses_preview_scene() const override
        {
            return true;
        }
        bool uses_animation_preview_scene() const override
        {
            return true;
        }
        bool on_initialize_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks) override
        {
            if (!pool_.initialize(scene, factory_.default_material(), tasks))
            {
                return false;
            }
            // Exercise reference-pose GPU generation even when a prior run left valid PNGs.
            pool_.generate(simple_);
            pool_.generate(full_);
            return true;
        }
        bool on_initialize_animation_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks) override
        {
            if (!panel_.initialize(scene, factory_.default_material(), tasks))
            {
                state_.error = panel_.error();
                return false;
            }
            panel_.request_open(skeleton_);
            return true;
        }
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override
        {
            views.emplace_back(Vector3(0, 0, -300), Quaternion::identity(), Vector3(0, 0, 1),
                               IntRect{0, 0, extent.width, extent.height}, extent, CameraProjectionMode::Perspective,
                               Radians(k_pi / 4), 1.0f, 10000.0f);
        }
        void fail(const std::string& error)
        {
            state_.error = error;
            window().close();
        }
        void on_tick(double delta) override
        {
            if (phase_ != timed_phase_)
            {
                timed_phase_ = phase_;
                elapsed_ = 0;
                std::cout << "Animation preview phase " << phase_ << std::endl;
            }
            else
            {
                elapsed_ += delta;
            }
            // Bound each operation, rather than charging repeated Debug asset validation
            // to the final reopen. CTest also bounds the complete integration run.
            if (elapsed_ > 90)
            {
                fail("Animation preview timed out at phase " + std::to_string(phase_) + ": " + panel_.error());
                return;
            }
            pool_.tick();
            panel_.tick(delta);
            if (phase_ == 5 && !panel_.error().empty())
            {
                if (!panel_.asset() || !(panel_.asset()->id == run_))
                {
                    fail("Failed candidate replaced the active animation.");
                    return;
                }
                panel_.close();
                panel_.request_open(skeleton_);
                panel_.tick(0);
                panel_.close();
                panel_.request_open(full_);
                phase_ = 6;
            }
            if (phase_ == 8)
            {
                bool ready = true;
                for (const auto& entry : workspace_.catalog().entries)
                {
                    if (entry.file.asset_id == simple_ || entry.file.asset_id == full_)
                    {
                        const auto thumbnail = pool_.request(entry);
                        if (!thumbnail.error.empty())
                        {
                            fail(thumbnail.error);
                            return;
                        }
                        ready = ready && thumbnail.texture_id.valid() && !thumbnail.busy;
                    }
                }
                if (ready)
                {
                    if (!world().actor_ids().empty() || world().content_revision() != author_revision_)
                    {
                        fail("Preview changed the author World.");
                        return;
                    }
                    state_.complete = true;
                    window().close();
                }
            }
        }
        void on_build_ui() override
        {
            const auto size = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
#if WITH_WIN
            const ImVec2 preview_size(size.x, std::max(360.0f, size.y - 190));
#else
            // Other platforms still exercise preview-target reconstruction. Native
            // WSI resize is only driven by the owned Win32 window in this fixture.
            const ImVec2 preview_size(phase_ >= 7 ? size.x * 0.75f : size.x,
                                      std::max(360.0f, size.y - (phase_ >= 7 ? 280.0f : 190.0f)));
#endif
            // The production panel supplies a FirstUseEver default size; resize its
            // existing window explicitly so the native resize also changes its canvas.
            ImGui::SetWindowSize("Animation Editor", preview_size, ImGuiCond_Always);
            ImGui::SetNextWindowSize(preview_size, ImGuiCond_Always);
            panel_.draw();
            ImGui::SetNextWindowPos(ImVec2(0, size.y - 185), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(size.x, 185), ImGuiCond_Always);
            browser_.draw(workspace_, selection_, folder_, show_engine_, pool_);
        }
        void on_collect_ui_render_work(UiRenderWork& work) override
        {
            pool_.collect_render_work(work);
            panel_.collect_render_work(work);
        }
        std::vector<ImGuiTextureId> ui_texture_ids() const override
        {
            auto ids = panel_.texture_ids();
            const auto thumbnails = pool_.texture_ids();
            ids.insert(ids.end(), thumbnails.begin(), thumbnails.end());
            return ids;
        }
        void capture(const UiTextureResult& result)
        {
            Rgba8Image image;
            image.width = result.extent.width;
            image.height = result.extent.height;
            image.pixels = result.bgra_pixels;
            for (std::size_t i = 0; i < image.pixels.size(); i += 4)
            {
                std::swap(image.pixels[i], image.pixels[i + 2]);
            }
            std::vector<std::uint8_t> png;
            NativePlatformFile files;
            const std::string path =
                std::string(TOY3D_ANIMATION_PREVIEW_TEST_ROOT) + "/phase-" + std::to_string(phase_) + ".png";
            if (!encode_png(image, png).succeeded() ||
                !files.write_binary(PhysicalPath(path), png, FileWriteMode::Truncate).succeeded())
            {
                fail("Animation preview capture failed.");
            }
        }
        void on_ui_texture_result(UiTextureResult result) override
        {
            if (result.texture_id.value() < (1ull << 44))
            {
                pool_.on_texture_result(std::move(result));
                return;
            }
            panel_.on_texture_result(result);
            if (!result.succeeded())
            {
                fail("GPU preview: " + result.error);
                return;
            }
            if (!panel_.asset() ||
                result.bgra_pixels.size() != static_cast<std::size_t>(result.extent.width) * result.extent.height * 4)
            {
                fail("Preview completed before a valid asset or GPU image.");
                return;
            }
            capture(result);
            const auto hash = sha256(result.bgra_pixels);
            switch (phase_)
            {
            case 0:
            {
                bool visible = false;
                for (std::size_t pixel = 0; pixel < result.bgra_pixels.size(); pixel += 4)
                {
                    visible = visible || result.bgra_pixels[pixel] > 100 || result.bgra_pixels[pixel + 1] > 100 ||
                              result.bgra_pixels[pixel + 2] > 100;
                }
                if (panel_.asset()->mesh || panel_.asset()->sequence || !visible)
                {
                    fail("Skeleton-only preview has no visible joints.");
                    return;
                }
                panel_.request_open(simple_);
                phase_ = 1;
                break;
            }
            case 1:
                if (!panel_.asset()->mesh || panel_.asset()->layout->skeleton().bones.size() != 89)
                {
                    fail("Simple mesh preview did not preserve its 89-bone skeleton.");
                    return;
                }
                panel_.set_preview_display(true, false, false);
                panel_.request_open(run_);
                phase_ = 2;
                break;
            case 2:
                if (!panel_.asset()->sequence || !panel_.asset()->mesh)
                {
                    fail("Animation did not select its compatible mesh.");
                    return;
                }
                pose_hash_ = hash;
                panel_.seek(0.6);
                phase_ = 3;
                break;
            case 3:
                if (hash == pose_hash_)
                {
                    fail("Seeking the sequence did not change the GPUSkin image.");
                    return;
                }
                panel_.set_preview_display(true, true, true);
                phase_ = 4;
                break;
            case 4:
                panel_.set_preview_display(false, true, false);
                panel_.set_playing(true);
                phase_ = 9;
                break;
            case 9:
            {
                panel_.set_playing(false);
                AssetId missing;
                AssetId::try_generate(missing);
                panel_.request_open(missing);
                phase_ = 5;
                break;
            }
            case 6:
                if (!(panel_.asset()->id == full_))
                {
                    fail("A stale closed-session load replaced the reopened mesh.");
                    return;
                }
                panel_.set_preview_display(true, true, false);
                old_extent_ = result.extent;
#if WITH_WIN
                if (auto* native = dynamic_cast<Win32Window*>(&window()))
                {
                    SetWindowPos(native->get_native_hwnd(), nullptr, 0, 0, 950, 780,
                                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
                }
#endif
                phase_ = 7;
                break;
            case 7:
                if (result.extent.width == old_extent_.width && result.extent.height == old_extent_.height)
                {
                    return;
                }
                phase_ = 8;
                break;
            default:
                break;
            }
        }
        void on_shutdown() override
        {
            panel_.shutdown();
            pool_.shutdown();
            if (!flush_rendering_commands().succeeded())
            {
                state_.error = "Animation preview teardown did not drain.";
            }
            factory_.release();
        }

        EditorWorkspace& workspace_;
        AnimationEditorPanel panel_;
        AssetThumbnailPool pool_;
        ActorFactory factory_;
        ContentBrowserPanel browser_;
        EditorSelection selection_;
        TestState& state_;
        AssetId skeleton_, simple_, full_, run_;
        std::string folder_ = "/Project";
        bool show_engine_ = false;
        int phase_ = 0;
        int timed_phase_ = -1;
        double elapsed_ = 0;
        std::uint64_t author_revision_ = 0;
        Sha256Hash pose_hash_{};
        Extent old_extent_;
    };
} // namespace

int main(int argc, char** argv)
{
    using namespace toy3d;
    try
    {
        // C++17 filesystem copies fixtures into an isolated writable asset root;
        // the user's Project files are never candidates for test mutation.
        const std::filesystem::path root(TOY3D_ANIMATION_PREVIEW_TEST_ROOT);
        const auto assets = root / "asset";
        std::filesystem::create_directories(assets);
        for (const auto& source : std::filesystem::directory_iterator(TOY3D_MANNY_PREVIEW_SOURCE))
        {
            std::filesystem::copy_file(source.path(), assets / source.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing);
        }
        EditorWorkspacePaths paths;
        paths.project_assets = PhysicalPath(assets.u8string());
        paths.saved = PhysicalPath((root / "saved").u8string());
        paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
        paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
        paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
        EditorWorkspace workspace;
        check(workspace.initialize(paths), workspace.error());
        const auto simple = id_for(workspace, "SKM_Manny_Simple");
        const auto run = id_for(workspace, "MM_Run_Fwd");
        const auto skeleton = id_for(workspace, "SKM_Manny_Skeleton");
        const auto loaded = load_animation_preview_asset(workspace.asset_pairs(), workspace.catalog(), run);
        check(loaded.succeeded(), loaded.status().message);
        check(!load_animation_preview_asset(workspace.asset_pairs(), workspace.catalog(), run, true, simple, run)
                   .succeeded(),
              "Mixed full/Simple skeleton selection was accepted.");
        check(animation_preview_asset_current(workspace.asset_pairs(), workspace.catalog(), loaded.value()),
              "Valid preview baseline rejected.");
        const auto* location = workspace.catalog().index.find(skeleton);
        const auto original = workspace.files().read_binary(location->path);
        auto changed = original.value();
        changed.push_back('\n');
        check(workspace.files().write_binary_atomic(location->path, changed, FilePublishMode::Replace).succeeded(),
              "Baseline mutation failed.");
        check(!animation_preview_asset_current(workspace.asset_pairs(), workspace.catalog(), loaded.value()),
              "Changed Skeleton baseline accepted.");
        check(workspace.files()
                  .write_binary_atomic(location->path, original.value(), FilePublishMode::Replace)
                  .succeeded(),
              "Baseline restore failed.");
        const bool multi = argc > 1 && std::string(argv[1]) == "--multithread";
        CommandLineParser::get_instance().parser_args(
            {"AnimationPreviewTests", "--Window.Width=1200", "--Window.Height=900",
             "--Window.Title=Animation Preview Tests",
             multi ? "--Renderer.MultiThreaded=true" : "--Renderer.MultiThreaded=false"});
        TestState state;
        Engine engine;
        EngineStartupPaths startup;
        startup.saved = PhysicalPath((root / "runtime_saved").u8string());
        check(engine.set_startup_paths(std::move(startup)), "Test runtime Saved configuration failed.");
        engine.set_application(std::make_unique<AnimationPreviewApplication>(workspace, state));
#if WITH_WIN
        engine.init(static_cast<void*>(GetModuleHandleW(nullptr)));
#else
        engine.init(nullptr);
#endif
        check(engine.initialized(), "Animation preview Engine initialization failed.");
        engine.main_loop();
        engine.exit();
        check(state.complete && state.error.empty(), "Animation preview integration: " + state.error);
        std::cout << "Skeleton/Simple/full mesh, GPUSkin seek, depth lines, independent thumbnails, stale "
                     "close/reopen, failure retention and resize passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
