#include "editor.h"

#include "imgui.h"

#include "file_system/physical_path.h"
#include "format/shader_binding_identity.h"
#include "format/shader_format_types.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/material/material.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map.h"
#include "ui/imgui_draw_data.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace toy3d
{
    namespace
    {
        std::uint32_t physical_extent(float logical, float scale)
        {
            const double value = static_cast<double>(logical) * static_cast<double>(scale);
            if (!std::isfinite(value) || value <= 0.0 ||
                value > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
            {
                return 0u;
            }
            return static_cast<std::uint32_t>(std::floor(value + 0.5));
        }

        StaticMeshRef make_preview_cube()
        {
            ShaderMapEntryLoader loader(PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
            ShaderMap shader_map(loader);
            ShaderMapProgramKey key;
            key.shader_name = "Toy3d/Surface/Phong";
            key.pass_name = "Forward";
            key.platform = ShaderPlatform::VulkanES31;
            ShaderMapProgramResult loaded = shader_map.find_or_load(key);
            if (!loaded.succeeded())
            {
                TOY_LOG_ERROR("Editor preview cube shader load failed: {}", loaded.error);
                return nullptr;
            }

            TextureDesc white_desc;
            white_desc.width = 1u;
            white_desc.height = 1u;
            white_desc.format = PixelFormat::R8G8B8A8UNorm;
            white_desc.row_pitches = {4u};
            white_desc.slice_pitches = {4u};
            white_desc.mip_pixels = {{255u, 255u, 255u, 255u}};
            TextureRef white_texture = Texture::create(std::move(white_desc));
            if (!white_texture)
                return nullptr;

            MaterialDesc material_desc;
            material_desc.shader_name = key.shader_name;
            material_desc.shader_program = std::move(loaded.program);
            material_desc.parameter_schema = material_parameter_schema_from_shader_schema(
                material_desc.shader_program->data().parameter_schema);
            material_desc.vector4_defaults.emplace(
                shader::make_shader_parameter_id(shader::BindingGroup::Material,
                                                  shader::ShaderParameterCategory::Constant, "base_color"),
                vec4(0.85f, 0.32f, 0.18f, 1.0f));
            const auto constant_id = [](const char* name)
            {
                return shader::make_shader_parameter_id(shader::BindingGroup::Material,
                                                        shader::ShaderParameterCategory::Constant, name);
            };
            material_desc.vector3_defaults.emplace(constant_id("directional_light_direction"),
                                                   vec3(0.35f, -0.55f, -0.75f));
            material_desc.vector4_defaults.emplace(constant_id("directional_light_color"),
                                                   vec4(1.0f, 0.96f, 0.88f, 1.0f));
            material_desc.vector4_defaults.emplace(constant_id("ambient_color"),
                                                   vec4(0.08f, 0.10f, 0.14f, 1.0f));
            material_desc.vector4_defaults.emplace(constant_id("specular_color"),
                                                   vec4(1.0f, 0.92f, 0.78f, 1.0f));
            material_desc.scalar_defaults.emplace(constant_id("specular_power"), 32.0f);
            material_desc.scalar_defaults.emplace(constant_id("specular_intensity"), 0.35f);
            material_desc.texture_defaults.emplace(
                shader::make_shader_parameter_id(shader::BindingGroup::Material,
                                                  shader::ShaderParameterCategory::SampledTexture,
                                                  "surface_tint_texture"),
                std::move(white_texture));
            material_desc.two_sided = true;
            MaterialInstanceRef material = MaterialInstance::create(Material::create(std::move(material_desc)));
            if (!material)
                return nullptr;

            constexpr float h = 0.75f;
            StaticMeshDesc mesh_desc;
            mesh_desc.vertices = {
                {{-h, -h, 3.0f - h}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
                {{ h, -h, 3.0f - h}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
                {{ h,  h, 3.0f - h}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
                {{-h,  h, 3.0f - h}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
                {{-h, -h, 3.0f + h}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                {{ h, -h, 3.0f + h}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                {{ h,  h, 3.0f + h}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                {{-h,  h, 3.0f + h}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}
            };
            // This small preview uses one explicit UInt16 index width.
            mesh_desc.indices = std::vector<std::uint16_t>{
                0u, 1u, 2u, 0u, 2u, 3u, 4u, 6u, 5u, 4u, 7u, 6u,
                0u, 4u, 5u, 0u, 5u, 1u, 1u, 5u, 6u, 1u, 6u, 2u,
                2u, 6u, 7u, 2u, 7u, 3u, 3u, 7u, 4u, 3u, 4u, 0u};
            mesh_desc.sections.push_back({0u, 36u, 0u});
            mesh_desc.material_slots.push_back(std::move(material));
            return StaticMesh::create(std::move(mesh_desc));
        }
    } // namespace

    bool EditorApplication::on_initialize()
    {
        if (ImGui::GetCurrentContext() == nullptr)
            return false;
        StaticMeshRef preview_mesh = make_preview_cube();
        if (!preview_mesh)
        {
            TOY_LOG_ERROR("Editor preview cube could not be created.");
            return false;
        }
        preview_material_ = preview_mesh->material_slots().front();
        preview_actor_ = &world().spawn_actor<StaticMeshActor>();
        preview_actor_->static_mesh_component().set_static_mesh(std::move(preview_mesh));
        return true;
    }

    void EditorApplication::on_shutdown()
    {
        if (preview_actor_ != nullptr)
        {
            preview_actor_->static_mesh_component().set_static_mesh(nullptr);
            preview_actor_ = nullptr;
        }
        const RenderFenceWaitResult drained = flush_rendering_commands();
        if (!drained.succeeded())
            TOY_LOG_ERROR("Editor preview scene could not drain before material release: {}",
                          drained.framework_status().message);
        MaterialInstance::release(preview_material_);
    }

    void EditorApplication::on_build_ui()
    {
        scene_extent_ = {};
        const ImGuiID dockspace = ImGui::DockSpaceOverViewport();
        // The first frame must give the scene panel a real viewport-sized region.
        ImGui::SetNextWindowDockID(dockspace, ImGuiCond_Always);
        const bool visible = ImGui::Begin("Game Viewport");
        if (visible)
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            scene_extent_.width = physical_extent(available.x, scale.x);
            scene_extent_.height = physical_extent(available.y, scale.y);
            if (scene_extent_.width != 0u && scene_extent_.height != 0u)
            {
                const std::uintptr_t id = static_cast<std::uintptr_t>(IMGUI_SCENE_VIEWPORT_TEXTURE_ID.value());
                ImGui::Image(reinterpret_cast<ImTextureID>(id), available);
            }
        }
        ImGui::End();
    }

    bool EditorApplication::on_scene_viewport_extent(Extent& extent) const
    {
        extent = scene_extent_;
        return true;
    }

    void EditorApplication::on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const
    {
        const Vector3 camera_position(3.0f, 2.5f, -6.0f);
        Vector3 camera_direction;
        Quaternion camera_orientation;
        if (!try_normalize(Vector3(0.0f, 0.0f, 3.0f) - camera_position, camera_direction) ||
            !try_make_rotation_from_forward_up(camera_direction, Vector3(0.0f, 1.0f, 0.0f), camera_orientation))
        {
            TOY_LOG_ERROR("Editor preview camera could not be constructed.");
            return;
        }
        views.emplace_back(camera_position, camera_orientation, camera_direction,
                           IntRect{0, 0, extent.width, extent.height}, extent, CameraProjectionMode::Perspective,
                           to_radians(Degrees(60.0f)), 0.1f, 1000.0f);
    }
} // namespace toy3d
