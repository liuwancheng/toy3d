#include "renderscene/view/scene_visibility.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

#include "logging/logger.h"
#include "math/matrix_construction.h"
#include "math/vector3.h"
#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/scene/light_scene_proxy.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "renderscene/geometry/static_mesh_render_data.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/primitive_scene_info.h"
#include "renderscene/render_scene.h"
#include "renderscene/shadow_render_targets.h"
#include "renderscene/view/view_info.h"

namespace toy3d
{
    namespace
    {
        std::array<Vector3, 8> bounds_corners(const Vector3& minimum, const Vector3& maximum)
        {
            return {{{minimum.x, minimum.y, minimum.z}, {maximum.x, minimum.y, minimum.z},
                     {minimum.x, maximum.y, minimum.z}, {maximum.x, maximum.y, minimum.z},
                     {minimum.x, minimum.y, maximum.z}, {maximum.x, minimum.y, maximum.z},
                     {minimum.x, maximum.y, maximum.z}, {maximum.x, maximum.y, maximum.z}}};
        }

        struct BasisBounds
        {
            float min_x = std::numeric_limits<float>::max();
            float max_x = -std::numeric_limits<float>::max();
            float min_y = std::numeric_limits<float>::max();
            float max_y = -std::numeric_limits<float>::max();
            float min_z = std::numeric_limits<float>::max();
            float max_z = -std::numeric_limits<float>::max();
        };

        void include_basis_point(BasisBounds& bounds, const Vector3& point, const Vector3& right,
                                 const Vector3& up, const Vector3& forward)
        {
            const float x = dot(right, point);
            const float y = dot(up, point);
            const float z = dot(forward, point);
            bounds.min_x = std::min(bounds.min_x, x);
            bounds.max_x = std::max(bounds.max_x, x);
            bounds.min_y = std::min(bounds.min_y, y);
            bounds.max_y = std::max(bounds.max_y, y);
            bounds.min_z = std::min(bounds.min_z, z);
            bounds.max_z = std::max(bounds.max_z, z);
        }

        bool finite_basis_bounds(const BasisBounds& bounds)
        {
            return is_finite(bounds.min_x) && is_finite(bounds.max_x) &&
                   is_finite(bounds.min_y) && is_finite(bounds.max_y) &&
                   is_finite(bounds.min_z) && is_finite(bounds.max_z);
        }
    } // namespace

