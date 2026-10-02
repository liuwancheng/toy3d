#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "rendercore/scene/light_scene_proxy.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

namespace toy3d
{
    class RHIDevice;

    struct ShadowCascadeTile
    {
        static constexpr std::uint32_t k_border = 4u;
        std::uint32_t x = 0u;
        std::uint32_t y = 0u;
        std::uint32_t size = 0u;
        std::uint32_t resolution() const
        {
            return size > 2u * k_border ? size - 2u * k_border : 0u;
        }
    };

    // Stable directional CSM layout; sizes include the PCF border. No camera-dependent packing.
    struct ShadowAtlasLayout
    {
        std::size_t cascade_count = 0u;
        std::uint32_t max_resolution = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::array<ShadowCascadeTile, LightSceneData::k_max_shadow_cascades> tiles{};
    };

    RHIStatus build_shadow_atlas_layout(std::size_t cascade_count, std::uint32_t max_resolution,
                                        ShadowAtlasLayout& layout);

    // One depth atlas per View. Renderer owns the container across frames;
    // submitted lists retain their own GPU references when it is replaced.
    class ShadowRenderTargets final
    {
      public:
        static constexpr std::size_t k_max_cascade_count = LightSceneData::k_max_shadow_cascades;

        RHIStatus ensure_views(RHIDevice& device, std::size_t view_count, std::size_t cascade_count,
                               std::uint32_t requested_resolution);
        void release() noexcept;
        std::size_t view_count() const
        {
            return views_.size();
        }
        const ShadowAtlasLayout& layout() const
        {
            return layout_;
        }
        const RHITextureRef& texture(std::size_t view) const
        {
            return views_.at(view).texture;
        }
        const RHITextureViewRef& depth_view(std::size_t view) const
        {
            return views_.at(view).depth_view;
        }
        const RHITextureViewRef& shader_view(std::size_t view) const
        {
            return views_.at(view).shader_view;
        }
        RHIAccess access(std::size_t view) const
        {
            return views_.at(view).access;
        }
        const RHISamplerRef& sampler() const noexcept
        {
            return sampler_;
        }
        void publish_submitted_access() noexcept;

      private:
        struct ViewTarget
        {
            RHITextureRef texture;
            RHITextureViewRef depth_view;
            RHITextureViewRef shader_view;
            RHIAccess access = RHIAccess::Common;
        };
        const RHIDevice* device_ = nullptr;
        ShadowAtlasLayout layout_;
        std::vector<ViewTarget> views_;
        RHISamplerRef sampler_;
    };
} // namespace toy3d
