#include "rendercore/geometry/skin_weight_vertex_buffer.h"

#include "rendercore/geometry/mesh_buffer_upload.h"

namespace toy3d
{
    SkinWeightVertexBuffer::SkinWeightVertexBuffer(const std::vector<SkinWeights>& weights,
                                                   std::uint32_t num_bone_influences)
        : num_bone_influences_(num_bone_influences)
    {
        if (num_bone_influences != skin_influences_per_group && num_bone_influences != max_skin_influences)
        {
            return;
        }
        bytes_.reserve(weights.size() * stride());
        for (const auto& weight : weights)
        {
            bytes_.insert(bytes_.end(), weight.bone_indices.begin(), weight.bone_indices.begin() + num_bone_influences);
            bytes_.insert(bytes_.end(), weight.weights.begin(), weight.weights.begin() + num_bone_influences);
        }
    }

    RHIStatus SkinWeightVertexBuffer::record_upload(RHIDevice& device, RHIGraphicsCommandContext& context)
    {
        bool deterministic = false;
        if (num_bone_influences_ != skin_influences_per_group && num_bone_influences_ != max_skin_influences)
        {
            return fail(
                RHIStatus::failure(RHIErrorCode::InvalidArgument, "Skin storage must have four or eight slots."));
        }
        const auto status =
            record_mesh_buffer_upload(device, context, bytes_.data(), bytes_.size(), RHIResourceUsage::VertexBuffer,
                                      RHIAccess::VertexBuffer, "SkeletalMesh.SkinWeights", buffer_, deterministic);
        return !status && deterministic ? fail(status) : status;
    }

    void SkinWeightVertexBuffer::on_recording_committed() noexcept
    {
        // Keep the immutable weights for later residency uploads.
    }

    void SkinWeightVertexBuffer::on_recording_discarded() noexcept
    {
        release_rhi();
    }

    void SkinWeightVertexBuffer::release_rhi() noexcept
    {
        buffer_.reset();
    }
} // namespace toy3d
