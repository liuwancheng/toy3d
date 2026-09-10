#include "renderscene/view/forward_scene_renderer.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "logging/logger.h"
#include "math/matrix_construction.h"
#include "renderscene/material/material_shader_bindings.h"
#include "renderscene/object_shader_bindings.h"
#include "renderscene/pass/base_pass.h"
#include "renderscene/render_scene.h"
#include "renderscene/scene_render_targets.h"
#include "renderscene/view/scene_visibility.h"
#include "renderscene/view/view_shader_bindings.h"

namespace toy3d
{
    ForwardSceneRenderer::ForwardSceneRenderer(SceneViewFamily view_family) : SceneRenderer(std::move(view_family)) {}

    RHIStatus ForwardSceneRenderer::render_scene_passes(RenderScene& render_scene, RHIDevice& device,
                                                        RHIShaderProgramCache& shader_program_cache,
                                                        RHIGraphicsCommandContext& context,
                                                        SceneRenderTargets& scene_render_targets)
    {
        const bool scene_targets_complete = scene_render_targets.scene_color_texture() &&
                                            scene_render_targets.scene_color_view() &&
                                            scene_render_targets.scene_color_shader_resource_view() &&
                                            scene_render_targets.scene_depth_texture() &&
                                            scene_render_targets.scene_depth_view() &&
                                            scene_render_targets.scene_depth_shader_resource_view();
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

        compute_scene_visibility(render_scene, view_infos());
        RHIStatus status = create_view_shader_bindings(device, context, view_infos());
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

        BasePassInputs inputs{view_infos(), scene_render_targets.scene_color_view(),
                              scene_render_targets.scene_depth_view()};
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
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected View {} with an out-of-bounds view rect.", view_index);
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
