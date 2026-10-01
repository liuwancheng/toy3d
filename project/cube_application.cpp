#include "cube_application.h"
#include "gamescene/actor/light_actor.h"
#include "logging/logger.h"

#include "cube_actor.h"

#include "file_system/physical_path.h"
#include "format/shader_format_types.h"
#include "gamescene/world/world.h"
#include "math/angle.h"
#include "math/quaternion.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"

#if WITH_WIN64
#include "platform/win/win32_window.h"
#include <windows.h>
#elif WITH_MAC
#include "platform/mac/mac_window.h"
#include <GLFW/glfw3.h>
#endif

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include "imgui.h"

namespace
{
    constexpr std::uint64_t k_destroy_actor_frame = 120u;
    constexpr float k_cube_half_extent = 45.0f;

    toy3d::ShaderParameterId material_constant_id(const char* name)
    {
        return toy3d::shader::make_shader_parameter_id(toy3d::shader::BindingGroup::Material,
                                                       toy3d::shader::ShaderParameterCategory::Constant, name);
    }

    toy3d::ShaderParameterId material_texture_id(const char* name)
    {
        return toy3d::shader::make_shader_parameter_id(toy3d::shader::BindingGroup::Material,
                                                       toy3d::shader::ShaderParameterCategory::SampledTexture, name);
    }

    toy3d::TextureRef make_solid_texture(const std::array<std::uint8_t, 4>& rgba)
    {
        toy3d::TextureDesc desc;
        desc.width = 1u;
        desc.height = 1u;
        desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
        desc.row_pitches = {rgba.size()};
        desc.slice_pitches = {rgba.size()};
        desc.mip_pixels = {std::vector<std::uint8_t>(rgba.begin(), rgba.end())};
        return toy3d::Texture::create(std::move(desc));
    }

    toy3d::ShaderMapProgramRef load_phong_program()
    {
        toy3d::ShaderMapEntryLoader loader(toy3d::PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
        toy3d::ShaderMap shader_map(loader);
        toy3d::ShaderMapProgramKey key;
        key.shader_name = "Toy3d/Surface/Phong";
        key.pass_name = "Forward";
        key.platform = toy3d::ShaderPlatform::VulkanES31;

        toy3d::ShaderMapProgramResult loaded = shader_map.find_or_load(key);
        if (!loaded.succeeded())
        {
            std::cerr << "Failed to load Phong shader: " << loaded.error << '\n';
            return nullptr;
        }
        return std::move(loaded.program);
    }

    toy3d::MaterialInstanceRef make_cube_material(const toy3d::TextureRef& initial_texture)
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
        // Material defaults must use the Program's complete Material schema, not its active binding subset.
        desc.parameter_schema = toy3d::material_parameter_schema_from_shader_schema(
            desc.shader_program->data().parameter_schema);
        desc.vector4_defaults.emplace(material_constant_id("base_color"), toy3d::vec4(0.85f, 0.32f, 0.18f, 1.0f));
        desc.vector4_defaults.emplace(material_constant_id("ambient_color"), toy3d::vec4(0.08f, 0.10f, 0.14f, 1.0f));
        desc.vector4_defaults.emplace(material_constant_id("specular_color"), toy3d::vec4(1.0f, 0.92f, 0.78f, 1.0f));
        desc.scalar_defaults.emplace(material_constant_id("specular_power"), 32.0f);
        desc.scalar_defaults.emplace(material_constant_id("specular_intensity"), 0.35f);
        desc.vector2_defaults.emplace(material_constant_id("uv_scale"), toy3d::vec2(1.0f, 1.0f));
        desc.texture_defaults.emplace(material_texture_id("surface_tint_texture"), initial_texture);
        desc.sampler_defaults.emplace(toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::Material, toy3d::shader::ShaderParameterCategory::Sampler,
            "material_sampler"), toy3d::MaterialSamplerPreset::TrilinearWrap);

