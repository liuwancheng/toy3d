#include "renderscene/render_resource_manager.h"

#include <algorithm>
#include <cassert>

#include "drivers/rhi/rhi_command_context.h"
#include "renderscene/render_resource.h"

namespace toy3d
{
    namespace
    {
        RHIStatus invalid_caller_status()
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "RenderResourceManager is mutable only on its logical Rendering Thread");
        }

        RHIStatus terminal_status()
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "RenderResourceManager no longer accepts work after terminal clear");
        }
    }

    RenderResourceManager::RenderResourceManager(RHIDevice& device)
        : owner_thread_id_(std::this_thread::get_id())
        , device_(device)
    {
    }

    RenderResourceManager::~RenderResourceManager()
    {
        assert(is_on_owner_thread());
        assert(pending_resources_.empty());
        assert(recording_resources_.empty());
    }

    RHIStatus RenderResourceManager::begin_init(RenderResource& resource)
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }
        if (resource.state_ != RenderResourceState::Uninitialized ||
            resource.pending_manager_ != nullptr)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "RenderResource begin_init requires an uninitialized resource");
        }

        resource.pending_manager_ = this;
        resource.state_ = RenderResourceState::PendingUpload;
        resource.failure_status_ = RHIStatus::success();
        pending_resources_.push_back(&resource);
        return RHIStatus::success();
    }

    RHIStatus RenderResourceManager::begin_update(RenderResource& resource)
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }
        if (resource.state_ != RenderResourceState::Ready ||
            resource.pending_manager_ != nullptr)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "RenderResource begin_update requires a Ready resource without pending work");
        }

        // Ready remains the published long-term state while the manager's
        // private collections carry the retryable update transaction.
        resource.pending_manager_ = this;
        resource.failure_status_ = RHIStatus::success();
        pending_resources_.push_back(&resource);
        return RHIStatus::success();
    }

    RHIStatus RenderResourceManager::record_pending_uploads(
        RHIGraphicsCommandContext& context)
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }
        if (!recording_resources_.empty() || recording_failed_)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "RenderResourceManager recording must be committed or discarded before reuse");
        }

        // RT-only mutation makes this value snapshot stable for the duration of
        // the call without adding a public transaction or prepared state.
        const std::vector<RenderResource*> pending_snapshot = pending_resources_;
        for (RenderResource* const resource : pending_snapshot)
        {
            if (resource == nullptr || resource->pending_manager_ != this ||
                (resource->state_ != RenderResourceState::PendingUpload &&
                 resource->state_ != RenderResourceState::Ready))
            {
                recording_failed_ = true;
                return RHIStatus::failure(
                    RHIErrorCode::BackendFailure,
                    "RenderResourceManager pending collection invariant was violated");
            }

            const RHIStatus recorded = resource->record_upload(device_, context);
            if (!recorded.succeeded())
            {
                // A failed resource may have created partial candidate RHI refs;
                // its hook rolls those back without affecting already-recorded
                // command-list payload ownership.
                resource->on_recording_discarded();
                const bool deterministic_update_failure =
                    resource->state_ == RenderResourceState::Ready &&
                    (recorded.code() == RHIErrorCode::InvalidArgument ||
                     recorded.code() == RHIErrorCode::Unsupported);
                if (resource->state_ == RenderResourceState::Failed ||
                    deterministic_update_failure)
                {
                    remove_pending(resource);
                    resource->pending_manager_ = nullptr;
                }
                recording_failed_ = true;
                return recorded;
            }
            if (resource->state_ != RenderResourceState::PendingUpload &&
                resource->state_ != RenderResourceState::Ready)
            {
                resource->on_recording_discarded();
                remove_pending(resource);
                resource->pending_manager_ = nullptr;
                recording_failed_ = true;
                return RHIStatus::failure(
                    RHIErrorCode::BackendFailure,
                    "RenderResource changed long-term state during upload recording");
            }

            recording_resources_.push_back(resource);
        }
        return RHIStatus::success();
    }

    RHIStatus RenderResourceManager::commit_recording()
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }
        if (recording_failed_)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "A failed RenderResource recording can only be discarded");
        }

        for (RenderResource* const resource : recording_resources_)
        {
            if (resource == nullptr || resource->pending_manager_ != this ||
                (resource->state_ != RenderResourceState::PendingUpload &&
                 resource->state_ != RenderResourceState::Ready))
            {
                return RHIStatus::failure(
                    RHIErrorCode::BackendFailure,
                    "RenderResourceManager cannot commit an invalid recording entry");
            }
        }

        // The caller invokes this only after business submit success. Publication
        // deliberately does not wait for queue completion or presentation.
        for (RenderResource* const resource : recording_resources_)
        {
            resource->on_recording_committed();
            if (resource->state_ == RenderResourceState::PendingUpload)
            {
                resource->state_ = RenderResourceState::Ready;
            }
            resource->failure_status_ = RHIStatus::success();
            remove_pending(resource);
            resource->pending_manager_ = nullptr;
        }
        recording_resources_.clear();
        return RHIStatus::success();
    }

    RHIStatus RenderResourceManager::discard_recording()
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }

        for (RenderResource* const resource : recording_resources_)
        {
            if (resource != nullptr && resource->pending_manager_ == this &&
                (resource->state_ == RenderResourceState::PendingUpload ||
                 resource->state_ == RenderResourceState::Ready))
            {
                resource->on_recording_discarded();
            }
        }
        recording_resources_.clear();
        recording_failed_ = false;
        return RHIStatus::success();
    }

    RHIStatus RenderResourceManager::release(RenderResource& resource)
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }
        if (resource.state_ == RenderResourceState::Released)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "RenderResource has already been released");
        }
        if (resource.pending_manager_ != nullptr &&
            resource.pending_manager_ != this)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Pending RenderResource belongs to another manager");
        }

        if (remove_recording(&resource))
        {
            resource.on_recording_discarded();
        }
        remove_pending(&resource);
        resource.pending_manager_ = nullptr;
        resource.release_rhi();
        resource.state_ = RenderResourceState::Released;
        return RHIStatus::success();
    }

    RHIStatus RenderResourceManager::clear_for_terminal()
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return RHIStatus::success();
        }

        for (RenderResource* const resource : recording_resources_)
        {
            if (resource != nullptr && resource->pending_manager_ == this &&
                (resource->state_ == RenderResourceState::PendingUpload ||
                 resource->state_ == RenderResourceState::Ready))
            {
                resource->on_recording_discarded();
            }
        }
        recording_resources_.clear();
        recording_failed_ = false;

        // Clear every non-owning pointer before terminal command disposal is
        // allowed to destroy the actual resource representations.
        for (RenderResource* const resource : pending_resources_)
        {
            if (resource != nullptr && resource->pending_manager_ == this)
            {
                resource->pending_manager_ = nullptr;
            }
        }
        pending_resources_.clear();
        terminal_ = true;
        return RHIStatus::success();
    }

    bool RenderResourceManager::is_on_owner_thread() const
    {
        return std::this_thread::get_id() == owner_thread_id_;
    }

    void RenderResourceManager::remove_pending(RenderResource* resource)
    {
        pending_resources_.erase(
            std::remove(
                pending_resources_.begin(), pending_resources_.end(), resource),
            pending_resources_.end());
    }

    bool RenderResourceManager::remove_recording(RenderResource* resource)
    {
        const auto found = std::find(
            recording_resources_.begin(), recording_resources_.end(), resource);
        if (found == recording_resources_.end())
        {
            return false;
        }
        recording_resources_.erase(found);
        return true;
    }
}
