#include "renderscene/view/forward_scene_renderer.h"

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "logging/logger.h"
#include "math/matrix_construction.h"
#include "renderscene/builtin_mesh_pass_programs.h"
#include "renderscene/material/material_shader_bindings.h"
#include "rendercore/material/material_render_proxy.h"
#include "renderscene/object_shader_bindings.h"
#include "renderscene/pass/base_pass.h"
#include "renderscene/pass/hit_proxy_pass.h"
#include "renderscene/pass/shadow_pass.h"
#include "renderscene/render_scene.h"
#include "renderscene/primitive_scene_info.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "renderscene/scene_render_targets.h"
#include "renderscene/view/scene_visibility.h"
#include "renderscene/view/view_shader_bindings.h"
#include "rendercore/shader/shader_parameters.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"

namespace toy3d
{
    ForwardSceneRenderer::ForwardSceneRenderer(SceneViewFamily view_family, bool thumbnail_preview,
                                               bool preview_shadows)
        : SceneRenderer(std::move(view_family)), thumbnail_preview_(thumbnail_preview),
          preview_shadows_(preview_shadows)
    {
    }

    RHIStatus ForwardSceneRenderer::render_hit_proxy(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                                     const ShaderMapCollection& shader_map,
                                                     RHIGraphicsCommandContext& context,
                                                     const RHITextureViewRef& id_view,
                                                     const RHITextureViewRef& depth_view, HitProxyTable& table)
    {
        return render_hit_proxy_pass(device, shader_program_cache, shader_map, context, view_infos(), id_view,
                                     depth_view, table);
    }

