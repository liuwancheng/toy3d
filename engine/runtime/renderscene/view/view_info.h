#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "math/geometry/convex_volume.h"
#include "math/matrix4.h"
#include "rendercore/view/scene_view.h"
#include "renderscene/mesh_batch.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"

#include <utility>
#include <vector>

namespace toy3d
{
    class ForwardSceneRenderer;
    class PrimitiveSceneInfo;
    class RenderScene;
    class RHICommandContext;
    class RHIDevice;
    class RHIStatus;

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
        const ViewShaderParameters& view_shader_parameters() const { return view_shader_parameters_; }
        const ConvexVolume& view_frustum() const { return view_frustum_; }
        const std::vector<PrimitiveSceneInfo*>& visible_primitives() const { return visible_primitives_; }
        const std::vector<MeshBatch>& mesh_batches() const { return mesh_batches_; }
        const RHIBindingSetRef& view_binding() const { return view_binding_; }
        // create_view_shader_bindings() is the only frame-local creation path;
        // business passes consume the published owner reference directly.
        void publish_view_binding(RHIBindingSetRef binding_set);

      private:
        friend class ForwardSceneRenderer;
        friend void compute_scene_visibility(const RenderScene& render_scene, std::vector<ViewInfo>& view_infos);
        friend RHIStatus create_object_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                                       std::vector<ViewInfo>& view_infos);
        friend RHIStatus create_material_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                                         std::vector<ViewInfo>& view_infos);

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
        ViewShaderParameters view_shader_parameters_;
        ConvexVolume view_frustum_;
        RHIBindingSetRef view_binding_;
        std::vector<PrimitiveSceneInfo*> visible_primitives_;
        std::vector<MeshBatch> mesh_batches_;
    };

} // namespace toy3d
