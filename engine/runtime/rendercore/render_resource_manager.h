#pragma once

#include "drivers/rhi/rhi_result.h"
#include "rendercore/render_resource.h"

#include <thread>
#include <unordered_map>
#include <vector>

namespace toy3d
{
    class RHIGraphicsCommandContext;
    class RHIDevice;
    class RenderResource;
    class StaticMeshRenderData;
    class SkeletalMeshRenderData;

    // Renderer-owned RT coordinator. Registered shared resources are pinned
    // until reclamation/detach; upload collections borrow those stable addresses.
    class RenderResourceManager final
    {
      public:
        explicit RenderResourceManager(RHIDevice& device);
        ~RenderResourceManager();

        RenderResourceManager(const RenderResourceManager&) = delete;
        RenderResourceManager& operator=(const RenderResourceManager&) = delete;
        RenderResourceManager(RenderResourceManager&&) = delete;
        RenderResourceManager& operator=(RenderResourceManager&&) = delete;

        RHIStatus begin_init(RenderResource& resource);
        RHIStatus begin_init(StaticMeshRenderData& mesh);
        RHIStatus begin_init(SkeletalMeshRenderData& mesh);
        RHIStatus begin_update(RenderResource& resource);
        RHIStatus record_pending_uploads(RHIGraphicsCommandContext& context);
        RHIStatus commit_recording();
        RHIStatus discard_recording();
        RHIStatus release(RenderResource& resource);
        RHIStatus release(StaticMeshRenderData& mesh);
        RHIStatus release(SkeletalMeshRenderData& mesh);
        RHIStatus clear_for_terminal();
        RHIStatus collect_reclaims();

        template <typename T> RHIResult<RenderResourceRef<T>> acquire(T& resource)
        {
            // C++17 weak_from_this diagnoses missing shared ownership without
            // throwing on borrowed bootstrap/test resources.
            auto owner = resource.weak_from_this().lock();
            if (!owner)
            {
                return RHIResult<RenderResourceRef<T>>::failure(
                    RHIErrorCode::InvalidArgument, "RenderResourceRef requires shared representation ownership");
            }
            const RHIStatus status = register_resource(owner);
            if (!status)
            {
                return RHIResult<RenderResourceRef<T>>::failure(status.code(), status.message());
            }
            return RHIResult<RenderResourceRef<T>>::success(
                RenderResourceRef<T>(std::static_pointer_cast<T>(std::move(owner))));
        }

      private:
        friend class RenderResource;

        bool is_on_owner_thread() const;
        RHIStatus register_resource(const std::shared_ptr<RenderResource>& resource);
        RHIStatus register_resource(const std::shared_ptr<StaticMeshRenderData>& mesh);
        RHIStatus register_resource(const std::shared_ptr<SkeletalMeshRenderData>& mesh);
        template <typename T> RHIStatus begin_mesh(T& mesh, std::unordered_map<T*, std::shared_ptr<T>>& meshes);
        template <typename T>
        RHIStatus register_mesh(const std::shared_ptr<T>& mesh, std::unordered_map<T*, std::shared_ptr<T>>& meshes);
        template <typename T> RHIStatus release_mesh(T& mesh, std::unordered_map<T*, std::shared_ptr<T>>& meshes);
        template <typename T> RHIStatus collect_mesh_reclaims(std::unordered_map<T*, std::shared_ptr<T>>& meshes);
        template <typename T> void discard_mesh_recordings(std::unordered_map<T*, std::shared_ptr<T>>& meshes);
        template <typename T> void clear_meshes(std::unordered_map<T*, std::shared_ptr<T>>& meshes);
        void detach_destroyed_resource(RenderResource& resource) noexcept;
        void remove_pending(RenderResource* resource);
        bool remove_recording(RenderResource* resource);

        const std::thread::id owner_thread_id_;
        RHIDevice& device_;
        std::vector<RenderResource*> pending_resources_;
        std::vector<RenderResource*> recording_resources_;
        std::vector<std::shared_ptr<RenderResource>> registered_resources_;
        // Shared admission pins the mesh owner, never individual member buffers.
        // Null mapped ownership denotes an explicitly RT-owned test mesh.
        std::unordered_map<StaticMeshRenderData*, std::shared_ptr<StaticMeshRenderData>> static_meshes_;
        std::unordered_map<SkeletalMeshRenderData*, std::shared_ptr<SkeletalMeshRenderData>> skeletal_meshes_;
        // Explicitly owned bootstrap/test resources also retain device identity
        // after commit, but their lifetime remains with the calling RT owner.
        std::vector<RenderResource*> borrowed_resources_;
        bool recording_failed_ = false;
        bool terminal_ = false;
    };
} // namespace toy3d
