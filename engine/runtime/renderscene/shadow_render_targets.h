#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace toy3d
{
    class RHIDevice;

    // One depth texture per View and cascade. Renderer owns the container across frames;
    // submitted lists retain their own GPU references when it is replaced.
    class ShadowRenderTargets final
    {
      public:
        static constexpr std::uint32_t k_resolution = 1024u;
        static constexpr std::size_t k_cascade_count = 2u;

        RHIStatus ensure_views(RHIDevice& device, std::size_t view_count);
        void release() noexcept;
        const RHITextureRef& texture(std::size_t view, std::size_t cascade) const { return views_.at(view * k_cascade_count + cascade).texture; }
        const RHITextureViewRef& depth_view(std::size_t view, std::size_t cascade) const { return views_.at(view * k_cascade_count + cascade).depth_view; }
        const RHITextureViewRef& shader_view(std::size_t view, std::size_t cascade) const { return views_.at(view * k_cascade_count + cascade).shader_view; }
        RHIAccess access(std::size_t view, std::size_t cascade) const { return views_.at(view * k_cascade_count + cascade).access; }
        const RHISamplerRef& sampler() const noexcept { return sampler_; }
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
        std::vector<ViewTarget> views_;
        RHISamplerRef sampler_;
    };
} // namespace toy3d
