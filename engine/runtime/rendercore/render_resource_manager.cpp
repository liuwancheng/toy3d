#include "rendercore/render_resource_manager.h"

#include <algorithm>
#include <cassert>

#include "drivers/rhi/rhi_command_context.h"
#include "rendercore/render_resource.h"
#include "rendercore/geometry/static_mesh_render_data.h"
#include "rendercore/geometry/skeletal_mesh_render_data.h"

namespace toy3d
{
    namespace
    {
        RHIStatus invalid_caller_status()
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "RenderResourceManager is mutable only on its logical Rendering Thread");
        }

        RHIStatus terminal_status()
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "RenderResourceManager no longer accepts work after terminal clear");
        }
    } // namespace

    RenderResourceManager::RenderResourceManager(RHIDevice& device)
        : owner_thread_id_(std::this_thread::get_id()), device_(device)
    {
    }

    RenderResourceManager::~RenderResourceManager()
    {
        assert(is_on_owner_thread());
        const RHIStatus cleared = clear_for_terminal();
        assert(cleared.succeeded());
    }

    RHIStatus RenderResourceManager::register_resource(const std::shared_ptr<RenderResource>& resource)
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }
        if ((resource->owner_manager_ && resource->owner_manager_ != this) ||
            (resource->pending_manager_ && resource->pending_manager_ != this))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "RenderResource belongs to another manager");
        }
        if (resource->state_ == RenderResourceState::Failed)
        {
            return resource->failure_status_;
        }
        if (resource->owner_manager_ == nullptr && resource->ref_count_ != 0)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Detached rendering references must drain before reuse");
        }
        if (resource->state_ == RenderResourceState::Uninitialized)
        {
            const RHIStatus status = begin_init(*resource);
            if (!status)
            {
                return status;
            }
        }
        const auto registered = std::find_if(registered_resources_.begin(), registered_resources_.end(),
                                             [&resource](const std::shared_ptr<RenderResource>& entry)
                                             {
                                                 return entry.get() == resource.get();
                                             });
        if (registered == registered_resources_.end())
        {
            registered_resources_.push_back(resource);
            borrowed_resources_.erase(
                std::remove(borrowed_resources_.begin(), borrowed_resources_.end(), resource.get()),
                borrowed_resources_.end());
            resource->owner_manager_ = this;
            resource->ref_thread_ = owner_thread_id_;
        }
        return RHIStatus::success();
    }

    // Both mesh owners expose the same small coordination interface. Ordinary
    // templates keep this algorithm shared without a second owner base class.
    template <typename T>
    RHIStatus RenderResourceManager::begin_mesh(T& mesh, std::unordered_map<T*, std::shared_ptr<T>>& meshes)
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }
        if (mesh.owner_manager_ != nullptr || mesh.ref_count_ != 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Mesh initialization requires an unbound owner without rendering references");
        }
        const RHIStatus valid = mesh.validate_geometry();
        if (!valid)
        {
            return valid;
        }
        for (const RenderResource* resource : mesh.resources())
        {
            if (resource && (resource->owner_manager_ || resource->state_ != RenderResourceState::Uninitialized))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Mesh buffers must be uninitialized");
            }
        }
        meshes.emplace(&mesh, nullptr);
        mesh.owner_manager_ = this;
        mesh.ref_thread_ = owner_thread_id_;
        for (RenderResource* resource : mesh.resources())
        {
            if (resource)
            {
                const RHIStatus status = begin_init(*resource);
                if (!status)
                {
                    release_mesh(mesh, meshes);
                    return status;
                }
            }
        }
        return RHIStatus::success();
    }

    template <typename T>
    RHIStatus RenderResourceManager::register_mesh(const std::shared_ptr<T>& mesh,
                                                   std::unordered_map<T*, std::shared_ptr<T>>& meshes)
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }
        if (mesh->owner_manager_ && mesh->owner_manager_ != this)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Mesh belongs to another manager");
        }
        if (!mesh->owner_manager_ && mesh->ref_count_ != 0)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Detached mesh references must drain before reuse");
        }
        if (mesh->state() == RenderResourceState::Failed)
        {
            return mesh->failure_status();
        }
        if (!mesh->owner_manager_)
        {
            const RHIStatus status = begin_mesh(*mesh, meshes);
            if (!status)
            {
                return status;
            }
        }
        meshes.at(mesh.get()) = mesh;
        return RHIStatus::success();
    }

    template <typename T>
    RHIStatus RenderResourceManager::release_mesh(T& mesh, std::unordered_map<T*, std::shared_ptr<T>>& meshes)
    {
        if (!is_on_owner_thread())
        {
            return invalid_caller_status();
        }
        if (terminal_)
        {
            return terminal_status();
        }
        if (mesh.ref_count_ != 0 || (mesh.owner_manager_ && mesh.owner_manager_ != this))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Mesh release requires this manager and no rendering references");
        }
        // Keep every member buffer alive while release removes its borrowed
        // upload addresses, even if the manager pin is the final CPU owner.
        const auto found = meshes.find(&mesh);
        const std::shared_ptr<T> owner = found != meshes.end() ? found->second : nullptr;
        mesh.reset_vertex_factory();
        for (RenderResource* resource : mesh.resources())
        {
            if (resource)
            {
                const RHIStatus status = release(*resource);
                if (!status)
                {
                    return status;
                }
            }
        }
        mesh.owner_manager_ = nullptr;
        mesh.reclaim_requested_ = false;
        meshes.erase(&mesh);
        return RHIStatus::success();
    }

    template <typename T>
    RHIStatus RenderResourceManager::collect_mesh_reclaims(std::unordered_map<T*, std::shared_ptr<T>>& meshes)
    {
        for (auto entry = meshes.begin(); entry != meshes.end();)
        {
            T* const mesh = entry->first;
            ++entry;
            if (mesh->reclaim_requested_ && mesh->ref_count_ == 0)
            {
                const RHIStatus status = release_mesh(*mesh, meshes);
                if (!status)
                {
                    return status;
                }
            }
        }
        return RHIStatus::success();
    }

    template <typename T>
    void RenderResourceManager::discard_mesh_recordings(std::unordered_map<T*, std::shared_ptr<T>>& meshes)
    {
        for (const auto& entry : meshes)
        {
            T& mesh = *entry.first;
            if (mesh.state() == RenderResourceState::Ready || mesh.state() == RenderResourceState::Uninitialized)
            {
                continue;
            }
            const bool failed = mesh.state() == RenderResourceState::Failed;
            mesh.reset_vertex_factory();
            for (RenderResource* resource : mesh.resources())
            {
                if (!resource)
                {
                    continue;
                }
                resource->on_recording_discarded();
                if (failed)
                {
                    // Preserve the failed leaf's diagnostic, but cancel all its
                    // siblings so an incomplete mesh cannot publish or retry.
                    remove_pending(resource);
                    resource->pending_manager_ = nullptr;
                    if (resource->state_ != RenderResourceState::Failed)
                    {
                        resource->state_ = RenderResourceState::Uninitialized;
                    }
                }
            }
        }
    }

    template <typename T> void RenderResourceManager::clear_meshes(std::unordered_map<T*, std::shared_ptr<T>>& meshes)
    {
        for (const auto& entry : meshes)
        {
            // Leaf resources are already detached. Drop cached stream bindings
            // before owner pins or the device can disappear.
            entry.first->reset_vertex_factory();
            entry.first->owner_manager_ = nullptr;
            entry.first->reclaim_requested_ = false;
        }
        meshes.clear();
    }

    RHIStatus RenderResourceManager::begin_init(StaticMeshRenderData& mesh)
    {
        return begin_mesh(mesh, static_meshes_);
    }

    RHIStatus RenderResourceManager::begin_init(SkeletalMeshRenderData& mesh)
    {
        return begin_mesh(mesh, skeletal_meshes_);
    }

    RHIStatus RenderResourceManager::register_resource(const std::shared_ptr<StaticMeshRenderData>& mesh)
    {
        return register_mesh(mesh, static_meshes_);
    }

    RHIStatus RenderResourceManager::register_resource(const std::shared_ptr<SkeletalMeshRenderData>& mesh)
    {
        return register_mesh(mesh, skeletal_meshes_);
    }

    RHIStatus RenderResourceManager::release(StaticMeshRenderData& mesh)
    {
        return release_mesh(mesh, static_meshes_);
    }

    RHIStatus RenderResourceManager::release(SkeletalMeshRenderData& mesh)
    {
        return release_mesh(mesh, skeletal_meshes_);
    }

    void RenderResourceManager::detach_destroyed_resource(RenderResource& resource) noexcept
    {
        assert(is_on_owner_thread());
        assert(resource.pending_manager_ == nullptr && resource.ref_count_ == 0);
        borrowed_resources_.erase(std::remove(borrowed_resources_.begin(), borrowed_resources_.end(), &resource),
                                  borrowed_resources_.end());
        resource.owner_manager_ = nullptr;
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
        if (resource.owner_manager_ != nullptr && resource.owner_manager_ != this)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "RenderResource belongs to another manager");
        }
        if (resource.state_ != RenderResourceState::Uninitialized || resource.pending_manager_ != nullptr)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "RenderResource begin_init requires an uninitialized resource");
        }

        resource.pending_manager_ = this;
        if (resource.owner_manager_ == nullptr)
        {
            borrowed_resources_.push_back(&resource);
            resource.owner_manager_ = this;
        }
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
        if (resource.owner_manager_ != nullptr && resource.owner_manager_ != this)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "RenderResource belongs to another manager");
        }
        if (resource.state_ != RenderResourceState::Ready || resource.pending_manager_ != nullptr)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "RenderResource begin_update requires a Ready resource without pending work");
        }
        if (!resource.supports_update())
        {
            return RHIStatus::failure(RHIErrorCode::Unsupported,
                                      "This immutable RenderResource does not support updates");
        }

        // Ready remains the published long-term state while the manager's
        // private collections carry the retryable update transaction.
        resource.pending_manager_ = this;
        resource.failure_status_ = RHIStatus::success();
        pending_resources_.push_back(&resource);
        return RHIStatus::success();
    }

    RHIStatus RenderResourceManager::record_pending_uploads(RHIGraphicsCommandContext& context)
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
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "RenderResourceManager recording must be committed or discarded before reuse");
        }
        const RHIStatus reclaimed = collect_reclaims();
        if (!reclaimed)
        {
            return reclaimed;
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
                return RHIStatus::failure(RHIErrorCode::BackendFailure,
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
                    (recorded.code() == RHIErrorCode::InvalidArgument || recorded.code() == RHIErrorCode::Unsupported);
                if (resource->state_ == RenderResourceState::Failed || deterministic_update_failure)
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
                return RHIStatus::failure(RHIErrorCode::BackendFailure,
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
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "A failed RenderResource recording can only be discarded");
        }

        for (RenderResource* const resource : recording_resources_)
        {
            if (resource == nullptr || resource->pending_manager_ != this ||
                (resource->state_ != RenderResourceState::PendingUpload &&
                 resource->state_ != RenderResourceState::Ready))
            {
                return RHIStatus::failure(RHIErrorCode::BackendFailure,
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
        discard_mesh_recordings(static_meshes_);
        discard_mesh_recordings(skeletal_meshes_);
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
        if (resource.ref_count_ != 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "RenderResource still has rendering references");
        }
        if (resource.owner_manager_ != nullptr && resource.owner_manager_ != this)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "RenderResource belongs to another manager");
        }
        if (resource.pending_manager_ != nullptr && resource.pending_manager_ != this)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Pending RenderResource belongs to another manager");
        }

        if (remove_recording(&resource))
        {
            resource.on_recording_discarded();
        }
        remove_pending(&resource);
        resource.pending_manager_ = nullptr;
        resource.release_rhi();
        resource.state_ = RenderResourceState::Uninitialized;
        resource.failure_status_ = RHIStatus::success();
        resource.reclaim_requested_ = false;
        resource.owner_manager_ = nullptr;
        borrowed_resources_.erase(std::remove(borrowed_resources_.begin(), borrowed_resources_.end(), &resource),
                                  borrowed_resources_.end());
        registered_resources_.erase(std::remove_if(registered_resources_.begin(), registered_resources_.end(),
                                                   [&resource](const std::shared_ptr<RenderResource>& entry)
                                                   {
                                                       return entry.get() == &resource;
                                                   }),
                                    registered_resources_.end());
        return RHIStatus::success();
    }

    RHIStatus RenderResourceManager::collect_reclaims()
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
            return RHIStatus::failure(RHIErrorCode::NotReady, "Resolve recording before reclaiming RenderResources");
        }
        RHIStatus status = collect_mesh_reclaims(static_meshes_);
        if (!status)
        {
            return status;
        }
        status = collect_mesh_reclaims(skeletal_meshes_);
        if (!status)
        {
            return status;
        }
        for (std::size_t i = 0; i < registered_resources_.size();)
        {
            // Keep the representation alive while release removes the manager pin.
            const auto resource = registered_resources_[i];
            if (resource->reclaim_requested_ && resource->ref_count_ == 0)
            {
                const RHIStatus status = release(*resource);
                if (!status)
                {
                    return status;
                }
            }
            else
            {
                ++i;
            }
        }
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
        for (RenderResource* resource : borrowed_resources_)
        {
            resource->owner_manager_ = nullptr;
            resource->release_rhi();
            resource->state_ = RenderResourceState::Uninitialized;
            resource->failure_status_ = RHIStatus::success();
        }
        borrowed_resources_.clear();
        for (const auto& resource : registered_resources_)
        {
            resource->release_rhi();
            resource->state_ = RenderResourceState::Uninitialized;
            resource->failure_status_ = RHIStatus::success();
            resource->owner_manager_ = nullptr;
            resource->reclaim_requested_ = false;
        }
        registered_resources_.clear();
        clear_meshes(static_meshes_);
        clear_meshes(skeletal_meshes_);
        terminal_ = true;
        return RHIStatus::success();
    }

    bool RenderResourceManager::is_on_owner_thread() const
    {
        return std::this_thread::get_id() == owner_thread_id_;
    }

    void RenderResourceManager::remove_pending(RenderResource* resource)
    {
        pending_resources_.erase(std::remove(pending_resources_.begin(), pending_resources_.end(), resource),
                                 pending_resources_.end());
    }

    bool RenderResourceManager::remove_recording(RenderResource* resource)
    {
        const auto found = std::find(recording_resources_.begin(), recording_resources_.end(), resource);
        if (found == recording_resources_.end())
        {
            return false;
        }
        recording_resources_.erase(found);
        return true;
    }
} // namespace toy3d
