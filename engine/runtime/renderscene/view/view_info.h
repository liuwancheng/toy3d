#pragma once

#include "math/geometry/convex_volume.h"
#include "math/matrix4.h"
#include "rendercore/shader/view_uniform_shader_parameters.h"
#include "rendercore/view/scene_view.h"
#include "renderscene/mesh_batch.h"

#include <vector>

namespace toy3d
{
    class ForwardSceneRenderer;
    class PrimitiveSceneInfo;

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

      private:
        friend class ForwardSceneRenderer;

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
        std::vector<PrimitiveSceneInfo*> visible_primitives_;
        std::vector<MeshBatch> mesh_batches_;
    };
} // namespace toy3d
