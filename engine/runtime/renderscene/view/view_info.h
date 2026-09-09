#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "math/geometry/convex_volume.h"
#include "math/matrix4.h"
#include "rendercore/shader/view_uniform_shader_parameters.h"
#include "rendercore/view/scene_view.h"
#include "renderscene/mesh_batch.h"

#include <utility>
#include <vector>

namespace toy3d
{
    class ForwardSceneRenderer;
    class PrimitiveSceneInfo;
    class RenderScene;

    // Render-side per-view state. ForwardSceneRenderer creates and mutates it
    // only on the logical Rendering Thread for one Draw.
    class ViewInfo
    {
      public:
        const SceneView& scene_view() const { return scene_view_; }
        const Matrix4& view_matrix() const { return view_matrix_; }
        const Matrix4& projection_matrix() const { return projection_matrix_; }
        const Matrix4& view_projection_matrix() const { return view_projection_matrix_; }
        const Matrix4& inverse_view_matrix() const { return inverse_view_matrix_; }
        const Matrix4& inverse_projection_matrix() const { return inverse_projection_matrix_; }
        const Matrix4& inverse_view_projection_matrix() const { return inverse_view_projection_matrix_; }
        const ViewUniformShaderParameters& view_uniform_shader_parameters() const
        {
            return view_uniform_shader_parameters_;
        }
        const ConvexVolume& view_frustum() const { return view_frustum_; }
        const std::vector<PrimitiveSceneInfo*>& visible_primitives() const { return visible_primitives_; }
        const std::vector<MeshBatch>& mesh_batches() const { return mesh_batches_; }
        const RHIUniformBufferSlice& view_uniform_slice() const { return view_uniform_slice_; }
        const RHIBindingSetRef& view_binding_set() const { return view_binding_set_; }
        // view_uniform_resources.* is the single policy path for these
        // frame-local GPU resources; concrete passes use that module's helpers.
        void publish_view_uniform_resources(RHIUniformBufferSlice slice, RHIBindingSetRef binding_set);

      private:
        friend class ForwardSceneRenderer;
        friend void compute_scene_visibility(const RenderScene& render_scene, std::vector<ViewInfo>& view_infos);

        ViewInfo(SceneView scene_view, Matrix4 view_matrix, Matrix4 projection_matrix, Matrix4 view_projection_matrix,
                 Matrix4 inverse_view_matrix, Matrix4 inverse_projection_matrix, Matrix4 inverse_view_projection_matrix,
                 ConvexVolume view_frustum);

        SceneView scene_view_;
        Matrix4 view_matrix_;
        Matrix4 projection_matrix_;
        Matrix4 view_projection_matrix_;
        Matrix4 inverse_view_matrix_;
        Matrix4 inverse_projection_matrix_;
        Matrix4 inverse_view_projection_matrix_;
        ViewUniformShaderParameters view_uniform_shader_parameters_;
        ConvexVolume view_frustum_;
        RHIUniformBufferSlice view_uniform_slice_;
        RHIBindingSetRef view_binding_set_;
        std::vector<PrimitiveSceneInfo*> visible_primitives_;
        std::vector<MeshBatch> mesh_batches_;
    };

} // namespace toy3d