        toy3d::MaterialRef material = toy3d::Material::create(std::move(desc));
        return material != nullptr ? toy3d::MaterialInstance::create(std::move(material)) : nullptr;
    }

    void append_face(std::vector<toy3d::StaticMeshVertex>& vertices, std::vector<std::uint16_t>& indices,
                     const toy3d::vec3& normal, const std::array<toy3d::vec3, 4>& positions)
    {
        const std::uint16_t first = static_cast<std::uint16_t>(vertices.size());
        vertices.push_back({positions[0], normal, toy3d::vec2(0.0f, 0.0f)});
        vertices.push_back({positions[1], normal, toy3d::vec2(1.0f, 0.0f)});
        vertices.push_back({positions[2], normal, toy3d::vec2(1.0f, 1.0f)});
        vertices.push_back({positions[3], normal, toy3d::vec2(0.0f, 1.0f)});
        indices.insert(indices.end(),
                       {first, static_cast<std::uint16_t>(first + 1u), static_cast<std::uint16_t>(first + 2u), first,
                        static_cast<std::uint16_t>(first + 2u), static_cast<std::uint16_t>(first + 3u)});
    }

    toy3d::StaticMeshRef make_cube_mesh(toy3d::MaterialInstanceRef material_instance)
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

        append_face(vertices, indices, toy3d::vec3(0.0f, 0.0f, 1.0f),
                    {toy3d::vec3(-h, -h, h), toy3d::vec3(h, -h, h), toy3d::vec3(h, h, h), toy3d::vec3(-h, h, h)});
        append_face(vertices, indices, toy3d::vec3(0.0f, 0.0f, -1.0f),
                    {toy3d::vec3(h, -h, -h), toy3d::vec3(-h, -h, -h), toy3d::vec3(-h, h, -h), toy3d::vec3(h, h, -h)});
        append_face(vertices, indices, toy3d::vec3(1.0f, 0.0f, 0.0f),
                    {toy3d::vec3(h, -h, h), toy3d::vec3(h, -h, -h), toy3d::vec3(h, h, -h), toy3d::vec3(h, h, h)});
        append_face(vertices, indices, toy3d::vec3(-1.0f, 0.0f, 0.0f),
                    {toy3d::vec3(-h, -h, -h), toy3d::vec3(-h, -h, h), toy3d::vec3(-h, h, h), toy3d::vec3(-h, h, -h)});
        append_face(vertices, indices, toy3d::vec3(0.0f, 1.0f, 0.0f),
                    {toy3d::vec3(-h, h, h), toy3d::vec3(h, h, h), toy3d::vec3(h, h, -h), toy3d::vec3(-h, h, -h)});
        append_face(vertices, indices, toy3d::vec3(0.0f, -1.0f, 0.0f),
                    {toy3d::vec3(-h, -h, -h), toy3d::vec3(h, -h, -h), toy3d::vec3(h, -h, h), toy3d::vec3(-h, -h, h)});

        toy3d::StaticMeshDesc desc;
        desc.vertices = std::move(vertices);
        // The fixture stays small enough for one explicit UInt16 index variant.
        desc.indices = std::move(indices);
        desc.sections.push_back({0u, 36u, 0u});
        desc.material_slots.push_back(std::move(material_instance));
        return toy3d::StaticMesh::create(std::move(desc));
    }

    bool flush_cube_shutdown_step(const char* step_name)
    {
        const toy3d::RenderFenceWaitResult flushed = toy3d::flush_rendering_commands();
        if (flushed.rendering_thread_reached())
        {
            return true;
        }

        std::cerr << "Failed to flush render commands during " << step_name << ": "
                  << flushed.framework_status().message << '\n';
        return false;
    }

#if WITH_WIN64
    bool report_win32_window_result(BOOL result, const char* operation)
    {
        if (result != FALSE)
        {
            return true;
        }
        std::cerr << "Failed to " << operation << " the Win32 Cube window: error " << GetLastError() << '\n';
        return false;
    }
#elif WITH_MAC
    void clear_glfw_error()
    {
        // The next query must describe only the automated operation under test.
        static_cast<void>(glfwGetError(nullptr));
    }

    bool report_glfw_window_result(const char* operation)
    {
        const char* glfw_error = nullptr;
        if (glfwGetError(&glfw_error) == GLFW_NO_ERROR)
        {
            return true;
        }
        std::cerr << "Failed to " << operation
                  << " the macOS Cube window: " << (glfw_error != nullptr ? glfw_error : "unknown GLFW error") << '\n';
        return false;
    }
