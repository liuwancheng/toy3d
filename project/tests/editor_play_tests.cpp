#include <cstdlib>

#include "scene/editor_play_session.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <map>

#include "rotating_actor.h"
#include "gamescene/scene_view.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "input/input_system.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/render_command.h"
#include "rendercore/rendering_thread.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map_collection.h"
#include "scene/editor_selection.h"
#include "scene/editor_scene_session.h"
#include "threading/task_graph/task_graph.h"
#include "threading/thread_manager.h"
#include "viewport/scene_viewport.h"
#include "workspace/editor_workspace.h"

namespace
{
    // --------------------------------------------------------------------------
    // PlayTestScene: owned FIFO proxy lifetime without a GPU device
    // --------------------------------------------------------------------------
    class PlayTestScene final : public toy3d::SceneInterface
    {
      public:
        std::atomic<std::size_t> primitives{0};
        std::atomic<std::size_t> updates{0};
        void add_primitive(std::unique_ptr<toy3d::PrimitiveSceneProxy> proxy) override
        {
            mark_material_usage_changed();
            toy3d::enqueue_render_command("PlayTestAdd",
                                          [this, proxy = std::move(proxy)]() mutable noexcept
                                          {
                                              auto* identity = proxy.get();
                                              proxies_.emplace(identity, std::move(proxy));
                                              ++primitives;
                                          });
        }
        void update_skeletal_mesh_pose(toy3d::PrimitiveSceneProxy*,
                                       std::shared_ptr<const toy3d::SkeletalMeshDeformationData>, toy3d::Matrix4,
                                       toy3d::AxisAlignedBounds, bool, bool, bool) override
        {
            std::abort();
        }
        void remove_primitive(toy3d::PrimitiveSceneProxy* proxy) override
        {
            mark_material_usage_changed();
            toy3d::enqueue_render_command("PlayTestRemove",
                                          [this, proxy]() noexcept
                                          {
                                              primitives -= proxies_.erase(proxy);
                                          });
        }
        void update_primitive_transform(toy3d::PrimitiveSceneProxy*, toy3d::Matrix4, toy3d::AxisAlignedBounds, bool,
                                        bool, bool) override
        {
            toy3d::enqueue_render_command("PlayTestTransform",
                                          [this]() noexcept
                                          {
                                              ++updates;
                                          });
        }
        void update_primitive_materials(toy3d::PrimitiveSceneProxy*, std::vector<toy3d::MaterialRenderProxy*>) override
        {
            mark_material_usage_changed();
        }
        void update_environment(toy3d::SceneEnvironmentSnapshot environment) override
        {
            std::string error;
            if (!toy3d::validate_scene_environment_snapshot(environment, error))
            {
                std::cerr << "Invalid PIE fixture environment: " << error << '\n';
                std::abort();
            }
            toy3d::enqueue_render_command("PlayTestEnvironment",
                                          [this, environment = std::move(environment)]() mutable noexcept
                                          {
                                              environment_ = std::move(environment);
                                          });
        }
        void add_light(std::unique_ptr<toy3d::LightSceneProxy>) override
        {
        }
        void update_light(toy3d::LightSceneProxy*, toy3d::LightSceneData) override
        {
        }
        void remove_light(toy3d::LightSceneProxy*) override
        {
        }

      private:
        std::map<toy3d::PrimitiveSceneProxy*, std::unique_ptr<toy3d::PrimitiveSceneProxy>> proxies_;
        toy3d::SceneEnvironmentSnapshot environment_;
    };
} // namespace

