#pragma once

#include "drivers/rhi/rhi_result.h"

#include <thread>
#include <vector>

namespace toy3d
{
    class RHIGraphicsCommandContext;
    class RHIDevice;
    class RenderResource;

    // Renderer-owned, Render-side lifecycle coordinator. Collections are
    // strictly non-owning and are never an Asset cache or identity registry.
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
        RHIStatus begin_update(RenderResource& resource);
        RHIStatus record_pending_uploads(
            RHIGraphicsCommandContext& context);
        RHIStatus commit_recording();
        RHIStatus discard_recording();
        RHIStatus release(RenderResource& resource);
        RHIStatus clear_for_terminal();

    private:
        bool is_on_owner_thread() const;
        void remove_pending(RenderResource* resource);
        bool remove_recording(RenderResource* resource);

        const std::thread::id owner_thread_id_;
        RHIDevice& device_;
        std::vector<RenderResource*> pending_resources_;
        std::vector<RenderResource*> recording_resources_;
        bool recording_failed_ = false;
        bool terminal_ = false;
    };
}
