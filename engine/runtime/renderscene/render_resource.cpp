#include "renderscene/render_resource.h"

#include <cassert>
#include <utility>

namespace toy3d
{
    RenderResource::~RenderResource()
    {
        // Pending resources must be removed or detached by terminal clear before
        // their representation owner destroys them.
        assert(pending_manager_ == nullptr);
    }

    RHIStatus RenderResource::fail(RHIStatus status)
    {
        if (status.succeeded())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "RenderResource cannot enter Failed with a success status");
        }
        if (state_ != RenderResourceState::PendingUpload)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "RenderResource can enter Failed only while PendingUpload");
        }

        failure_status_ = status;
        state_ = RenderResourceState::Failed;
        return status;
    }
}