bool check_editor_play(toy3d::EditorWorkspace& workspace, const toy3d::ActorTypeRegistry& actors,
                       const toy3d::SceneAssetData& authored)
{
    using namespace toy3d;
    bool passed = true;
    auto check = [&passed](bool condition, const char* message)
    {
        if (!condition)
        {
            passed = false;
            std::cerr << "PIE FAILED: " << message << '\n';
        }
    };
    ShaderMapEntryLoader loader{PhysicalPath(TOY3D_TEST_SHADER_ROOT)};
    ShaderMap shaders(loader);
    ShaderMapProgramKey key;
    key.shader_name = "Toy3d/Surface/Phong";
    key.pass_name = "Forward";
    key.role = shader::ShaderPassRole::Forward;
    key.vertex_factory = shader::VertexFactoryType::Local;
    key.platform = ShaderPlatform::VulkanES31;
    const auto defaults =
        ShaderMapCollection::create_candidate(loader.load_default_collection(key.shader_name, key.platform));
    check(defaults.succeeded(), "Published fixture Shader defaults resolve from the source domain");
    if (!defaults.succeeded())
    {
        return false;
    }
    const auto loaded =
        shaders.find_or_load_collection(key.shader_name, key.platform, defaults.collection->index().permutation_key);
    check(loaded.succeeded(), "Published fixture Shader Program loads");
    if (!loaded.succeeded())
    {
        return false;
    }
    auto programs = [program = loaded.collection](const std::string& name,
                                                  const std::vector<shader::ShaderPermutationSelection>& selections)
    {
        const auto resolved = shader::resolve_shader_permutation(program->index().material_domain, selections);
        return name == "Toy3d/Surface/Phong" && resolved.succeeded() &&
                       resolved.permutation->key == program->index().permutation_key
                   ? program
                   : nullptr;
    };
    // Isolated candidate includes a Mesh proxy while retaining the custom Actor's
    // typed properties. Author DTO and source files are never changed.
    SceneAssetData candidate = authored;
    candidate.actors.front().components.front().type = "toy3d.StaticMeshComponent";
    SceneMeshData mesh;
    mesh.builtin_mesh = "Cube";
    candidate.actors.front().components.front().properties = mesh;
    InputSystem::get_instance().init();
    for (const bool multithreaded : {false, true})
    {
        ThreadManager threads;
        auto graph_result = create_task_graph({multithreaded ? 1u : 0u, 256u, multithreaded}, threads);
        check(graph_result.succeeded(), "Create fixture TaskGraph");
        if (!graph_result.succeeded())
        {
            continue;
        }
        auto graph = graph_result.take_task_graph();
        check(graph->attach_to_thread(NamedThread::GameThread).succeeded(), "Attach GT");
        RenderingThread rendering(threads, *graph,
                                  multithreaded ? RenderingThreadMode::MultiThread : RenderingThreadMode::SingleThread);
        check(rendering.start().succeeded(), "Publish FIFO rendering bridge");
        {
            PlayTestScene scene;
            EditorPlaySession play;
            // Real ImGui frames exercise viewport controls; no native window or
            // GPU image is claimed by this fixture.
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(900, 650);
            io.DeltaTime = 1.0f / 60.0f;
            io.ConfigWindowsMoveFromTitleBarOnly = true;
            unsigned char* pixels = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            World author_world;
            EditorSelection selection;
            ActorFactory factory;
            MaterialAssignments assignments;
            EditorCommandHistory history(factory, assignments);
            SceneViewport viewport;
            auto frame = [&]()
            {
                ImGui::NewFrame();
                viewport.begin_frame();
                ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(750, 550), ImGuiCond_Always);
                viewport.draw(author_world, selection, history, &play, !play.active());
                ImGui::Render();
            };
            frame();
            frame();
            auto click_control = [&](bool second)
            {
                const auto* window = ImGui::FindWindowByName("Scene Viewport###Game Viewport");
                const auto& style = ImGui::GetStyle();
                const float button_size = ImGui::GetFrameHeight();
                float x = window->WorkRect.Max.x - style.CellPadding.x - button_size * 0.5f;
                if (play.active() && play.state() != EditorPlayState::Starting && !second)
                {
                    x -= button_size + style.ItemSpacing.x;
                }
                const float y = window->Pos.y + window->TitleBarHeight() + style.WindowPadding.y + style.CellPadding.y +
                                button_size * 0.5f;
                io.AddMousePosEvent(x, y);
                frame();
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                frame();
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                frame();
            };
            click_control(false);
            check(play.take_action() == EditorPlayAction::Play, "Viewport Play button requests embedded session");
            auto invalid = candidate;
            invalid.actors.front().type = "missing.NativeActor";
            check(!play.start(invalid, workspace, actors, programs, scene) && !play.active() && !play.world(),
                  "Unknown type rolls back candidate before starting gameplay");
            for (int iteration = 0; iteration != 3; ++iteration)
            {
                check(play.start(candidate, workspace, actors, programs, scene), "Repeat Play creates fresh World");
                if (!play.world())
                {
                    break;
                }
                const auto* actor = dynamic_cast<const RotatingActor*>(
                    play.world()->find_actor_by_id(play.world()->actor_ids().front()));
                check(actor != nullptr, "PIE preserves native concrete type");
                if (!actor)
                {
                    play.stop();
                    break;
                }
                const auto initial = actor->root_component()->local_transform();
                auto previous_feedback = play.feedback();
                play.tick(0.2);
                check(play.state() == EditorPlayState::Starting && !actor->has_begun_play() &&
                          actor->root_component()->local_transform().rotation == initial.rotation,
                      "BeginPlay waits for actual renderer preparation feedback");
                // This CPU fixture explicitly injects readiness. Renderer tests
                // separately verify publication/lifetime; this is not GPU evidence.
                previous_feedback->state.store(SceneRenderState::Ready, std::memory_order_release);
                play.tick(0.2);
                check(actor->has_begun_play() && play.world()->world_time_seconds() == 0.0,
                      "Preparation time is excluded from game time");
                play.tick(0.5);
                const auto rotated = actor->root_component()->local_transform();
                check(rotated.rotation != initial.rotation && rotated.translation == initial.translation &&
                          rotated.scale == initial.scale,
                      "Runtime Actor rotates and preserves translation/scale");
                if (iteration == 0)
                {
                    click_control(false);
                    check(play.take_action() == EditorPlayAction::Pause, "Viewport Pause button routes once");
                }
                play.pause();
                const auto time = play.world()->world_time_seconds();
                play.tick(1.0);
                check(play.state() == EditorPlayState::Paused && play.world()->world_time_seconds() == time &&
                          actor->root_component()->local_transform().rotation == rotated.rotation,
                      "Pause preserves runtime state and clock");
                if (iteration == 0)
                {
                    click_control(false);
                    check(play.take_action() == EditorPlayAction::Resume, "Viewport Resume button routes once");
                }
                play.resume();
                play.tick(0.1);
                check(play.world()->world_time_seconds() > time, "Resume advances game time");
                std::vector<SceneView> views;
                build_game_scene_views(*play.world(), views, {640, 360});
                check(views.size() == 1 && views.front().output_extent() == Extent{640, 360},
                      "Scene without Camera has shared Game/PIE fallback");
                if (iteration == 0)
                {
                    io.AddMousePosEvent(350, 320);
                    frame();
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                    frame();
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                    frame();
                    check(viewport.game_input_captured(), "Click game image captures input");
                    io.AddMousePosEvent(880, 620);
                    frame();
                    check(viewport.game_input_captured() && !viewport.game_mouse_input(),
                          "Leaving game image suppresses mouse while retaining focused keyboard input");
                    io.AddKeyEvent(ImGuiMod_Shift, true);
                    io.AddKeyEvent(ImGuiKey_F1, true);
                    frame();
                    check(!viewport.game_input_captured(), "Shift+F1 releases game image input");
                    io.AddKeyEvent(ImGuiKey_F1, false);
                    io.AddKeyEvent(ImGuiMod_Shift, false);
                    frame();
                    click_control(true);
                    check(play.take_action() == EditorPlayAction::Stop, "Viewport Stop button remains accessible");
                }
                check(play.stop() && !play.world() && !play.active() && scene.primitives == 0,
                      "Stop drains all owned proxies before deleting runtime resources");
                previous_feedback->state.store(SceneRenderState::Failed, std::memory_order_release);
                check(!play.active(), "Expired feedback cannot resurrect stopped session");
            }
            check(play.start(candidate, workspace, actors, programs, scene), "Start renderer failure fixture");
            if (const auto feedback = play.feedback())
            {
                feedback->error = "fixture upload failure";
                feedback->state.store(SceneRenderState::Failed, std::memory_order_release);
                play.tick(0.01);
            }
            check(!play.active() && !play.world() && play.error().find("fixture upload failure") != std::string::npos,
                  "Preparation failure destroys candidate without BeginPlay");
            check(play.start(candidate, workspace, actors, programs, scene), "Restart after preparation failure");
            play.request(EditorPlayAction::Stop);
            play.request(EditorPlayAction::Pause);
            check(play.take_action() == EditorPlayAction::Stop, "Stop wins concurrent UI requests");
            play.stop();
            ImGui::DestroyContext();
        }
        check(flush_rendering_commands().succeeded(), "Drain material release commands");
        check(rendering.stop().succeeded() && graph->shutdown(TaskGraphShutdownMode::Drain).succeeded(),
              "Shutdown fixture bridge and scheduler");
    }
    InputSystem::get_instance().exit();
    return passed;
}
