#include "engine.h"

#include "config/command_line_parser.h"
#include "gamescene/actor/actor.h"
#include "gamescene/component/scene_component.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/world/world.h"
#include "math/angle.h"
#include "math/quaternion.h"
#include "math/transform.h"
#include "platform/window_interface.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/material/material.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/texture/texture.h"
#include "format/shader_format_types.h"

#if WITH_WIN64
#include "platform/win/win32_window.h"
#include <windows.h>
#endif

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uint64_t k_destroy_actor_frame = 120u;
    constexpr float k_cube_half_extent = 0.45f;

    bool build_cube_test_arguments(
        int argc,
        char* argv[],
        std::vector<std::string>& arguments,
        bool& automated_window_events,
        bool& auto_close)
    {
        automated_window_events = false;
        auto_close = false;
        bool rendering_mode_overridden = false;
        arguments.clear();
        arguments.reserve(static_cast<std::size_t>(argc) + 1u);
        for (int index = 0; index < argc; ++index)
        {
            const std::string argument = argv[index] != nullptr
                ? argv[index]
                : std::string();
            constexpr const char* k_renderer_multithreaded_prefix =
                "--Renderer.MultiThreaded=";
            constexpr const char* k_plain_renderer_multithreaded_prefix =
                "Renderer.MultiThreaded=";
            if (argument.rfind(k_renderer_multithreaded_prefix, 0u) == 0u ||
                argument.rfind(
                    k_plain_renderer_multithreaded_prefix, 0u) == 0u)
            {
                rendering_mode_overridden = true;
            }

            constexpr const char* k_mode_prefix = "--cube-rendering-mode=";
            if (argument.rfind(k_mode_prefix, 0u) == 0u)
            {
                const std::string mode =
                    argument.substr(std::string(k_mode_prefix).size());
                if (mode == "single")
                {
                    arguments.push_back("--Renderer.MultiThreaded=false");
                    rendering_mode_overridden = true;
                    continue;
                }
                if (mode == "multi")
                {
                    arguments.push_back("--Renderer.MultiThreaded=true");
                    rendering_mode_overridden = true;
                    continue;
                }

                std::cerr << "Unknown cube rendering mode '" << mode
                          << "'. Expected 'single' or 'multi'.\n";
                return false;
            }

            constexpr const char* k_window_events_prefix =
                "--cube-window-events=";
            if (argument.rfind(k_window_events_prefix, 0u) == 0u)
            {
                const std::string events =
                    argument.substr(
                        std::string(k_window_events_prefix).size());
                if (events == "resize-minimize-restore")
                {
                    automated_window_events = true;
                    continue;
                }

                std::cerr << "Unknown cube window event mode '" << events
                          << "'. Expected 'resize-minimize-restore'.\n";
                return false;
            }

            constexpr const char* k_auto_close_prefix =
                "--cube-auto-close=";
            if (argument.rfind(k_auto_close_prefix, 0u) == 0u)
            {
                const std::string close_mode =
                    argument.substr(
                        std::string(k_auto_close_prefix).size());
                if (close_mode == "true" || close_mode == "on" ||
                    close_mode == "1")
                {
                    auto_close = true;
                    continue;
                }
                if (close_mode == "false" || close_mode == "off" ||
                    close_mode == "0")
                {
                    auto_close = false;
                    continue;
                }

                std::cerr << "Unknown cube auto close mode '" << close_mode
                          << "'. Expected true/on/1 or false/off/0.\n";
                return false;
            }

            arguments.push_back(argument);
        }
        if (!rendering_mode_overridden)
        {
            arguments.push_back("--Renderer.MultiThreaded=true");
        }
        return true;
    }

    toy3d::Quaternion make_axis_rotation(
        const toy3d::Vector3& axis,
        float radians)
    {
        toy3d::Quaternion rotation = toy3d::Quaternion::identity();
        if (!toy3d::try_make_quaternion_from_axis_angle(
                axis, toy3d::Radians(radians), rotation))
        {
            return toy3d::Quaternion::identity();
        }
        return rotation;
    }

    toy3d::Transform make_transform(
        const toy3d::Vector3& translation,
        const toy3d::Quaternion& rotation,
        float uniform_scale)
    {
        toy3d::Transform transform;
        transform.translation = translation;
        transform.rotation = rotation;
        transform.scale = toy3d::Vector3(uniform_scale);
        return transform;
    }

    toy3d::ShaderParameterId material_constant_id(const char* name)
    {
        return toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::Material,
            toy3d::shader::ShaderParameterCategory::Constant,
            name);
    }

    toy3d::ShaderParameterId material_texture_id(const char* name)
    {
        return toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::Material,
            toy3d::shader::ShaderParameterCategory::SampledTexture,
            name);
    }

    toy3d::TextureRef make_solid_texture(
        const std::array<std::uint8_t, 4>& rgba)
    {
        toy3d::TextureDesc desc;
        desc.width = 1u;
        desc.height = 1u;
        desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
        desc.row_pitches = {rgba.size()};
        desc.slice_pitches = {rgba.size()};
        desc.mip_pixels = {
            std::vector<std::uint8_t>(rgba.begin(), rgba.end())};
        return toy3d::Texture::create(std::move(desc));
    }

    toy3d::ShaderMapProgramRef load_phong_program()
    {
        toy3d::ShaderMapEntryLoader loader(
            toy3d::PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
        toy3d::ShaderMap shader_map(loader);
        toy3d::ShaderMapProgramKey key;
        key.shader_name = "Toy3d/Surface/Phong";
        key.pass_name = "Forward";
        key.platform = toy3d::ShaderPlatform::VulkanPortableV1;

        toy3d::ShaderMapProgramResult loaded = shader_map.find_or_load(key);
        if (!loaded.succeeded())
        {
            std::cerr << "Failed to load Phong shader: "
                      << loaded.error << '\n';
            return nullptr;
        }
        return std::move(loaded.program);
    }

    toy3d::MaterialInstanceRef make_cube_material(
        const toy3d::TextureRef& initial_texture)
    {
        if (!initial_texture)
        {
            return nullptr;
        }
        toy3d::MaterialDesc desc;
        desc.shader_name = "Toy3d/Surface/Phong";
        desc.shader_program = load_phong_program();
        if (!desc.shader_program)
        {
            return nullptr;
        }
        desc.vector4_defaults.emplace(
            material_constant_id("base_color"),
            toy3d::vec4(0.85f, 0.32f, 0.18f, 1.0f));
        desc.vector3_defaults.emplace(
            material_constant_id("directional_light_direction"),
            toy3d::vec3(0.35f, -0.55f, -0.75f));
        desc.vector4_defaults.emplace(
            material_constant_id("directional_light_color"),
            toy3d::vec4(1.0f, 0.96f, 0.88f, 1.0f));
        desc.vector4_defaults.emplace(
            material_constant_id("ambient_color"),
            toy3d::vec4(0.08f, 0.10f, 0.14f, 1.0f));
        desc.vector4_defaults.emplace(
            material_constant_id("specular_color"),
            toy3d::vec4(1.0f, 0.92f, 0.78f, 1.0f));
        desc.scalar_defaults.emplace(
            material_constant_id("specular_power"), 32.0f);
        desc.scalar_defaults.emplace(
            material_constant_id("specular_intensity"), 0.35f);
        desc.texture_defaults.emplace(
            material_texture_id("surface_tint_texture"),
            initial_texture);

        toy3d::MaterialRef material =
            toy3d::Material::create(std::move(desc));
        return material != nullptr
            ? toy3d::MaterialInstance::create(std::move(material))
            : nullptr;
    }

    void append_face(
        std::vector<toy3d::StaticMeshVertex>& vertices,
        std::vector<std::uint16_t>& indices,
        const toy3d::vec3& normal,
        const std::array<toy3d::vec3, 4>& positions)
    {
        const std::uint16_t first =
            static_cast<std::uint16_t>(vertices.size());
        vertices.push_back({positions[0], normal, toy3d::vec2(0.0f, 0.0f)});
        vertices.push_back({positions[1], normal, toy3d::vec2(1.0f, 0.0f)});
        vertices.push_back({positions[2], normal, toy3d::vec2(1.0f, 1.0f)});
        vertices.push_back({positions[3], normal, toy3d::vec2(0.0f, 1.0f)});
        indices.insert(indices.end(), {
            first,
            static_cast<std::uint16_t>(first + 1u),
            static_cast<std::uint16_t>(first + 2u),
            first,
            static_cast<std::uint16_t>(first + 2u),
            static_cast<std::uint16_t>(first + 3u)});
    }

    toy3d::StaticMeshRef make_cube_mesh(
        toy3d::MaterialInstanceRef material_instance)
    {
        if (!material_instance)
        {
            return nullptr;
        }

        const float h = k_cube_half_extent;
        std::vector<toy3d::StaticMeshVertex> vertices;
        std::vector<std::uint16_t> indices;
        vertices.reserve(24u);
        indices.reserve(36u);

        append_face(vertices, indices, toy3d::vec3(0.0f, 0.0f, 1.0f), {
            toy3d::vec3(-h, -h, h),
            toy3d::vec3(h, -h, h),
            toy3d::vec3(h, h, h),
            toy3d::vec3(-h, h, h)});
        append_face(vertices, indices, toy3d::vec3(0.0f, 0.0f, -1.0f), {
            toy3d::vec3(h, -h, -h),
            toy3d::vec3(-h, -h, -h),
            toy3d::vec3(-h, h, -h),
            toy3d::vec3(h, h, -h)});
        append_face(vertices, indices, toy3d::vec3(1.0f, 0.0f, 0.0f), {
            toy3d::vec3(h, -h, h),
            toy3d::vec3(h, -h, -h),
            toy3d::vec3(h, h, -h),
            toy3d::vec3(h, h, h)});
        append_face(vertices, indices, toy3d::vec3(-1.0f, 0.0f, 0.0f), {
            toy3d::vec3(-h, -h, -h),
            toy3d::vec3(-h, -h, h),
            toy3d::vec3(-h, h, h),
            toy3d::vec3(-h, h, -h)});
        append_face(vertices, indices, toy3d::vec3(0.0f, 1.0f, 0.0f), {
            toy3d::vec3(-h, h, h),
            toy3d::vec3(h, h, h),
            toy3d::vec3(h, h, -h),
            toy3d::vec3(-h, h, -h)});
        append_face(vertices, indices, toy3d::vec3(0.0f, -1.0f, 0.0f), {
            toy3d::vec3(-h, -h, -h),
            toy3d::vec3(h, -h, -h),
            toy3d::vec3(h, -h, h),
            toy3d::vec3(-h, -h, h)});

        toy3d::StaticMeshDesc desc;
        desc.vertices = std::move(vertices);
        // The mesh owns one fixed index-width variant, keeping the indexed draw
        // path explicit while the fixture stays small enough for UInt16.
        desc.indices = std::move(indices);
        desc.sections.push_back(
            {0u, 36u, 0u});
        desc.material_slots.push_back(std::move(material_instance));
        return toy3d::StaticMesh::create(std::move(desc));
    }

    class CubeRigActor final : public toy3d::Actor
    {
    public:
        CubeRigActor(
            toy3d::World& world,
            toy3d::StaticMeshRef mesh)
            : Actor(world),
              mesh_(std::move(mesh))
        {
            toy3d::SceneComponent& root =
                create_component<toy3d::SceneComponent>();
            set_root_component(&root);
            root_ = &root;
            root_->set_local_transform(make_transform(
                toy3d::Vector3(0.0f, 0.0f, 4.0f),
                toy3d::Quaternion::identity(),
                1.0f));

            const std::array<float, 3> offsets = {-1.5f, 0.0f, 1.5f};
            for (std::size_t index = 0; index < cubes_.size(); ++index)
            {
                toy3d::StaticMeshComponent& cube =
                    create_component<toy3d::StaticMeshComponent>();
                cube.attach_to(root_, toy3d::AttachmentRule::KeepRelative);
                cube.set_static_mesh(mesh_);
                cube.set_local_transform(make_transform(
                    toy3d::Vector3(offsets[index], 0.0f, 0.0f),
                    toy3d::Quaternion::identity(),
                    1.0f));
                cubes_[index] = &cube;
            }
            set_tick_enabled(true);
        }

    private:
        void tick(const toy3d::WorldTickContext& context) override
        {
            const float time =
                static_cast<float>(context.world_time_seconds);
            if (root_ != nullptr)
            {
                root_->set_local_transform(make_transform(
                    toy3d::Vector3(0.0f, 0.0f, 4.0f),
                    make_axis_rotation(
                        toy3d::Vector3(0.0f, 1.0f, 0.0f),
                        time),
                    1.0f));
            }

            for (std::size_t index = 0; index < cubes_.size(); ++index)
            {
                toy3d::StaticMeshComponent* const cube = cubes_[index];
                if (cube == nullptr)
                {
                    continue;
                }
                const float direction = index == 1u ? -1.0f : 1.0f;
                const float angle = time * (1.5f + 0.35f * index) * direction;
                cube->set_local_transform(make_transform(
                    cube->local_transform().translation,
                    make_axis_rotation(
                        toy3d::Vector3(1.0f, 0.0f, 0.0f),
                        angle),
                    1.0f));
            }

            if (cubes_[2] != nullptr)
            {
                cubes_[2]->set_visible(
                    context.frame_number % 48u < 36u);
            }
        }

        toy3d::StaticMeshRef mesh_;
        toy3d::SceneComponent* root_ = nullptr;
        std::array<toy3d::StaticMeshComponent*, 3> cubes_{};
    };

    struct CubeSceneState
    {
        toy3d::TextureRef warm_tint_texture;
        toy3d::TextureRef cool_tint_texture;
        toy3d::MaterialInstanceRef material_instance;
        toy3d::StaticMeshRef mesh;
        toy3d::Actor* actor = nullptr;
        float camera_x = 0.0f;
        bool setup_failed = false;
        bool actor_destroyed = false;
        bool material_released = false;
        bool resources_released = false;
        bool automated_window_events = false;
        bool auto_close = false;
    };

    bool flush_cube_shutdown_step(const char* step_name)
    {
        const toy3d::RenderFenceWaitResult flushed =
            toy3d::flush_rendering_commands();
        if (flushed.rendering_thread_reached())
        {
            return true;
        }

        std::cerr << "Failed to flush render commands during "
                  << step_name << ": "
                  << flushed.framework_status().message << '\n';
        return false;
    }

    bool release_cube_scene_resources(
        toy3d::World& world,
        CubeSceneState& state)
    {
        if (!state.actor_destroyed && state.actor != nullptr)
        {
            static_cast<void>(world.destroy_actor(*state.actor));
            state.actor = nullptr;
            state.actor_destroyed = true;
            if (!flush_cube_shutdown_step("Cube actor destruction"))
            {
                return false;
            }
        }

        if (state.actor_destroyed && !state.material_released)
        {
            state.mesh.reset();
            toy3d::MaterialInstance::release(state.material_instance);
            state.material_released = true;
            if (!flush_cube_shutdown_step("Cube material release"))
            {
                return false;
            }
        }

        if (state.material_released && !state.resources_released &&
            (!state.warm_tint_texture ||
             state.warm_tint_texture.use_count() == 1) &&
            (!state.cool_tint_texture ||
             state.cool_tint_texture.use_count() == 1))
        {
            toy3d::Texture::release(state.warm_tint_texture);
            toy3d::Texture::release(state.cool_tint_texture);
            state.resources_released = true;
        }

        return state.resources_released;
    }

    void apply_automated_window_event(
        toy3d::IWindow* window,
        std::uint64_t frame_number)
    {
        if (window == nullptr)
        {
            return;
        }

#if WITH_WIN64
        auto* const win32_window = dynamic_cast<toy3d::Win32Window*>(window);
        if (win32_window == nullptr ||
            win32_window->get_native_hwnd() == nullptr)
        {
            return;
        }

        HWND const hwnd = win32_window->get_native_hwnd();
        if (frame_number == 30u)
        {
            SetWindowPos(
                hwnd,
                nullptr,
                0,
                0,
                960,
                540,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
        if (frame_number == 60u)
        {
            ShowWindow(hwnd, SW_MINIMIZE);
            return;
        }
        if (frame_number == 90u)
        {
            ShowWindow(hwnd, SW_RESTORE);
            SetWindowPos(
                hwnd,
                nullptr,
                0,
                0,
                800,
                600,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
#else
        static_cast<void>(frame_number);
#endif
    }
}

int main(int argc, char* argv[])
{
    std::vector<std::string> command_line;
    bool automated_window_events = false;
    bool auto_close = false;
    if (!build_cube_test_arguments(
            argc, argv, command_line, automated_window_events, auto_close))
    {
        return 1;
    }
    toy3d::CommandLineParser::get_instance().parser_args(command_line);

    toy3d::Engine engine;
    toy3d::ShaderLoadConfig shader_config;
    shader_config.mode = toy3d::ShaderLoadMode::ShaderMapEntry;
    shader_config.path = toy3d::PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT);
    engine.set_shader_load_config(std::move(shader_config));

    CubeSceneState state;
    state.automated_window_events = automated_window_events;
    state.auto_close = auto_close;
    engine.set_world_setup_callback(
        [&state](toy3d::World& world)
        {
            state.warm_tint_texture = make_solid_texture(
                {255u, 238u, 210u, 255u});
            state.cool_tint_texture = make_solid_texture(
                {184u, 214u, 255u, 255u});
            state.material_instance = make_cube_material(
                state.warm_tint_texture);
            state.mesh = make_cube_mesh(state.material_instance);
            if (!state.warm_tint_texture || !state.cool_tint_texture ||
                !state.material_instance || !state.mesh)
            {
                state.setup_failed = true;
                return;
            }
            state.actor = &world.spawn_actor<CubeRigActor>(state.mesh);
        });
    engine.set_frame_callback(
        [&engine, &state](toy3d::World& world, double)
        {
            if (state.setup_failed)
            {
                if (engine.get_window() != nullptr)
                {
                    engine.get_window()->close();
                }
                return;
            }

            const float time =
                static_cast<float>(world.world_time_seconds());
            if (state.automated_window_events)
            {
                apply_automated_window_event(
                    engine.get_window(),
                    world.frame_number());
            }
            state.camera_x = 0.45f * std::sin(time * 0.55f);
            if (state.material_instance)
            {
                const toy3d::ShaderParameterId light_direction_id =
                    material_constant_id("directional_light_direction");
                const toy3d::ShaderParameterId specular_intensity_id =
                    material_constant_id("specular_intensity");
                const toy3d::ShaderParameterId texture_id =
                    material_texture_id("surface_tint_texture");
                const float light_x = 0.35f * std::sin(time * 0.7f);
                static_cast<void>(state.material_instance->set_vector(
                    light_direction_id,
                    toy3d::vec3(light_x, -0.55f, -0.75f)));
                static_cast<void>(state.material_instance->set_scalar(
                    specular_intensity_id,
                    0.25f + 0.20f * (0.5f + 0.5f * std::sin(time))));
                static_cast<void>(state.material_instance->set_texture(
                    texture_id,
                    world.frame_number() % 64u < 32u
                        ? state.warm_tint_texture
                        : state.cool_tint_texture));
            }

            if (!state.auto_close)
            {
                if (engine.get_window() != nullptr &&
                    engine.get_window()->should_close())
                {
                    static_cast<void>(
                        release_cube_scene_resources(world, state));
                }
                return;
            }

            if (!state.actor_destroyed &&
                world.frame_number() >= k_destroy_actor_frame &&
                state.actor != nullptr)
            {
                static_cast<void>(
                    release_cube_scene_resources(world, state));
                return;
            }

            if (state.material_released && !state.resources_released &&
                (!state.warm_tint_texture ||
                 state.warm_tint_texture.use_count() == 1) &&
                (!state.cool_tint_texture ||
                 state.cool_tint_texture.use_count() == 1))
            {
                toy3d::Texture::release(state.warm_tint_texture);
                toy3d::Texture::release(state.cool_tint_texture);
                state.resources_released = true;
                return;
            }

            if (state.resources_released && engine.get_window() != nullptr)
            {
                engine.get_window()->close();
            }
        });
    engine.set_scene_view_callback(
        [&state](
            std::vector<toy3d::SceneView>& views,
            const toy3d::Extent& extent)
        {
            views.emplace_back(
                toy3d::Vector3(state.camera_x, 1.5f, -6.0f),
                toy3d::Quaternion::identity(),
                toy3d::Vector3(0.0f, 0.0f, 1.0f),
                toy3d::UIntVector2(0, 0),
                toy3d::UIntVector2(extent.width, extent.height),
                toy3d::UIntVector2(extent.width, extent.height),
                toy3d::CameraProjectionMode::Perspective,
                toy3d::to_radians(toy3d::Degrees(60.0f)),
                0.1f,
                1000.0f);
        });

    engine.init(nullptr);
    engine.main_loop();
    engine.exit();
    return state.setup_failed ? 1 : 0;
}
