#pragma once

#include "math/matrix4.h"
#include "rendercore/geometry/axis_aligned_bounds.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"

#include <cstdint>

namespace toy3d
{
    class RenderScene;

    // Render-side representation with a stable address. Game-side code may retain
    // its pointer only as an opaque identity protected by RenderCommand FIFO order.
    class PrimitiveSceneProxy
    {
      public:
        virtual ~PrimitiveSceneProxy() = default;

        PrimitiveSceneProxy(const PrimitiveSceneProxy&) = delete;
        PrimitiveSceneProxy& operator=(const PrimitiveSceneProxy&) = delete;

        const Matrix4& world_transform() const { return world_transform_; }
        const ObjectShaderParameters& object_shader_parameters() const { return object_shader_parameters_; }
        std::uint64_t object_data_generation() const { return object_data_generation_; }
        const AxisAlignedBounds& world_bounds() const { return world_bounds_; }
        bool visible() const { return visible_; }

      protected:
        PrimitiveSceneProxy(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible);

      private:
        friend class RenderScene;

        void update_transform(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible);

        Matrix4 world_transform_;
        ObjectShaderParameters object_shader_parameters_;
        std::uint64_t object_data_generation_ = 1u;
        AxisAlignedBounds world_bounds_;
        bool visible_ = true;
    };
} // namespace toy3d
