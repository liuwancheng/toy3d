#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "math/vector4.h"
#include "rendercore/render_resource.h"

#include <vector>

namespace toy3d
{
    // One immutable section/pose upload; replacement never overwrites an in-flight buffer.
    class BoneMatrixBuffer final : public RenderResource
    {
      public:
        explicit BoneMatrixBuffer(const std::vector<Vector4>& rows);
        const RHIBufferViewRef& view() const
        {
            return view_;
        }

      private:
        RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context) override;
        void on_recording_committed() noexcept override;
        void on_recording_discarded() noexcept override;
        void release_rhi() noexcept override;

        std::vector<float> values_;
        RHIBufferRef buffer_;
        RHIBufferViewRef view_;
        bool valid_ = false;
    };
} // namespace toy3d