    void compute_scene_visibility(const RenderScene& render_scene, std::vector<ViewInfo>& view_infos)
    {
        for (ViewInfo& view_info : view_infos)
        {
            std::vector<PrimitiveSceneInfo*>& visible_primitives = view_info.visible_primitives_;
            std::vector<MeshBatch>& mesh_batches = view_info.mesh_batches_;
            visible_primitives.clear();
            mesh_batches.clear();
            visible_primitives.reserve(render_scene.primitive_scene_infos().size());

            for (const std::unique_ptr<PrimitiveSceneInfo>& primitive_info : render_scene.primitive_scene_infos())
            {
                if (!primitive_info)
                {
                    continue;
                }

                PrimitiveSceneProxy* const proxy = primitive_info->proxy();
                if (proxy == nullptr || !proxy->visible())
                {
                    continue;
                }

                const AxisAlignedBounds& bounds = proxy->world_bounds();
                const Vector3 minimum(bounds.minimum.x, bounds.minimum.y, bounds.minimum.z);
                const Vector3 maximum(bounds.maximum.x, bounds.maximum.y, bounds.maximum.z);
                if (!is_finite(minimum) || !is_finite(maximum) || minimum.x > maximum.x ||
                    minimum.y > maximum.y || minimum.z > maximum.z)
                {
                    TOY_LOG_ERROR("Scene visibility skipped a Primitive with invalid world bounds.");
                    continue;
                }
                if (!view_info.view_frustum().intersects_axis_aligned_bounds(minimum, maximum))
                {
                    continue;
                }

                visible_primitives.push_back(primitive_info.get());
            }

            mesh_batches.reserve(visible_primitives.size());
            for (PrimitiveSceneInfo* const primitive_info : visible_primitives)
            {
                if (primitive_info == nullptr)
                {
                    continue;
                }

                const auto* const static_mesh_proxy =
                    dynamic_cast<const StaticMeshSceneProxy*>(primitive_info->proxy());
                if (static_mesh_proxy == nullptr)
                {
                    continue;
                }

                StaticMeshRenderData* const render_data = static_mesh_proxy->render_data();
                if (render_data == nullptr)
                {
                    TOY_LOG_ERROR("Scene visibility skipped a visible StaticMesh with no StaticMeshRenderData.");
                    continue;
                }
                const RHIStatus prepared_render_data = render_data->prepare_current_recording();
                if (!prepared_render_data)
                {
                    TOY_LOG_ERROR(
                        "Scene visibility skipped a visible StaticMesh whose render data is not ready in the "
                        "current recording: {}",
                        prepared_render_data.message());
                    continue;
                }
                if (!render_data->is_drawable())
                {
                    TOY_LOG_ERROR("Scene visibility skipped a visible StaticMesh whose complete render-data gate "
                                  "is not drawable.");
                    continue;
                }

                const LocalVertexFactory* const vertex_factory = render_data->vertex_factory();
                if (vertex_factory == nullptr)
                {
                    TOY_LOG_ERROR("Scene visibility skipped a visible StaticMesh with no LocalVertexFactory.");
                    continue;
                }

                const std::vector<MaterialRenderProxy*>& material_proxies =
                    static_mesh_proxy->material_render_proxies();
                const std::vector<StaticMeshSection>& sections = render_data->sections();
                for (std::size_t section_index = 0; section_index < sections.size(); ++section_index)
                {
                    if (section_index > std::numeric_limits<std::uint32_t>::max())
                    {
                        TOY_LOG_ERROR("Scene visibility skipped a StaticMesh section whose index exceeds uint32.");
                        continue;
                    }
                    const StaticMeshSection& section = sections[section_index];
                    const std::size_t first_index = section.first_index;
                    const std::size_t index_count = section.index_count;
                    if (index_count == 0u || index_count % 3u != 0u || first_index > render_data->index_count() ||
                        index_count > render_data->index_count() - first_index)
                    {
                        TOY_LOG_ERROR("Scene visibility skipped StaticMesh section {} with an invalid index range.",
                                      section_index);
                        continue;
                    }
                    if (section.material_slot >= material_proxies.size())
                    {
                        TOY_LOG_ERROR(
                            "Scene visibility skipped StaticMesh section {} whose Material slot is out of range.",
                            section_index);
                        continue;
                    }

                    MaterialRenderProxy* const material_proxy = material_proxies[section.material_slot];
                    if (material_proxy == nullptr || !material_proxy->shader_program())
                    {
                        TOY_LOG_ERROR(
                            "Scene visibility skipped StaticMesh section {} with no usable Material representation.",
                            section_index);
                        continue;
                    }

                    mesh_batches.emplace_back(*static_mesh_proxy, *render_data, *vertex_factory, *material_proxy,
                                              section.first_index, section.index_count,
                                              static_cast<std::uint32_t>(section_index));
                }
            }
        }
    }