#endif

    bool apply_automated_window_event(toy3d::IWindow* window, std::uint64_t frame_number)
    {
        if (window == nullptr)
        {
            return false;
        }

#if WITH_WIN64
        auto* const win32_window = dynamic_cast<toy3d::Win32Window*>(window);
        if (win32_window == nullptr || win32_window->get_native_hwnd() == nullptr)
        {
            return false;
        }

        HWND const hwnd = win32_window->get_native_hwnd();
        if (frame_number == 30u)
        {
            return report_win32_window_result(
                SetWindowPos(hwnd, nullptr, 0, 0, 960, 540, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), "resize");
        }
        if (frame_number == 60u)
        {
            ShowWindow(hwnd, SW_MINIMIZE);
            return true;
        }
        if (frame_number == 90u)
        {
            ShowWindow(hwnd, SW_RESTORE);
            return report_win32_window_result(
                SetWindowPos(hwnd, nullptr, 0, 0, 800, 600, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
                "restore and resize");
        }
#elif WITH_MAC
        auto* const mac_window = dynamic_cast<toy3d::MacWindow*>(window);
        if (mac_window == nullptr || mac_window->get_glfw_window() == nullptr)
        {
            return false;
        }

        GLFWwindow* const glfw_window = mac_window->get_glfw_window();
        if (frame_number == 30u)
        {
            clear_glfw_error();
            glfwSetWindowSize(glfw_window, 960, 540);
            return report_glfw_window_result("resize");
        }
        if (frame_number == 60u)
        {
            clear_glfw_error();
            glfwIconifyWindow(glfw_window);
            return report_glfw_window_result("minimize");
        }
        if (frame_number == 90u)
        {
            clear_glfw_error();
            glfwRestoreWindow(glfw_window);
            if (!report_glfw_window_result("restore"))
            {
                return false;
            }
            clear_glfw_error();
            glfwSetWindowSize(glfw_window, 800, 600);
            return report_glfw_window_result("restore and resize");
        }
#else
        static_cast<void>(frame_number);
#endif
        return true;
    }
} // namespace

CubeApplication::CubeApplication(bool automated_window_events, bool auto_close)
    : automated_window_events_(automated_window_events), auto_close_(auto_close)
{
}

bool CubeApplication::on_initialize()
{
    warm_tint_texture_ = make_solid_texture({255u, 238u, 210u, 255u});
    cool_tint_texture_ = make_solid_texture({184u, 214u, 255u, 255u});
    material_instance_ = make_cube_material(warm_tint_texture_);
    mesh_ = make_cube_mesh(material_instance_);
    if (!warm_tint_texture_ || !cool_tint_texture_ || !material_instance_ || !mesh_)
    {
        setup_failed_ = true;
        return false;
    }

    actor_ = &world().spawn_actor<CubeActor>(mesh_);
    light_actor_ = &world().spawn_actor<toy3d::DirectionalLightActor>();
    toy3d::Transform light_transform;
    if (!toy3d::try_make_rotation_from_forward_up(toy3d::Vector3(-0.35f, 0.55f, 0.75f),
                                                  toy3d::Vector3(0, 1, 0), light_transform.rotation) ||
        !light_actor_->root_component()->set_local_transform(light_transform)) return false;
    return true;
}

