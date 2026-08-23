#include "rendercore/scene_interface.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "rendercore/shader/primitive_uniform_shader_parameters.h"
#include "rendercore/shader/view_uniform_shader_parameters.h"
#include "rendercore/view/scene_view.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/render_command_internal.h"
#include "rendercore/rendering_thread.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/material/material.h"
#include "gamescene/world/world.h"
#include "renderscene/render_scene.h"
#include "renderscene/renderer.h"
#include "renderscene/view/forward_scene_renderer.h"
#include "task_graph/task_graph.h"
#include "threading/thread_manager.h"

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    static_assert(std::is_abstract<toy3d::SceneInterface>::value,
        "SceneInterface must remain an abstract bridge");
    static_assert(std::has_virtual_destructor<toy3d::SceneInterface>::value,
        "SceneInterface must support polymorphic destruction");
    static_assert(std::is_base_of<toy3d::SceneInterface, toy3d::RenderScene>::value,
        "RenderScene must expose only the SceneInterface bridge to Game-side code");
    static_assert(std::is_constructible<
            toy3d::RenderScene, toy3d::TaskGraphInterface&>::value,
        "RenderScene construction must receive the logical-thread contract");
    static_assert(std::is_constructible<
            toy3d::Renderer, toy3d::TaskGraphInterface&>::value,
        "Engine must create the Renderer shell with its Task Graph dependency");
    static_assert(!std::is_copy_constructible<toy3d::Renderer>::value,
        "Renderer ownership must remain unique");
    static_assert(!std::is_move_constructible<toy3d::Renderer>::value,
        "Renderer address must remain stable");
    static_assert(!std::is_copy_constructible<toy3d::RenderScene>::value,
        "RenderScene ownership must remain unique");
    static_assert(!std::is_move_constructible<toy3d::RenderScene>::value,
        "RenderScene address must remain stable");
    static_assert(!std::is_copy_constructible<toy3d::SceneViewFamily>::value,
        "SceneViewFamily must retain one-shot ownership");
    static_assert(std::is_move_constructible<toy3d::SceneViewFamily>::value,
        "SceneViewFamily must transfer into SceneRenderer ownership");
    static_assert(std::is_abstract<toy3d::SceneRenderer>::value,
        "SceneRenderer must remain the polymorphic one-shot render owner");
    static_assert(std::has_virtual_destructor<toy3d::SceneRenderer>::value,
        "SceneRenderer must support polymorphic logical-RT destruction");
    static_assert(std::is_final<toy3d::ForwardSceneRenderer>::value,
        "ForwardSceneRenderer must remain the concrete forward implementation");
    static_assert(std::is_standard_layout<
            toy3d::ViewUniformShaderParameters>::value,
        "View uniform parameters must remain a standard-layout CPU value");
    static_assert(std::is_standard_layout<
            toy3d::PrimitiveUniformShaderParameters>::value,
        "Primitive uniform parameters must remain a standard-layout CPU value");

    int failure_count = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    std::unique_ptr<toy3d::TaskGraphInterface> create_graph(
        toy3d::ThreadManager& thread_manager,
        bool multithreaded)
    {
        toy3d::TaskGraphCreateResult created = toy3d::create_task_graph(
            {multithreaded ? 1u : 0u, 64, multithreaded}, thread_manager);
        check(created.succeeded(), "Renderer fixture must create Task Graph");
        if (!created.succeeded())
        {
            return nullptr;
        }

        std::unique_ptr<toy3d::TaskGraphInterface> graph = created.take_task_graph();
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
            "Renderer fixture must attach GameThread");
        return graph;
    }

    void shutdown_graph(std::unique_ptr<toy3d::TaskGraphInterface>& graph)
    {
        if (graph)
        {
            check(graph->shutdown(
                    toy3d::TaskGraphShutdownMode::CancelPending).succeeded(),
                "Renderer fixture Task Graph must shut down");
            graph.reset();
        }
    }

    toy3d::StaticMeshRef make_mesh()
    {
        toy3d::MaterialDesc material_desc;
        material_desc.shader_name = "Builtin/Surface/Phong";
        const toy3d::MaterialInstanceRef material =
            toy3d::MaterialInstance::create(
                toy3d::Material::create(std::move(material_desc)));

        toy3d::StaticMeshDesc mesh_desc;
        mesh_desc.vertices = {
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}};
        mesh_desc.indices = std::vector<std::uint16_t>{0, 1, 2};
        mesh_desc.sections.push_back({0, 3, 0});
        mesh_desc.material_slots.push_back(material);
        return toy3d::StaticMesh::create(std::move(mesh_desc));
    }

    void test_canonical_uniform_parameter_values()
    {
        const toy3d::Matrix4 view_matrix(2.0f);
        const toy3d::Matrix4 projection_matrix(3.0f);
        const toy3d::Matrix4 view_projection_matrix(4.0f);
        const toy3d::Matrix4 inverse_view_matrix(5.0f);
        const toy3d::Matrix4 inverse_projection_matrix(6.0f);
        const toy3d::Matrix4 inverse_view_projection_matrix(7.0f);
        const toy3d::Vector3 camera_position(1.0f, 2.0f, 3.0f);
        const toy3d::Vector3 camera_direction(0.0f, 0.0f, 1.0f);
        const toy3d::ViewUniformShaderParameters view_parameters{
            view_matrix,
            projection_matrix,
            view_projection_matrix,
            inverse_view_matrix,
            inverse_projection_matrix,
            inverse_view_projection_matrix,
            camera_position,
            0.0f,
            camera_direction,
            0.0f};
        check(view_parameters.view_matrix == view_matrix &&
                view_parameters.projection_matrix == projection_matrix &&
                view_parameters.view_projection_matrix ==
                    view_projection_matrix &&
                view_parameters.inverse_view_matrix == inverse_view_matrix &&
                view_parameters.inverse_projection_matrix ==
                    inverse_projection_matrix &&
                view_parameters.inverse_view_projection_matrix ==
                    inverse_view_projection_matrix &&
                view_parameters.camera_position == camera_position &&
                view_parameters.camera_direction == camera_direction &&
                view_parameters.camera_position_padding == 0.0f &&
                view_parameters.camera_direction_padding == 0.0f,
            "View uniform parameters must preserve canonical matrices and camera values");

        toy3d::Matrix4 object_to_world = toy3d::Matrix4::identity();
        object_to_world.at(3, 0) = 2.0f;
        object_to_world.at(3, 1) = 3.0f;
        object_to_world.at(3, 2) = 4.0f;
        const toy3d::StaticMeshSceneProxy proxy(
            object_to_world,
            toy3d::AxisAlignedBounds{},
            true,
            nullptr,
            {});
        check(proxy.primitive_uniform_shader_parameters().object_to_world ==
                object_to_world,
            "Primitive uniform parameters must initialize from copied Proxy transform values");
    }

    void test_renderer_lifecycle(bool multithreaded)
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph =
            create_graph(thread_manager, multithreaded);
        if (!graph)
        {
            return;
        }

        toy3d::Renderer renderer(*graph);
        toy3d::Renderer* const stable_address = &renderer;
        toy3d::RenderingThread rendering_thread(
            thread_manager,
            *graph,
            multithreaded
                ? toy3d::RenderingThreadMode::MultiThread
                : toy3d::RenderingThreadMode::SingleThread);

        if (multithreaded)
        {
            const toy3d::ThreadStatus wrong_thread = renderer.initialize();
            check(wrong_thread.code == toy3d::ThreadErrorCode::InvalidCaller,
                "GT must not initialize multi-thread Renderer mutable state");
        }

        const toy3d::ThreadStatus started = rendering_thread.start(
            [&renderer, stable_address]()
            {
                check(&renderer == stable_address,
                    "Renderer address must remain stable during logical RT initialization");
                const toy3d::ThreadStatus initialized = renderer.initialize();
                check(initialized.succeeded(),
                    "Renderer must initialize on the logical Rendering Thread");
                check(renderer.initialize().code == toy3d::ThreadErrorCode::InvalidState,
                    "Renderer must reject repeated initialization");
                return initialized;
            });
        check(started.succeeded(),
            "RenderingThread bootstrap must initialize Renderer");

        toy3d::SceneInterface* const scene_interface = renderer.scene_interface();
        check(scene_interface != nullptr,
            "Renderer must publish its stable SceneInterface after initialization");

        toy3d::Vector3 camera_position(1.0f, 2.0f, 3.0f);
        toy3d::Quaternion camera_orientation = toy3d::Quaternion::identity();
        toy3d::Vector3 camera_direction(0.0f, 0.0f, 1.0f);
        toy3d::Radians vertical_fov(1.0f);
        std::vector<toy3d::SceneView> views;
        views.emplace_back(
            camera_position,
            camera_orientation,
            camera_direction,
            toy3d::UIntVector2(10, 20),
            toy3d::UIntVector2(640, 360),
            toy3d::UIntVector2(1280, 720),
            toy3d::CameraProjectionMode::Perspective,
            vertical_fov,
            0.25f,
            500.0f);
        toy3d::SceneViewFamily view_family(
            *scene_interface,
            toy3d::UIntVector2(1280, 720),
            std::move(views));
        camera_position.x = 99.0f;
        camera_orientation = toy3d::Quaternion(0.0f, 0.0f, 0.0f, 0.0f);
        camera_direction.z = -1.0f;
        vertical_fov = toy3d::Radians(2.0f);
        check(&view_family.scene_interface() == scene_interface &&
                view_family.output_size() == toy3d::UIntVector2(1280, 720) &&
                view_family.views().size() == 1 &&
                view_family.views()[0].camera_position() ==
                    toy3d::Vector3(1.0f, 2.0f, 3.0f) &&
                view_family.views()[0].camera_orientation() ==
                    toy3d::Quaternion::identity() &&
                view_family.views()[0].camera_direction() ==
                    toy3d::Vector3(0.0f, 0.0f, 1.0f) &&
                view_family.views()[0].view_rect_minimum() ==
                    toy3d::UIntVector2(10, 20) &&
                view_family.views()[0].view_rect_size() ==
                    toy3d::UIntVector2(640, 360) &&
                view_family.views()[0].output_size() ==
                    toy3d::UIntVector2(1280, 720) &&
                view_family.views()[0].projection_mode() ==
                    toy3d::CameraProjectionMode::Perspective &&
                view_family.views()[0].vertical_fov() ==
                    toy3d::Radians(1.0f) &&
                view_family.views()[0].near_clip() == 0.25f &&
                view_family.views()[0].far_clip() == 500.0f &&
                !view_family.views()[0].infinite_far(),
            "SceneViewFamily must own copied Camera, viewport, and projection values");
        renderer.draw_scene(
            std::make_unique<toy3d::ForwardSceneRenderer>(
                std::move(view_family)));
        check(toy3d::flush_rendering_commands().succeeded(),
            "Draw must execute and dispose its exclusive SceneRenderer on logical RT");
        toy3d::World world;
        toy3d::StaticMeshActor& actor =
            world.spawn_actor<toy3d::StaticMeshActor>();
        actor.static_mesh_component().set_static_mesh(make_mesh());
        check(world.scene_interface() == nullptr,
            "World must begin without a scene binding");
        check(world.bind_scene(*scene_interface)
                && world.scene_interface() == scene_interface,
            "World must bind the Renderer-owned SceneInterface non-owningly");
        check(actor.static_mesh_component().has_render_state(),
            "bind_scene must create render state for an existing registered Primitive");
        check(!world.bind_scene(*scene_interface),
            "World must reject a second scene binding");
        toy3d::Transform moved_transform;
        moved_transform.translation = {2.0f, 3.0f, 4.0f};
        check(actor.static_mesh_component().set_local_transform(moved_transform),
            "Primitive transform changes must produce an owned-value scene update");
        check(world.unbind_scene() && world.scene_interface() == nullptr,
            "World must clear its non-owning scene binding before Renderer teardown");
        check(!actor.static_mesh_component().has_render_state(),
            "unbind_scene must destroy Primitive render state before clearing the scene");
        check(!world.unbind_scene(),
            "World must diagnose unbind without an active scene binding");
        const toy3d::RenderFenceWaitResult drained =
            toy3d::flush_rendering_commands();
        check(drained.succeeded(),
            "Scene add/update/remove ownership must drain before Renderer teardown");

        toy3d::World terminal_world;
        toy3d::StaticMeshActor& terminal_actor =
            terminal_world.spawn_actor<toy3d::StaticMeshActor>();
        terminal_actor.static_mesh_component().set_static_mesh(make_mesh());
        toy3d::render_command_detail::disable_render_command_execution(*graph);
        std::vector<toy3d::SceneView> terminal_views;
        terminal_views.emplace_back(
            toy3d::Vector3(),
            toy3d::Quaternion::identity(),
            toy3d::Vector3(0.0f, 0.0f, 1.0f),
            toy3d::UIntVector2(),
            toy3d::UIntVector2(1, 1),
            toy3d::UIntVector2(1, 1),
            toy3d::CameraProjectionMode::Perspective,
            toy3d::Radians(1.0f),
            0.1f,
            100.0f);
        renderer.draw_scene(
            std::make_unique<toy3d::ForwardSceneRenderer>(
                toy3d::SceneViewFamily(
                    *scene_interface,
                    toy3d::UIntVector2(1, 1),
                    std::move(terminal_views))));
        check(terminal_world.bind_scene(*scene_interface),
            "Terminal admission window must still accept Add ownership");
        check(terminal_actor.static_mesh_component().has_render_state(),
            "Normal Add return must publish only opaque identity during terminal disposal");
        check(terminal_world.unbind_scene(),
            "Terminal admission window must accept Remove disposal ordering");
        check(!terminal_actor.static_mesh_component().has_render_state(),
            "Terminal Remove must clear opaque identity without dereferencing it");
        const toy3d::RenderFenceWaitResult terminal_drained =
            toy3d::flush_rendering_commands();
        check(terminal_drained.succeeded(),
            "Terminal-skipped Scene and SceneRenderer payloads must drain before Renderer teardown");

        const toy3d::ThreadStatus stopped = rendering_thread.stop(
            [&renderer, stable_address]()
            {
                check(&renderer == stable_address,
                    "Renderer address must remain stable during logical RT teardown");
                const toy3d::ThreadStatus torn_down = renderer.teardown();
                check(torn_down.succeeded(),
                    "Renderer must teardown on the logical Rendering Thread");
                check(renderer.teardown().code == toy3d::ThreadErrorCode::InvalidState,
                    "Renderer must reject repeated teardown");
                return torn_down;
            });
        check(stopped.succeeded(),
            "RenderingThread stop must teardown Renderer before returning");
        check(renderer.scene_interface() == nullptr,
            "Renderer must withdraw SceneInterface publication after teardown");
        check(&renderer == stable_address,
            "GT-owned Renderer shell address must remain stable for its full lifetime");

        shutdown_graph(graph);
    }
}

int main()
{
    test_canonical_uniform_parameter_values();
    test_renderer_lifecycle(true);
    test_renderer_lifecycle(false);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " Renderer scene ownership test(s) failed\n";
        return 1;
    }
    std::cout << "All Renderer scene ownership tests passed\n";
    return 0;
}
