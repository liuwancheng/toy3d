#include "rendercore/render_resource.h"

#include <cassert>
#include <utility>

#include "rendercore/render_resource_manager.h"

namespace toy3d
{
    RenderResource::~RenderResource()
    {
        // Pending resources must be removed or detached by terminal clear before
        // their representation owner destroys them.
        assert(pending_manager_ == nullptr);
        assert(ref_count_ == 0);
        if (owner_manager_ != nullptr)
        {
            owner_manager_->detach_destroyed_resource(*this);
        }
    }

    void RenderResource::retain() noexcept
    {
        assert(ref_thread_ == std::this_thread::get_id());
        ++ref_count_;
        reclaim_requested_ = false;
    }

    void RenderResource::release() noexcept
    {
        assert(ref_thread_ == std::this_thread::get_id());
        assert(ref_count_ > 0);
        --ref_count_;
        if (ref_count_ == 0)
        {
            reclaim_requested_ = true;
        }
    }

    RHIStatus RenderResource::fail(RHIStatus status)
    {
        if (status.succeeded())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "RenderResource cannot enter Failed with a success status");
        }
        if (state_ != RenderResourceState::PendingUpload)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "RenderResource can enter Failed only while PendingUpload");
        }

        failure_status_ = status;
        state_ = RenderResourceState::Failed;
        return status;
    }
} // namespace toy3d