    RHIStatus compute_shadow_visibility(const RenderScene& render_scene, const LightSceneData* directional_light,
                                        std::vector<ViewInfo>& view_infos)
    {
        constexpr float k_parallel_up_threshold = 0.99f;
        constexpr float k_depth_padding_fraction = 0.01f;
        constexpr float k_min_depth_padding = 0.1f;
        constexpr float k_max_slope = 4.0f;
        constexpr float k_constant_bias_texels = 2.0f;
        constexpr float k_slope_bias_texels = 4.0f;
        constexpr float k_max_bias_texels = 5.0f;
        constexpr float k_max_normalized_bias = 0.01f;
        constexpr float k_near_cascade_fraction = 0.25f;
        constexpr float k_cascade_blend_fraction = 0.05f;
        for (ViewInfo& view : view_infos)
        {
            for (ShadowCascadeInfo& cascade : view.shadow_cascades_) cascade = ShadowCascadeInfo();
            view.shadow_active_ = false;
            view.shadow_effective_end_ = 0.0f;
            view.shadow_fade_start_ = 0.0f;
            view.shadow_split_start_ = 0.0f;
            view.shadow_split_end_ = 0.0f;
            if (!directional_light || !directional_light->cast_shadows ||
                directional_light->intensity <= 0.0f) continue;

            const SceneView& camera = view.scene_view();
            const float end = camera.infinite_far() ? directional_light->shadow_distance
                                                    : std::min(camera.far_clip(), directional_light->shadow_distance);
            if (!is_finite(end) || end <= camera.near_clip()) continue;
            Vector3 forward;
            if (!try_normalize(directional_light->direction, forward))
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Directional shadow has invalid direction.");
            const Vector3 requested_up = std::abs(dot(forward, Vector3(0, 1, 0))) > k_parallel_up_threshold
                ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
            Vector3 right;
            if (!try_normalize(cross(requested_up, forward), right))
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Directional shadow basis is degenerate.");
            const Vector3 up = cross(forward, right);
            const Vector3 camera_right = rotate_vector(camera.camera_orientation(), Vector3(1, 0, 0));
            const Vector3 camera_up = rotate_vector(camera.camera_orientation(), Vector3(0, 1, 0));
            const Vector3 camera_forward = rotate_vector(camera.camera_orientation(), Vector3(0, 0, 1));
            const float aspect = static_cast<float>(camera.view_rect().width) /
                                 static_cast<float>(camera.view_rect().height);
            const float tan_half_fov = std::tan(camera.vertical_fov().value() * 0.5f);
            const float distance_span = end - camera.near_clip();
            const float split = camera.near_clip() + distance_span * k_near_cascade_fraction;
            const float blend_half_width = distance_span * k_cascade_blend_fraction;
            view.shadow_split_start_ = split - blend_half_width;
            view.shadow_split_end_ = split + blend_half_width;
            view.shadow_effective_end_ = end;
            view.shadow_fade_start_ = std::max(camera.near_clip(),
                end * (1.0f - directional_light->shadow_distance_fade_fraction));

            for (std::size_t cascade_index = 0; cascade_index < ShadowRenderTargets::k_cascade_count; ++cascade_index)
            {
                ShadowCascadeInfo& cascade = view.shadow_cascades_[cascade_index];
                cascade.near_distance = cascade_index == 0u ? camera.near_clip() : view.shadow_split_start_;
                cascade.far_distance = cascade_index == 0u ? view.shadow_split_end_ : end;
                std::array<Vector3, 8> corners;
                for (std::size_t plane = 0; plane < 2u; ++plane)
                {
                    const float distance = plane == 0u ? cascade.near_distance : cascade.far_distance;
                    const Vector3 center = camera.camera_position() + camera_forward * distance;
                    const Vector3 horizontal = camera_right * (tan_half_fov * aspect * distance);
                    const Vector3 vertical = camera_up * (tan_half_fov * distance);
                    corners[plane * 4u + 0u] = center - horizontal - vertical;
                    corners[plane * 4u + 1u] = center + horizontal - vertical;
                    corners[plane * 4u + 2u] = center - horizontal + vertical;
                    corners[plane * 4u + 3u] = center + horizontal + vertical;
                }
                Vector3 sphere_center;
                BasisBounds receiver;
                for (const Vector3& corner : corners)
                {
                    sphere_center += corner;
                    include_basis_point(receiver, corner, right, up, forward);
                }
                sphere_center /= static_cast<float>(corners.size());
                float radius = 0.0f;
                for (const Vector3& corner : corners)
                    radius = std::max(radius, length(corner - sphere_center));
                if (!is_finite(sphere_center) || !finite_basis_bounds(receiver) ||
                    !is_finite(radius) || radius <= 0.0f)
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Directional shadow receiver bounds are invalid.");

                const float texel_world = 2.0f * radius / ShadowRenderTargets::k_resolution;
                const float extent = radius + 2.0f * texel_world;
                const float texel_step = 2.0f * extent / ShadowRenderTargets::k_resolution;
                const float center_x = std::round(dot(right, sphere_center) / texel_step) * texel_step;
                const float center_y = std::round(dot(up, sphere_center) / texel_step) * texel_step;
                float minimum_z = receiver.min_z;
                float maximum_z = receiver.max_z;

                for (const auto& primitive : render_scene.primitive_scene_infos())
                {
                    if (!primitive || !primitive->proxy()) continue;
                    const auto* proxy = dynamic_cast<const StaticMeshSceneProxy*>(primitive->proxy());
                    if (!proxy || !proxy->visible() || !proxy->cast_shadows() || !proxy->normal_transform_valid()) continue;
                    const AxisAlignedBounds& world = proxy->world_bounds();
                    const Vector3 minimum(world.minimum.x, world.minimum.y, world.minimum.z);
                    const Vector3 maximum(world.maximum.x, world.maximum.y, world.maximum.z);
                    if (!is_finite(minimum) || !is_finite(maximum) || minimum.x > maximum.x ||
                        minimum.y > maximum.y || minimum.z > maximum.z) continue;
                    BasisBounds caster;
                    for (const Vector3& corner : bounds_corners(minimum, maximum))
                        include_basis_point(caster, corner, right, up, forward);
                    if (!finite_basis_bounds(caster) || caster.max_x < center_x - extent ||
                        caster.min_x > center_x + extent || caster.max_y < center_y - extent ||
                        caster.min_y > center_y + extent || caster.min_z > receiver.max_z) continue;
                    StaticMeshRenderData* render_data = proxy->render_data();
                    if (!render_data || !render_data->vertex_factory()) continue;
                    const RHIStatus prepared = render_data->prepare_current_recording();
                    if (!prepared || !render_data->is_drawable()) continue;
                    const auto& materials = proxy->material_render_proxies();
                    const auto& sections = render_data->sections();
                    bool added = false;
                    for (std::size_t section_index = 0; section_index < sections.size(); ++section_index)
                    {
                        const StaticMeshSection& section = sections[section_index];
                        if (!section.index_count || section.index_count % 3u != 0u ||
                            section.first_index > render_data->index_count() ||
                            section.index_count > render_data->index_count() - section.first_index ||
                            section.material_slot >= materials.size() ||
                            section_index > std::numeric_limits<std::uint32_t>::max()) continue;
                        MaterialRenderProxy* material = materials[section.material_slot];
                        const auto* state = material ? material->effective_graphics_pass_state() : nullptr;
                        if (!material || !material->shader_program() || !state || state->blend.enabled) continue;
                        cascade.batches.emplace_back(*proxy, *render_data, *render_data->vertex_factory(),
                                                     *material, section.first_index, section.index_count,
                                                     static_cast<std::uint32_t>(section_index));
                        added = true;
                    }
                    if (added)
                    {
                        minimum_z = std::min(minimum_z, caster.min_z);
                        maximum_z = std::max(maximum_z, caster.max_z);
                    }
                }
                const float padding = std::max(k_min_depth_padding,
                                               (maximum_z - minimum_z) * k_depth_padding_fraction);
                Matrix4 light_view;
                light_view.at(0, 0) = right.x; light_view.at(1, 0) = right.y; light_view.at(2, 0) = right.z;
                light_view.at(0, 1) = up.x; light_view.at(1, 1) = up.y; light_view.at(2, 1) = up.z;
                light_view.at(0, 2) = forward.x; light_view.at(1, 2) = forward.y; light_view.at(2, 2) = forward.z;
                light_view.at(3, 0) = -center_x;
                light_view.at(3, 1) = -center_y;
                light_view.at(3, 2) = -(minimum_z - padding);
                OrthographicProjectionDesc projection_desc;
                projection_desc.left = -extent; projection_desc.right = extent;
                projection_desc.bottom = -extent; projection_desc.top = extent;
                projection_desc.near_clip = padding * 0.5f;
                projection_desc.far_clip = maximum_z - minimum_z + padding * 1.5f;
                Matrix4 projection;
                if (!is_finite(light_view) || !try_make_orthographic_projection(projection_desc, projection))
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Directional shadow projection is invalid.");
                cascade.world_to_clip = projection * light_view;
                const float depth_span = projection_desc.far_clip - projection_desc.near_clip;
                const float depth_per_texel = texel_step / depth_span;
                cascade.light_direction = Vector4(forward, 0.0f);
                // Scale both user controls independently in world texels; the bounded caster bias
                // must cover the Gather PCF footprint without detaching contact shadows.
                cascade.bias_parameters = Vector4(directional_light->shadow_bias * k_constant_bias_texels * depth_per_texel,
                    directional_light->shadow_slope_bias * k_slope_bias_texels * depth_per_texel,
                    k_max_slope, std::min(k_max_bias_texels * depth_per_texel, k_max_normalized_bias));
                if (!is_finite(cascade.world_to_clip) || !is_finite(cascade.bias_parameters))
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Directional shadow data is not finite.");
            }
            view.shadow_active_ = is_finite(view.shadow_fade_start_) &&
                is_finite(view.shadow_split_start_) && is_finite(view.shadow_split_end_);
            if (!view.shadow_active_)
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Directional shadow split is not finite.");
        }
        return RHIStatus::success();
    }
} // namespace toy3d