void CubeApplication::on_tick(double delta_time)
{
    static_cast<void>(delta_time);

    const float time = static_cast<float>(world().world_time_seconds());
    if (automated_window_events_ && !apply_automated_window_event(&window(), world().frame_number()))
    {
        setup_failed_ = true;
        window().close();
        return;
    }
    if (animate_camera_)
    {
        camera_x_ = 45.0f * std::sin(time * 0.55f);
    }
    if (animate_material_ && material_instance_)
    {
        const float light_x = 0.35f * std::sin(time * 0.7f);
        if (light_actor_)
        {
            toy3d::Transform light_transform = light_actor_->root_component()->local_transform();
            if (!toy3d::try_make_rotation_from_forward_up(toy3d::Vector3(-light_x, 0.55f, 0.75f),
                                                         toy3d::Vector3(0, 1, 0), light_transform.rotation) ||
                !light_actor_->root_component()->set_local_transform(light_transform))
                TOY_LOG_ERROR("Cube sample light animation failed.");
        }
        // C++17 string_view resolves the schema name synchronously; the literal never crosses threads.
        static_cast<void>(
            material_instance_->set_scalar("specular_intensity", 0.25f + 0.20f * (0.5f + 0.5f * std::sin(time))));
        static_cast<void>(material_instance_->set_texture(
            "surface_tint_texture", world().frame_number() % 64u < 32u ? warm_tint_texture_ : cool_tint_texture_));
    }

    if (!auto_close_)
    {
        if (window().should_close())
        {
            static_cast<void>(release_scene_resources());
        }
        return;
    }

    if (!actor_destroyed_ && world().frame_number() >= k_destroy_actor_frame)
    {
        static_cast<void>(release_scene_resources());
        return;
    }

    if (material_released_ && !resources_released_ && (!warm_tint_texture_ || warm_tint_texture_.use_count() == 1) &&
        (!cool_tint_texture_ || cool_tint_texture_.use_count() == 1))
    {
        toy3d::Texture::release(warm_tint_texture_);
        toy3d::Texture::release(cool_tint_texture_);
        resources_released_ = true;
        return;
    }

    if (resources_released_)
    {
        window().close();
    }
}

void CubeApplication::on_build_ui()
{
    ImGui::SetNextWindowSize(ImVec2(340.0F, 0.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Toy3d Cube Controls"))
    {
        ImGui::Text("Frame: %llu", static_cast<unsigned long long>(world().frame_number()));
        ImGui::Text("World time: %.2f s", world().world_time_seconds());
        ImGui::Text("Camera X: %.3f", camera_x_);
        ImGui::Separator();
        ImGui::Checkbox("Animate camera", &animate_camera_);
        ImGui::Checkbox("Animate material", &animate_material_);
        ImGui::Checkbox("Show diagnostics", &show_diagnostics_);
        if (ImGui::Button("Center camera"))
        {
            animate_camera_ = false;
            camera_x_ = 0.0F;
        }
    }
    ImGui::End();

    if (show_diagnostics_)
    {
        if (ImGui::Begin("Cube Diagnostics", &show_diagnostics_))
        {
            const toy3d::Extent framebuffer = window().get_framebuffer_size();
            ImGui::TextUnformatted("UI generated by Dear ImGui core");
            ImGui::TextUnformatted("GPU rendering owned by Toy3d RHI");
            ImGui::Text("Framebuffer: %u x %u", framebuffer.width, framebuffer.height);
        }
        ImGui::End();
    }
}

void CubeApplication::on_build_scene_views(std::vector<toy3d::SceneView>& views, const toy3d::Extent& extent) const
{
    views.emplace_back(toy3d::Vector3(camera_x_, 150.0f, -600.0f), toy3d::Quaternion::identity(),
                       toy3d::Vector3(0.0f, 0.0f, 1.0f), toy3d::IntRect{0, 0, extent.width, extent.height}, extent,
                       toy3d::CameraProjectionMode::Perspective, toy3d::to_radians(toy3d::Degrees(60.0f)), 10.0f,
                       100000.0f);
}

void CubeApplication::on_shutdown()
{
    static_cast<void>(release_scene_resources());
    actor_ = nullptr;
}

bool CubeApplication::release_scene_resources()
{
    if (!actor_destroyed_)
    {
        if (actor_ != nullptr)
        {
            static_cast<void>(world().destroy_actor(*actor_));
            actor_ = nullptr;
            if (!flush_cube_shutdown_step("Cube actor destruction"))
            {
                return false;
            }
        }
        if (light_actor_)
        {
            if (!world().destroy_actor(*light_actor_)) return false;
            light_actor_ = nullptr;
        }
        actor_destroyed_ = true;
    }

    if (!material_released_)
    {
        mesh_.reset();
        toy3d::MaterialInstance::release(material_instance_);
        material_released_ = true;
        if (!flush_cube_shutdown_step("Cube material release"))
        {
            return false;
        }
    }

    if (!resources_released_ && (!warm_tint_texture_ || warm_tint_texture_.use_count() == 1) &&
        (!cool_tint_texture_ || cool_tint_texture_.use_count() == 1))
    {
        toy3d::Texture::release(warm_tint_texture_);
        toy3d::Texture::release(cool_tint_texture_);
        resources_released_ = true;
    }

    return resources_released_;
}
