#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "math/integer_vector.h"
#include "renderscene/shadow_render_targets.h"

#include <cstdint>

namespace toy3d
{
    class RHIDevice;

    // Renderer-owned RT-only scene attachment set. Presentation images remain
    // owned by RHIViewportContext; Forward passes write these offscreen targets.
    class SceneRenderTargets final
    {
      public:
        SceneRenderTargets() = default;
        ~SceneRenderTargets() = default;

        SceneRenderTargets(const SceneRenderTargets&) = delete;
        SceneRenderTargets& operator=(const SceneRenderTargets&) = delete;

        RHIStatus ensure_extent(RHIDevice& device, const Extent& extent);
        void release() noexcept;

        const RHITextureRef& scene_color_texture() const noexcept;
        const RHITextureViewRef& scene_color_view() const noexcept;
        const RHITextureViewRef& scene_color_shader_resource_view() const noexcept;
        const RHITextureRef& scene_depth_texture() const noexcept;
        const RHITextureViewRef& scene_depth_view() const noexcept;
        const RHITextureViewRef& scene_depth_shader_resource_view() const noexcept;
        RHIAccess scene_color_access() const noexcept;
        RHIAccess scene_depth_access() const noexcept;
        void publish_submitted_access(RHIAccess scene_color_access, RHIAccess scene_depth_access) noexcept;
        ShadowRenderTargets& shadow_targets() noexcept
        {
            return shadow_targets_;
        }

      private:
        bool matches(const Extent& extent) const noexcept;

        RHIStatus create_targets(RHIDevice& device, const Extent& extent);

        RHITextureRef scene_color_texture_;
        RHITextureViewRef scene_color_view_;
        RHITextureViewRef scene_color_shader_resource_view_;
        RHITextureRef scene_depth_texture_;
        RHITextureViewRef scene_depth_view_;
        RHITextureViewRef scene_depth_shader_resource_view_;
        const RHIDevice* owning_device_ = nullptr;
        RHIAccess scene_color_access_ = RHIAccess::Common;
        RHIAccess scene_depth_access_ = RHIAccess::Common;
        ShadowRenderTargets shadow_targets_;
    };
} // namespace toy3d
