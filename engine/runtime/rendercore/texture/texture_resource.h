#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "rendercore/texture/texture.h"
#include "rendercore/render_resource.h"

#include <cstdint>

namespace toy3d
{
    class RenderResourceManager;

    // Stable Render-side representation. All mutable fields are confined to
    // the logical Rendering Thread; CPU assets and manager pins share its allocation.
    class TextureResource final : public RenderResource
    {
      public:
        explicit TextureResource(const TextureDesc& initial_desc);

        RHIStatus update(TextureDesc desc, RenderResourceManager& manager);

        const RHITextureViewRef& view_for_current_recording() const noexcept;
        TextureUsage usage_for_current_recording() const noexcept;
        const RHITextureViewRef& active_view() const noexcept
        {
            return active_view_;
        }
        std::uint64_t binding_generation() const noexcept
        {
            return binding_generation_;
        }

      private:
        bool supports_update() const noexcept override
        {
            return true;
        }

        RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context) override;
        void on_recording_committed() noexcept override;
        void on_recording_discarded() noexcept override;
        void release_rhi() noexcept override;

        TextureDesc initial_desc_;
        TextureDesc active_desc_;
        TextureDesc pending_desc_;
        RHITextureRef active_texture_;
        RHITextureViewRef active_view_;
        RHITextureRef candidate_texture_;
        RHITextureViewRef candidate_view_;
        std::uint64_t binding_generation_ = 0;
        bool has_active_desc_ = false;
        bool has_pending_update_ = false;
        bool pending_replacement_ = false;
        bool deterministic_recording_failure_ = false;
    };
} // namespace toy3d
