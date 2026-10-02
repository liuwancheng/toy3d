#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "math/integer_vector.h"

namespace toy3d
{
    class RHIDevice;

    // Render-thread-owned SDR image sampled by the UI after tonemapping.
    class ViewportOutputTarget final
    {
      public:
        RHIStatus ensure_extent(RHIDevice& device, const Extent& extent);
        void release() noexcept;

        const RHITextureRef& texture() const noexcept
        {
            return texture_;
        }
        const RHITextureViewRef& render_target_view() const noexcept
        {
            return render_target_view_;
        }
        const RHITextureViewRef& shader_resource_view() const noexcept
        {
            return shader_resource_view_;
        }
        RHIAccess access() const noexcept
        {
            return access_;
        }
        void publish_submitted_access(RHIAccess access) noexcept
        {
            access_ = access;
        }

      private:
        RHITextureRef texture_;
        RHITextureViewRef render_target_view_;
        RHITextureViewRef shader_resource_view_;
        const RHIDevice* owning_device_ = nullptr;
        RHIAccess access_ = RHIAccess::Common;
    };
} // namespace toy3d
