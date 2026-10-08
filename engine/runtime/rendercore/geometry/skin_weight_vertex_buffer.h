#pragma once

#include "asset/mesh/skeletal_mesh_asset.h"
#include "drivers/rhi/rhi_resource.h"
#include "rendercore/render_resource.h"

namespace toy3d
{
    class SkinWeightVertexBuffer final : public RenderResource
    {
      public:
        SkinWeightVertexBuffer(const std::vector<SkinWeights>& weights, std::uint32_t num_bone_influences);
        const RHIBufferRef& buffer() const
        {
            return buffer_;
        }
        std::uint32_t stride() const
        {
            return num_bone_influences_ * 2u;
        }

      private:
        RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context) override;
        void on_recording_committed() noexcept override;
        void on_recording_discarded() noexcept override;
        void release_rhi() noexcept override;
        std::uint32_t num_bone_influences_ = 0;
        std::vector<std::uint8_t> bytes_;
        RHIBufferRef buffer_;
    };
} // namespace toy3d
