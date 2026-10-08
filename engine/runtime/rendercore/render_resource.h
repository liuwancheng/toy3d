#pragma once

#include "drivers/rhi/rhi_result.h"

#include <cstddef>
#include <memory>
#include <thread>
#include <utility>

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
        Failed
    };

    // Render-side resource lifecycle base. Mutable state is confined to the
    // logical Rendering Thread; GPU submission and completion remain RHI duties.
    class RenderResource : public std::enable_shared_from_this<RenderResource>
    {
      public:
        RenderResource() = default;
        virtual ~RenderResource();

        RenderResource(const RenderResource&) = delete;
        RenderResource& operator=(const RenderResource&) = delete;
        RenderResource(RenderResource&&) = delete;
        RenderResource& operator=(RenderResource&&) = delete;

        RenderResourceState state() const
        {
            return state_;
        }

        const RHIStatus& failure_status() const
        {
            return failure_status_;
        }

        std::size_t ref_count() const noexcept
        {
            return ref_count_;
        }

      protected:
        virtual bool supports_update() const noexcept
        {
            return false;
        }

        // Derived resources use this only for deterministic resource-local
        // failures. Frame abort and explicit no-submit failures remain retryable.
        RHIStatus fail(RHIStatus status);

        virtual RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context) = 0;
        virtual void on_recording_committed() noexcept = 0;
        virtual void on_recording_discarded() noexcept = 0;
        virtual void release_rhi() noexcept = 0;

      private:
        template <typename T> friend class RenderResourceRef;
        friend class RenderResourceManager;

        void retain() noexcept;
        void release() noexcept;

        RenderResourceManager* pending_manager_ = nullptr;
        RenderResourceManager* owner_manager_ = nullptr;
        std::thread::id ref_thread_{};
        std::size_t ref_count_ = 0;
        bool reclaim_requested_ = false;
        RenderResourceState state_ = RenderResourceState::Uninitialized;
        RHIStatus failure_status_;
    };

    // Strong ownership keeps the representation alive; the explicit count tracks
    // rendering consumers independently of CPU asset/cache ownership. RT-only.
    template <typename T> class RenderResourceRef
    {
      public:
        RenderResourceRef() = default;

        RenderResourceRef(const RenderResourceRef& other) : resource_(other.resource_)
        {
            if (resource_)
            {
                resource_->retain();
            }
        }

        RenderResourceRef(RenderResourceRef&& other) noexcept = default;

        RenderResourceRef& operator=(RenderResourceRef other) noexcept
        {
            resource_.swap(other.resource_);
            return *this;
        }

        ~RenderResourceRef()
        {
            if (resource_)
            {
                resource_->release();
            }
        }

        T* get() const noexcept
        {
            return resource_.get();
        }

        T* operator->() const noexcept
        {
            return get();
        }

        explicit operator bool() const noexcept
        {
            return resource_ != nullptr;
        }

      private:
        friend class RenderResourceManager;

        explicit RenderResourceRef(std::shared_ptr<T> resource) : resource_(std::move(resource))
        {
            resource_->retain();
        }

        std::shared_ptr<T> resource_;
    };
} // namespace toy3d
