#include "application/application.h"

#include <iostream>

#include "engine.h"
#include "gamescene/scene_view.h"
#include "gamescene/component/static_mesh_component.h"
#include "imgui.h"
#include "rendercore/frame_synchronization.h"
#include "scene/editor_play_session.h"
#include "scene/editor_scene_session.h"
#include "scene/editor_selection.h"
#include "viewport/scene_viewport.h"
#include "workspace/editor_workspace.h"

namespace
{
    using namespace toy3d;

    // --------------------------------------------------------------------------
    // PlayIntegrationApplication: real Vulkan author/Play scene switching
    // --------------------------------------------------------------------------
    class PlayIntegrationApplication final : public Application
    {
      public:
        PlayIntegrationApplication(EditorWorkspace& workspace, bool& complete, std::string& error)
            : workspace_(workspace), complete_(complete), error_(error),
              author_(workspace, factory_, materials_, selection_, viewport_)
        {
        }

      private:
        bool on_initialize() override
        {
            ImGui::GetIO().IniFilename = nullptr;
            if (!factory_.initialize())
            {
                return false;
            }
            PlacementRequest cube;
            cube.item = PlacementItemId::Cube;
            auto* actor = factory_.create(world(), cube);
            PlacementRequest light;
            light.item = PlacementItemId::DirectionalLight;
            if (!actor || !factory_.create(world(), light))
            {
                return false;
            }
            author_actor_ = actor;
            author_.bind(world());
            if (!author_.capture(snapshot_))
            {
                return false;
            }
            revision_ = world().content_revision();
            return true;
        }
        bool starts_world_play() const override
        {
            return false;
        }
        bool uses_play_scene() const override
        {
            return true;
        }
        void on_initialize_play_scene(SceneInterface& scene) override
        {
            scene_ = &scene;
        }
        bool renders_play_scene() const override
        {
            return play_.active();
        }
        std::shared_ptr<SceneRenderFeedback> scene_render_feedback() const override
        {
            return play_.feedback();
        }
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override
        {
            build_game_scene_views(play_.world() ? *play_.world() : world(), views, extent);
        }
        bool on_scene_viewport_extent(Extent& extent) const override
        {
            return viewport_.extent(extent);
        }
        void on_build_ui() override
        {
            viewport_.begin_frame();
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
            viewport_.draw(world(), selection_, author_.history(), &play_, !play_.active());
        }
        void fail(const std::string& message)
        {
            error_ = message;
            window().close();
        }
        void on_tick(double delta) override
        {
            elapsed_ += delta;
            if (elapsed_ > 30.0)
            {
                fail("Vulkan PIE preparation/switching timed out.");
                return;
            }
            if (phase_ == 0)
            {
                if (!scene_ || !play_.start(
                                   snapshot_, workspace_, factory_.actor_types(),
                                   [this](const std::string& name)
                                   {
                                       const auto material = factory_.default_material()->material();
                                       return name == material->desc().shader_name ? material->desc().shader_program
                                                                                   : nullptr;
                                   },
                                   *scene_))
                {
                    fail("Vulkan PIE startup: " + play_.error());
                    return;
                }
                phase_ = 1;
                frames_ = 0;
            }
            play_.tick(delta);
            if (phase_ <= 3 && !play_.active())
            {
                fail("Vulkan PIE failed: " + play_.error());
                return;
            }
            if (phase_ == 1 && play_.state() == EditorPlayState::Playing && ++frames_ >= 3)
            {
                const auto* mesh = dynamic_cast<const StaticMeshComponent*>(
                    play_.world()->find_actor_by_id(play_.world()->actor_ids().front())->root_component());
                const auto* author_mesh = dynamic_cast<const StaticMeshComponent*>(author_actor_->root_component());
                if (!mesh || !author_mesh || mesh->static_mesh() == author_mesh->static_mesh() ||
                    !author_mesh->has_render_state() || world().lifecycle_state() == WorldLifecycleState::Playing)
                {
                    fail("Vulkan PIE reused author resources or changed author gameplay state.");
                    return;
                }
                play_.pause();
                paused_time_ = play_.world()->world_time_seconds();
                phase_ = 2;
                frames_ = 0;
            }
            else if (phase_ == 2 && ++frames_ >= 2)
            {
                if (play_.world()->world_time_seconds() != paused_time_)
                {
                    fail("Vulkan PIE advanced time while paused.");
                    return;
                }
                play_.resume();
                phase_ = 3;
                frames_ = 0;
            }
            else if (phase_ == 3 && ++frames_ >= 2)
            {
                if (play_.world()->world_time_seconds() <= paused_time_ || !play_.stop() ||
                    world().content_revision() != revision_ ||
                    world().find_actor_by_id(author_actor_->actor_id()) != author_actor_)
                {
                    fail("Vulkan PIE resume/stop changed author identity or content.");
                    return;
                }
                phase_ = 4;
                frames_ = 0;
            }
            else if (phase_ == 4 && ++frames_ >= 3)
            {
                if (++cycles_ == 3)
                {
                    complete_ = true;
                    window().close();
                }
                else
                {
                    phase_ = 0;
                }
            }
        }
        void on_shutdown() override
        {
            play_.stop();
            for (const auto id : world().actor_ids())
            {
                world().destroy_actor(*world().find_actor_by_id(id));
            }
            if (!flush_rendering_commands().succeeded())
            {
                error_ = "Vulkan PIE final author drain failed.";
            }
            factory_.release();
        }
        EditorWorkspace& workspace_;
        bool& complete_;
        std::string& error_;
        ActorFactory factory_;
        MaterialAssignments materials_;
        EditorSelection selection_;
        SceneViewport viewport_;
        EditorSceneSession author_;
        EditorPlaySession play_;
        SceneInterface* scene_ = nullptr;
        Actor* author_actor_ = nullptr;
        SceneAssetData snapshot_;
        std::uint64_t revision_ = 0;
        double elapsed_ = 0;
        double paused_time_ = 0;
        int phase_ = 0;
        int frames_ = 0;
        int cycles_ = 0;
    };
} // namespace

bool check_editor_play_integration(toy3d::EditorWorkspace& workspace, void* platform_context)
{
    bool complete = false;
    std::string error;
    {
        toy3d::Engine engine;
        toy3d::ShaderLoadConfig config;
        config.mode = toy3d::ShaderLoadMode::ShaderMapEntry;
        config.path = toy3d::PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT);
        engine.set_shader_load_config(std::move(config));
        engine.set_application(std::make_unique<PlayIntegrationApplication>(workspace, complete, error));
        engine.init(platform_context);
        engine.main_loop();
        engine.exit();
    }
    if (!complete || !error.empty())
    {
        std::cerr << "Vulkan PIE integration failed: " << error << '\n';
        return false;
    }
    std::cout << "Vulkan PIE first-frame readiness, pause/resume, repeated switching and teardown passed.\n";
    return true;
}
