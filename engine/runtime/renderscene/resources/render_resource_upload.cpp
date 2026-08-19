#include "renderscene/resources/render_resource_cache.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        struct IndexUploadSource
        {
            const void* data = nullptr;
            std::size_t size = 0;
            RHIIndexFormat format = RHIIndexFormat::UInt16;
        };

        IndexUploadSource index_upload_source(const StaticMeshIndexData& indices)
        {
            // The closed index-data variant selects both the byte width and
            // the public RHI binding format. Explicit branches keep that ABI
            // choice visible at the upload boundary.
            if (const auto* indices_u16 =
                std::get_if<std::vector<std::uint16_t>>(&indices))
            {
                return {
                    indices_u16->data(),
                    indices_u16->size() * sizeof(std::uint16_t),
                    RHIIndexFormat::UInt16};
            }
            if (const auto* indices_u32 =
                std::get_if<std::vector<std::uint32_t>>(&indices))
            {
                return {
                    indices_u32->data(),
                    indices_u32->size() * sizeof(std::uint32_t),
                    RHIIndexFormat::UInt32};
            }
            return {};
        }

        RHIStatus record_buffer_upload(
            RHIGraphicsCommandContext& context,
            const RHIBufferRef& buffer,
            const void* data,
            std::size_t size,
            RHIAccess final_access)
        {
            RHIResourceTransition to_copy;
            to_copy.resource = buffer;
            to_copy.before = RHIAccess::Common;
            to_copy.after = RHIAccess::CopyDestination;
            RHIStatus status = context.transition_resources({to_copy});
            if (!status)
            {
                return status;
            }

            RHIBufferUploadDesc upload;
            upload.destination = buffer;
            upload.source.data = data;
            upload.source.size = size;
            status = context.upload_buffer(upload);
            if (!status)
            {
                return status;
            }

            RHIResourceTransition to_final;
            to_final.resource = buffer;
            to_final.before = RHIAccess::CopyDestination;
            to_final.after = final_access;
            return context.transition_resources({to_final});
        }

        RHIResult<MeshRHIResourceRef> record_mesh_upload(
            RHIDevice& device,
            RHIGraphicsCommandContext& context,
            const MeshRenderResourceVersionRef& version)
        {
            const std::size_t vertex_size =
                version->vertices.size() * sizeof(StaticMeshVertex);
            const IndexUploadSource index_source =
                index_upload_source(version->indices);
            if (vertex_size == 0 || index_source.data == nullptr ||
                index_source.size == 0 ||
                vertex_size > std::numeric_limits<std::uint64_t>::max() ||
                index_source.size > std::numeric_limits<std::uint64_t>::max())
            {
                return RHIResult<MeshRHIResourceRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "A pending Mesh upload requires non-empty addressable vertex and index data.");
            }

            RHIBufferDesc vertex_desc;
            vertex_desc.size = static_cast<std::uint64_t>(vertex_size);
            vertex_desc.usage = rhi_enum_or(
                RHIResourceUsage::VertexBuffer,
                RHIResourceUsage::CopyDestination);
            vertex_desc.initial_access = RHIAccess::Common;
            vertex_desc.debug_name = "Mesh vertex buffer";
            auto vertex_result = device.create_buffer(vertex_desc);
            if (!vertex_result)
            {
                return RHIResult<MeshRHIResourceRef>::failure(
                    vertex_result.status().code(), vertex_result.status().message());
            }

            RHIBufferDesc index_desc;
            index_desc.size = static_cast<std::uint64_t>(index_source.size);
            index_desc.usage = rhi_enum_or(
                RHIResourceUsage::IndexBuffer,
                RHIResourceUsage::CopyDestination);
            index_desc.initial_access = RHIAccess::Common;
            index_desc.debug_name = "Mesh index buffer";
            auto index_result = device.create_buffer(index_desc);
            if (!index_result)
            {
                return RHIResult<MeshRHIResourceRef>::failure(
                    index_result.status().code(), index_result.status().message());
            }

            RHIBufferRef vertex_buffer = std::move(vertex_result).value();
            RHIBufferRef index_buffer = std::move(index_result).value();
            RHIStatus status = record_buffer_upload(
                context, vertex_buffer, version->vertices.data(), vertex_size,
                RHIAccess::VertexBuffer);
            if (!status)
            {
                return RHIResult<MeshRHIResourceRef>::failure(
                    status.code(), status.message());
            }
            status = record_buffer_upload(
                context, index_buffer, index_source.data, index_source.size,
                RHIAccess::IndexBuffer);
            if (!status)
            {
                return RHIResult<MeshRHIResourceRef>::failure(
                    status.code(), status.message());
            }

            auto resource = std::make_shared<MeshRHIResource>();
            resource->source_version = version;
            resource->vertex_buffer = std::move(vertex_buffer);
            resource->index_buffer = std::move(index_buffer);
            resource->index_format = index_source.format;
            resource->vertex_stride = sizeof(StaticMeshVertex);
            return RHIResult<MeshRHIResourceRef>::success(std::move(resource));
        }
    }

    RHIResult<RenderResourceUploadBatch>
    RenderResourceCache::record_pending_mesh_uploads(
        RHIDevice& device,
        RHIGraphicsCommandContext& context) const
    {
        std::vector<std::uint64_t> pending_ids;
        pending_ids.reserve(meshes_.size());
        for (const auto& entry : meshes_)
        {
            const auto uploaded = mesh_rhi_resources_.find(entry.first);
            if (uploaded == mesh_rhi_resources_.end() ||
                uploaded->second->source_version != entry.second)
            {
                pending_ids.push_back(entry.first);
            }
        }
        std::sort(pending_ids.begin(), pending_ids.end());

        RenderResourceUploadBatch batch;
        batch.meshes_.reserve(pending_ids.size());
        for (std::uint64_t resource_id : pending_ids)
        {
            auto upload_result = record_mesh_upload(
                device, context, meshes_.at(resource_id));
            if (!upload_result)
            {
                return RHIResult<RenderResourceUploadBatch>::failure(
                    upload_result.status().code(),
                    upload_result.status().message());
            }
            batch.meshes_.push_back(std::move(upload_result).value());
        }
        return RHIResult<RenderResourceUploadBatch>::success(std::move(batch));
    }

    RHIStatus RenderResourceCache::commit_uploads(
        RenderResourceUploadBatch batch)
    {
        for (const MeshRHIResourceRef& resource : batch.meshes_)
        {
            if (resource == nullptr || resource->source_version == nullptr)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "A committed Mesh upload must retain its immutable CPU source version.");
            }
            const std::uint64_t resource_id =
                resource->source_version->resource_id.value();
            const auto latest = meshes_.find(resource_id);
            if (latest == meshes_.end() ||
                latest->second != resource->source_version)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "A Mesh upload batch cannot replace a newer or released cache version.");
            }
        }

        for (MeshRHIResourceRef& resource : batch.meshes_)
        {
            mesh_rhi_resources_[resource->source_version->resource_id.value()] =
                std::move(resource);
        }
        return RHIStatus::success();
    }

    RenderResourceResolveResult<MeshRHIResourceRef>
    RenderResourceCache::resolve_mesh_rhi(
        MeshRenderResourceId resource_id) const
    {
        const auto latest = meshes_.find(resource_id.value());
        const auto uploaded = mesh_rhi_resources_.find(resource_id.value());
        if (!resource_id || latest == meshes_.end() ||
            uploaded == mesh_rhi_resources_.end() ||
            uploaded->second->source_version != latest->second)
        {
            return {};
        }
        return {RenderResourceResolveState::Found, uploaded->second};
    }

    void RenderResourceCache::prune_stale_mesh_rhi_resource(
        MeshRenderResourceId resource_id)
    {
        const auto latest = meshes_.find(resource_id.value());
        const auto uploaded = mesh_rhi_resources_.find(resource_id.value());
        if (uploaded != mesh_rhi_resources_.end() &&
            (latest == meshes_.end() ||
                uploaded->second->source_version != latest->second))
        {
            mesh_rhi_resources_.erase(uploaded);
        }
    }
}