    RHIStatus ForwardSceneRenderer::render_scene_passes(RenderScene& render_scene, RHIDevice& device,
                                                        RHIShaderProgramCache& shader_program_cache,
                                                        RHIGraphicsCommandContext& context,
                                                        SceneRenderTargets& scene_render_targets,
                                                        const BuiltinMeshPassPrograms& mesh_pass_programs)
    {
        const bool scene_targets_complete =
            scene_render_targets.scene_color_texture() && scene_render_targets.scene_color_view() &&
            scene_render_targets.scene_color_shader_resource_view() && scene_render_targets.scene_depth_texture() &&
            scene_render_targets.scene_depth_view() && scene_render_targets.scene_depth_shader_resource_view();
        if (!scene_targets_complete)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Forward scene passes require complete SceneRenderTargets.");
        }
        if (!init_views())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Forward scene passes rejected their SceneViewFamily inputs.");
        }

        const auto environment_status = render_scene.environment_status();
        if (!environment_status)
        {
            return environment_status;
        }
        const auto& environment = render_scene.environment_for_current_recording();
        const bool environment_active = environment.cube && environment.intensity > 0.0f;
        for (auto& view : view_infos())
        {
            view.environment_active_ = environment_active;
        }
        compute_scene_visibility(render_scene, view_infos());
        std::vector<const LightSceneData*> enabled_lights;
        for (const auto& light : render_scene.lights())
        {
            if (light->data.enabled)
            {
                enabled_lights.push_back(&light->data);
            }
        }
        // Priority selects the sole directional light; registration order only breaks ties.
        std::stable_sort(enabled_lights.begin(), enabled_lights.end(),
                         [](const LightSceneData* left, const LightSceneData* right)
                         {
                             return left->priority > right->priority;
                         });
        const LightSceneData* directional_light = nullptr;
        std::size_t directional_count = 0u;
        for (const LightSceneData* light : enabled_lights)
        {
            if (light->kind == LightKind::Directional)
            {
                if (!directional_light)
                {
                    directional_light = light;
                }
                ++directional_count;
            }
        }
        bool needs_pcf = false;
        for (const auto& view : view_infos())
        {
            for (const auto& batch : view.mesh_batches())
            {
                const auto& map = batch.material_render_proxy().shader_map();
                needs_pcf = needs_pcf || (map && map->features().shadows && map->index().policy.allow_pcf &&
                                          batch.scene_proxy().receives_shadows());
            }
        }
        ShadowRenderTargets& shadow_targets = scene_render_targets.shadow_targets();
        const LightSceneData* shadow_light = (!thumbnail_preview_ || preview_shadows_) && needs_pcf &&
                                                     directional_light && directional_light->cast_shadows &&
                                                     directional_light->intensity > 0.0f
                                                 ? directional_light
                                                 : nullptr;
        RHIStatus status = RHIStatus::success();
        if (shadow_light)
        {
            status = shadow_targets.ensure_views(device, view_infos().size(),
                                                 static_cast<std::size_t>(shadow_light->shadow_cascade_count),
                                                 static_cast<std::uint32_t>(shadow_light->shadow_map_resolution));
            if (!status)
            {
                return status;
            }
        }
        else
        {
            // Submitted command lists retain previous atlas references. The
            // current frame needs no allocation, clear pass or fake descriptor.
            shadow_targets.release();
        }
        status = compute_shadow_visibility(render_scene, shadow_light, view_infos(),
                                           shadow_light ? shadow_targets.layout().max_resolution : 0u);
        if (!status)
        {
            return status;
        }
        if (thumbnail_preview_)
        {
            // A missing draw must not publish a blank image as a valid cache.
            // Retry after the frame transaction rolls back pending uploads.
            if (view_infos().size() != 1)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Thumbnail requires exactly one view.");
            }
            std::size_t expected = 0;
            for (const auto* primitive : view_infos().front().visible_primitives())
            {
                const auto* proxy = primitive->proxy();
                if (proxy)
                {
                    expected += proxy->mesh_section_count();
                }
            }
            if (expected == 0 || view_infos().front().mesh_batches().size() != expected)
            {
                return RHIStatus::failure(RHIErrorCode::NotReady, "Thumbnail mesh is not completely drawable yet.");
            }
        }
        status = create_view_shader_bindings(device, context, view_infos());
        if (!status)
        {
            return status;
        }
        status = create_object_shader_bindings(device, context, view_infos());
        if (!status)
        {
            return status;
        }
        status = create_material_shader_bindings(device, context, view_infos());
        if (!status)
        {
            return status;
        }

        for (std::size_t view_index = 0; view_index < view_infos().size(); ++view_index)
        {
            if (!view_infos()[view_index].shadow_active())
            {
                continue;
            }
            status = render_shadow_pass(device, shader_program_cache, context, view_infos()[view_index], shadow_targets,
                                        view_index, mesh_pass_programs.shadow_depth_default);
            if (!status)
            {
                return status;
            }
        }

        std::vector<RHIResourceTransition> scene_attachment_transitions;
        if (scene_render_targets.scene_color_access() != RHIAccess::RenderTarget)
        {
            RHIResourceTransition scene_color_to_render_target;
            scene_color_to_render_target.resource = scene_render_targets.scene_color_texture();
            scene_color_to_render_target.subresources = scene_render_targets.scene_color_view()->desc().subresources;
            scene_color_to_render_target.before = scene_render_targets.scene_color_access();
            scene_color_to_render_target.after = RHIAccess::RenderTarget;
            scene_attachment_transitions.push_back(std::move(scene_color_to_render_target));
        }
        if (scene_render_targets.scene_depth_access() != RHIAccess::DepthStencilWrite)
        {
            RHIResourceTransition scene_depth_to_write;
            scene_depth_to_write.resource = scene_render_targets.scene_depth_texture();
            scene_depth_to_write.subresources = scene_render_targets.scene_depth_view()->desc().subresources;
            scene_depth_to_write.before = scene_render_targets.scene_depth_access();
            scene_depth_to_write.after = RHIAccess::DepthStencilWrite;
            scene_attachment_transitions.push_back(std::move(scene_depth_to_write));
        }
        if (!scene_attachment_transitions.empty())
        {
            status = context.transition_resources(scene_attachment_transitions);
            if (!status)
            {
                return status;
            }
        }

        std::vector<std::map<Sha256Hash, RHIBindingSetRef>> lighting_bindings(view_infos().size());
        bool needs_lighting_binding = false;
        bool needs_environment_sampler = false;
        for (const ViewInfo& view : view_infos())
        {
            for (const MeshBatch& batch : view.mesh_batches())
            {
                const auto selected = batch.material_program(view.shadow_active(), view.environment_active());
                const auto& program = selected.program;
                if (!program)
                {
                    continue;
                }
                for (const ShaderMapBinding& binding : program->data().bindings)
                {
                    if (binding.group == RHIBindingGroup::Pass)
                    {
                        needs_lighting_binding = true;
                        needs_environment_sampler = needs_environment_sampler || binding.name == "environment_sampler";
                    }
                }
            }
        }
        RHISamplerRef environment_sampler;
        if (needs_environment_sampler)
        {
            const auto sampler = render_scene.environment_sampler(device);
            if (!sampler)
            {
                return sampler.status();
            }
            environment_sampler = sampler.value();
        }
        // Empty/unlit draws do not require a lighting upload or a Pass binding.
        if (needs_lighting_binding)
        {
            constexpr std::size_t max_point_lights = Matrix4::k_column_count;
            std::size_t point_count = 0;
            for (std::size_t view_index = 0; view_index < view_infos().size(); ++view_index)
            {
                const ViewInfo& view = view_infos()[view_index];
                ForwardPassParameters lighting;
                lighting.scene_light_direction = Vector4(0, 0, -1, 0);
                lighting.scene_light_color = Vector4();
                lighting.point_light_positions = Matrix4::zero();
                lighting.point_light_colors = Matrix4::zero();
                lighting.point_light_count = 0.0f;
                if (environment_active)
                {
                    lighting.environment_world_to_cube = to_matrix4(conjugate(environment.rotation));
                    lighting.environment_parameters =
                        Vector4(environment.intensity,
                                static_cast<float>(environment.cube->desc().mip_pixels.size() - 1u), 0.0f, 0.0f);
                    lighting.environment_cube = render_scene.environment_view_for_current_recording();
                    lighting.environment_sampler = environment_sampler;
                }
                if (directional_light)
                {
                    const Vector3 radiance = directional_light->color * directional_light->intensity;
                    lighting.scene_light_direction =
                        Vector4(-directional_light->direction.x, -directional_light->direction.y,
                                -directional_light->direction.z, 0);
                    lighting.scene_light_color = Vector4(radiance, 0.0f);
                }
                point_count = 0u;
                for (const LightSceneData* light : enabled_lights)
                {
                    if (light->kind != LightKind::Point || light->intensity <= 0.0f)
                    {
                        continue;
                    }
                    if (point_count < max_point_lights)
                    {
                        const Vector3 radiance = light->color * light->intensity;
                        lighting.point_light_positions.at(point_count, 0) = light->position.x;
                        lighting.point_light_positions.at(point_count, 1) = light->position.y;
                        lighting.point_light_positions.at(point_count, 2) = light->position.z;
                        lighting.point_light_positions.at(point_count, 3) = light->range;
                        lighting.point_light_colors.at(point_count, 0) = radiance.x;
                        lighting.point_light_colors.at(point_count, 1) = radiance.y;
                        lighting.point_light_colors.at(point_count, 2) = radiance.z;
                    }
                    ++point_count;
                }
                lighting.point_light_count = static_cast<float>(std::min(point_count, max_point_lights));
                lighting.shadow_cascade_0_world_to_clip = view.shadow_cascade(0u).world_to_clip;
                lighting.shadow_cascade_1_world_to_clip = view.shadow_cascade(1u).world_to_clip;
                lighting.shadow_cascade_2_world_to_clip = view.shadow_cascade(2u).world_to_clip;
                lighting.shadow_distance_data =
                    Vector4(view.shadow_effective_end(), view.shadow_fade_start(),
                            view.shadow_active() ? static_cast<float>(view.shadow_cascade_count()) : 0.0f, 0.0f);
                lighting.shadow_split_data = Vector4(view.shadow_split(0u).x, view.shadow_split(0u).y,
                                                     view.shadow_split(1u).x, view.shadow_split(1u).y);
                lighting.shadow_receiver_parameters =
                    Vector4(view.shadow_cascade(0u).transition_scale, view.shadow_cascade(1u).transition_scale,
                            view.shadow_cascade(2u).transition_scale,
                            directional_light ? directional_light->shadow_receiver_bias : 0.0f);
                if (view.shadow_active())
                {
                    const ShadowAtlasLayout& atlas = shadow_targets.layout();
                    lighting.shadow_texel_size =
                        Vector4(1.0f / static_cast<float>(atlas.width), 1.0f / static_cast<float>(atlas.height),
                                static_cast<float>(atlas.width), static_cast<float>(atlas.height));
                    // Regions are pixel offsets and usable sizes. Inactive regions remain zero and are never sampled.
                    const auto tile_region = [](const ShadowCascadeTile& tile)
                    {
                        return Vector4(static_cast<float>(tile.x + ShadowCascadeTile::k_border),
                                       static_cast<float>(tile.y + ShadowCascadeTile::k_border),
                                       static_cast<float>(tile.resolution()), static_cast<float>(tile.resolution()));
                    };
                    lighting.shadow_cascade_0_region = tile_region(atlas.tiles[0]);
                    lighting.shadow_cascade_1_region = tile_region(atlas.tiles[1]);
                    lighting.shadow_cascade_2_region = tile_region(atlas.tiles[2]);
                    lighting.shadow_atlas = shadow_targets.shader_view(view_index);
                    lighting.shadow_sampler = shadow_targets.sampler();
                }
                for (const auto& batch : view.mesh_batches())
                {
                    const auto selected = batch.material_program(view.shadow_active(), view.environment_active());
                    if (!selected.succeeded())
                    {
                        return RHIStatus::failure(RHIErrorCode::Unsupported, selected.error);
                    }
                    ShaderParameterEncoder encoder(shader_parameters_metadata(lighting), selected.program->data());
                    if (!encoder.succeeded())
                    {
                        return RHIStatus::failure(RHIErrorCode::InvalidArgument, encoder.error());
                    }
                    const auto& metadata = encoder.binding_metadata();
                    if ((metadata.constant_buffer.size == 0u && metadata.resources.empty()) ||
                        lighting_bindings[view_index].count(metadata.group_identity) != 0u)
                    {
                        continue;
                    }
                    encode_shader_parameters(lighting, encoder);
                    auto created = create_transient_shader_binding(device, context, metadata, encoder);
                    if (!created)
                    {
                        return created.status();
                    }
                    lighting_bindings[view_index].emplace(metadata.group_identity, std::move(created).value());
                }
            }
            const bool overflow = point_count > max_point_lights || directional_count > 1;
            if (overflow && !render_scene.light_limit_reported())
            {
                TOY_LOG_WARN(
                    "Forward lighting uses only the highest-priority enabled directional light and {} point lights.",
                    max_point_lights);
            }
            render_scene.set_light_limit_reported(overflow);
        }
        BasePassInputs inputs{view_infos(), scene_render_targets.scene_color_view(),
                              scene_render_targets.scene_depth_view(), std::move(lighting_bindings)};
        if (thumbnail_preview_)
        {
            inputs.clear_color = vec4(0.025f, 0.025f, 0.025f, 1.0f);
            inputs.require_complete_meshes = true;
        }

        return render_base_pass(device, shader_program_cache, context, inputs);
    }

    bool ForwardSceneRenderer::init_views()
    {
        view_infos().clear();
        const Extent family_output_extent = view_family().output_extent();
        if (family_output_extent.width == 0 || family_output_extent.height == 0)
        {
            TOY_LOG_ERROR("ForwardSceneRenderer init_views requires a non-empty family output.");
            return false;
        }

        std::vector<ViewInfo> initialized_views;
        initialized_views.reserve(view_family().views().size());
        for (std::size_t view_index = 0; view_index < view_family().views().size(); ++view_index)
        {
            const SceneView& scene_view = view_family().views()[view_index];
            const Extent output_extent = scene_view.output_extent();
            const IntRect& view_rect = scene_view.view_rect();
            if (output_extent.width == 0 || output_extent.height == 0 || output_extent != family_output_extent)
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected View {} with invalid or inconsistent output size.",
                    view_index);
                return false;
            }
            if (view_rect.x < 0 || view_rect.y < 0 || view_rect.width == 0 || view_rect.height == 0)
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected View {} with an empty or out-of-bounds view rect.",
                    view_index);
                return false;
            }
            const std::uint32_t rect_x = static_cast<std::uint32_t>(view_rect.x);
            const std::uint32_t rect_y = static_cast<std::uint32_t>(view_rect.y);
            if (rect_x >= output_extent.width || rect_y >= output_extent.height ||
                view_rect.width > output_extent.width - rect_x || view_rect.height > output_extent.height - rect_y)
            {
                TOY_LOG_ERROR("ForwardSceneRenderer init_views rejected View {} with an out-of-bounds view rect.",
                              view_index);
                return false;
            }
            if (!is_finite(scene_view.camera_position()) || !is_finite(scene_view.camera_orientation()) ||
                !is_finite(scene_view.camera_direction()) || !is_finite(scene_view.near_clip()) ||
                scene_view.near_clip() <= 0.0f)
            {
                TOY_LOG_ERROR("ForwardSceneRenderer init_views rejected View {} with non-finite camera input or a "
                              "non-positive near plane.",
                              view_index);
                return false;
            }

            Matrix4 view_matrix;
            if (!try_make_view_matrix(scene_view.camera_position(), scene_view.camera_orientation(), view_matrix))
            {
                TOY_LOG_ERROR("ForwardSceneRenderer init_views failed to construct View {} world-to-view matrix.",
                              view_index);
                return false;
            }

            const float aspect = static_cast<float>(view_rect.width) / static_cast<float>(view_rect.height);
            Matrix4 projection_matrix;
            switch (scene_view.projection_mode())
            {
            case CameraProjectionMode::Perspective:
            {
                PerspectiveProjectionDesc projection_desc;
                projection_desc.vertical_fov = scene_view.vertical_fov();
                projection_desc.aspect = aspect;
                projection_desc.near_clip = scene_view.near_clip();
                projection_desc.far_clip = scene_view.far_clip();
                if (!try_make_perspective_projection(projection_desc, projection_matrix))
                {
                    TOY_LOG_ERROR("ForwardSceneRenderer init_views rejected View {} finite perspective inputs.",
                                  view_index);
                    return false;
                }
                break;
            }
            case CameraProjectionMode::PerspectiveInfiniteFar:
            {
                InfinitePerspectiveProjectionDesc projection_desc;
                projection_desc.vertical_fov = scene_view.vertical_fov();
                projection_desc.aspect = aspect;
                projection_desc.near_clip = scene_view.near_clip();
                if (!try_make_infinite_perspective_projection(projection_desc, projection_matrix))
                {
                    TOY_LOG_ERROR("ForwardSceneRenderer init_views rejected View {} infinite-far perspective inputs.",
                                  view_index);
                    return false;
                }
                break;
            }
            case CameraProjectionMode::Orthographic:
            case CameraProjectionMode::Custom:
            default:
                TOY_LOG_ERROR("ForwardSceneRenderer init_views rejected unsupported projection mode for View {}.",
                              view_index);
                return false;
            }

            const Matrix4 view_projection_matrix = projection_matrix * view_matrix;
            Matrix4 inverse_view_matrix;
            Matrix4 inverse_projection_matrix;
            Matrix4 inverse_view_projection_matrix;
            if (!is_finite(view_matrix) || !is_finite(projection_matrix) || !is_finite(view_projection_matrix) ||
                !try_inverse(view_matrix, inverse_view_matrix) ||
                !try_inverse(projection_matrix, inverse_projection_matrix) ||
                !try_inverse(view_projection_matrix, inverse_view_projection_matrix))
            {
                TOY_LOG_ERROR("ForwardSceneRenderer init_views rejected non-finite or non-invertible derived matrices "
                              "for View {}.",
                              view_index);
                return false;
            }

            ConvexVolume view_frustum;
            if (!try_make_reversed_z_frustum(view_projection_matrix, scene_view.infinite_far(), view_frustum))
            {
                TOY_LOG_ERROR("ForwardSceneRenderer init_views rejected degenerate frustum planes for View {}.",
                              view_index);
                return false;
            }

            ViewInfo initialized_view(scene_view, std::move(view_matrix), std::move(projection_matrix),
                                      view_projection_matrix, std::move(inverse_view_matrix),
                                      std::move(inverse_projection_matrix), std::move(inverse_view_projection_matrix),
                                      std::move(view_frustum));
            initialized_views.push_back(std::move(initialized_view));
        }

        // A fresh vector makes each View's current-frame visibility start empty;
        // publication happens only after every View has initialized successfully.
        view_infos() = std::move(initialized_views);
        return true;
    }
} // namespace toy3d
