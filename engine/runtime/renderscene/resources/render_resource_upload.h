#pragma once

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/render_resource_update.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace toy3d
{
    struct MeshRHIResource
    {
        MeshRenderResourceVersionRef source_version;
        RHIBufferRef vertex_buffer;
        RHIBufferRef index_buffer;
        RHIIndexFormat index_format = RHIIndexFormat::UInt16;
        std::uint32_t vertex_stride = 0;
    };

    using MeshRHIResourceRef = std::shared_ptr<const MeshRHIResource>;

    // A recorded upload batch is intentionally separate from cache state.
    // It is published only after the enclosing viewport submission succeeds;
    // destroying it before commit leaves the CPU version pending for retry.
    class RenderResourceUploadBatch final
    {
    public:
        RenderResourceUploadBatch() = default;

        RenderResourceUploadBatch(const RenderResourceUploadBatch&) = delete;
        RenderResourceUploadBatch& operator=(const RenderResourceUploadBatch&) = delete;
        RenderResourceUploadBatch(RenderResourceUploadBatch&&) noexcept = default;
        RenderResourceUploadBatch& operator=(RenderResourceUploadBatch&&) noexcept = default;

        bool empty() const { return meshes_.empty(); }
        std::size_t mesh_count() const { return meshes_.size(); }

    private:
        friend class RenderResourceCache;
        std::vector<MeshRHIResourceRef> meshes_;
    };
}
