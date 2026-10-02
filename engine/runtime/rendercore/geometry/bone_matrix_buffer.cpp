#include "rendercore/geometry/bone_matrix_buffer.h"

#include <utility>

#include "asset/mesh/skeletal_mesh_asset.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/geometry/mesh_buffer_upload.h"

namespace toy3d
{
    BoneMatrixBuffer::BoneMatrixBuffer(const std::vector<Vector4>& rows)
    {
        valid_ = !rows.empty() && rows.size() % 6 == 0 && rows.size() <= max_section_bones * 6;
        if (!valid_)
        {
            return;
        }
        values_.reserve(rows.size() * 4);
        for (const auto& row : rows)
        {
            if (!is_finite(row))
            {
                valid_ = false;
                values_.clear();
                return;
            }
            // Explicit scalar rows preserve the texel contract independently of Matrix4 ABI.
            values_.insert(values_.end(), {row.x, row.y, row.z, row.w});
        }
    }

    RHIStatus BoneMatrixBuffer::record_upload(RHIDevice& device, RHIGraphicsCommandContext& context)
    {
        if (!valid_)
        {
            return fail(RHIStatus::failure(RHIErrorCode::InvalidArgument, "Invalid bone matrix rows."));
        }
        bool deterministic = false;
        RHIBufferRef candidate;
        const auto status = record_mesh_buffer_upload(device, context, values_.data(), values_.size() * sizeof(float),
                                                      RHIResourceUsage::ShaderResource | RHIResourceUsage::TypedBuffer,
                                                      RHIAccess::ShaderResourceGraphics, "SkeletalMesh.BoneMatrices",
                                                      candidate, deterministic);
        if (!status)
        {
            return deterministic ? fail(status) : status;
        }
        RHIBufferViewDesc view_desc;
        view_desc.format = PixelFormat::R32G32B32A32Float;
        view_desc.size = candidate->desc().size;
        const auto view = device.create_buffer_view(candidate, view_desc);
        if (!view)
        {
            return view.status().code() == RHIErrorCode::Unsupported ||
                           view.status().code() == RHIErrorCode::InvalidArgument
                       ? fail(view.status())
                       : view.status();
        }
        buffer_ = std::move(candidate);
        view_ = view.value();
        return RHIStatus::success();
    }

    void BoneMatrixBuffer::on_recording_committed() noexcept
    {
        std::vector<float>().swap(values_);
    }

    void BoneMatrixBuffer::on_recording_discarded() noexcept
    {
        view_.reset();
        buffer_.reset();
    }

    void BoneMatrixBuffer::release_rhi() noexcept
    {
        view_.reset();
        buffer_.reset();
        std::vector<float>().swap(values_);
    }
} // namespace toy3d
