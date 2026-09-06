#pragma once

#include "drivers/rhi/rhi_result.h"

namespace toy3d
{
    class RHIGraphicsCommandContext;
    class RHIDevice;
    class RenderResourceManager;

    enum class RenderResourceState
    {
        Uninitialized,
        PendingUpload,
        Ready,
        Failed,
        Released
    };

    // Render-side resource lifecycle base. Mutable state is confined to the
    // logical Rendering Thread; GPU submission and completion remain RHI duties.
    class RenderResource
    {
      public:
        RenderResource() = default;
        virtual ~RenderResource();

        RenderResource(const RenderResource&) = delete;
        RenderResource& operator=(const RenderResource&) = delete;
        RenderResource(RenderResource&&) = delete;
        RenderResource& operator=(RenderResource&&) = delete;

        RenderResourceState state() const { return state_; }

        const RHIStatus& failure_status() const { return failure_status_; }

      protected:
        // Derived resources use this only for deterministic resource-local
        // failures. Frame abort and explicit no-submit failures remain retryable.
        RHIStatus fail(RHIStatus status);

        virtual RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context) = 0;
        virtual void on_recording_committed() noexcept = 0;
        virtual void on_recording_discarded() noexcept = 0;
        virtual void release_rhi() noexcept = 0;

      private:
        friend class RenderResourceManager;

        RenderResourceManager* pending_manager_ = nullptr;
        RenderResourceState state_ = RenderResourceState::Uninitialized;
        RHIStatus failure_status_;
    };
} // namespace toy3d
