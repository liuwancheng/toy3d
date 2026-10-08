#include "rendercore/geometry/static_mesh_render_data.h"

#include <cassert>
#include <cstring>
#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/geometry/mesh_buffer_upload.h"
#include "rendercore/render_resource_manager.h"

namespace toy3d
{
    // --------------------------------------------------------------------------
    // PositionVertexBuffer: 不可变 CPU 流与 RHI buffer
    // --------------------------------------------------------------------------
    PositionVertexBuffer::PositionVertexBuffer(const std::vector<StaticMeshVertex>& vertices)
    {
        initial_data_.reserve(vertices.size() * 4u);
        for (const StaticMeshVertex& vertex : vertices)
        {
            initial_data_.push_back(vertex.position.x);
            initial_data_.push_back(vertex.position.y);
            initial_data_.push_back(vertex.position.z);
            initial_data_.push_back(1.0f);
        }
    }

    RHIStatus PositionVertexBuffer::record_upload(RHIDevice& device, RHIGraphicsCommandContext& context)
    {
        bool deterministic_failure = false;
        const RHIStatus status = record_mesh_buffer_upload(
            device, context, initial_data_.data(), initial_data_.size() * sizeof(float), RHIResourceUsage::VertexBuffer,
            RHIAccess::VertexBuffer, "StaticMesh.PositionVertexBuffer", rhi_buffer_, deterministic_failure);
        return !status && deterministic_failure ? fail(status) : status;
    }

    void PositionVertexBuffer::on_recording_committed() noexcept
    {
        // Retain the immutable CPU stream for future residency uploads.
    }

    void PositionVertexBuffer::on_recording_discarded() noexcept
    {
        release_rhi();
    }

    void PositionVertexBuffer::release_rhi() noexcept
    {
        rhi_buffer_.reset();
    }

    // --------------------------------------------------------------------------
    // StaticMeshVertexBuffer: 不可变 CPU 流与 RHI buffer
    // --------------------------------------------------------------------------
    StaticMeshVertexBuffer::StaticMeshVertexBuffer(const std::vector<StaticMeshVertex>& vertices)
    {
        initial_data_.reserve(vertices.size() * 10u);
        for (const StaticMeshVertex& vertex : vertices)
        {
            initial_data_.push_back(vertex.normal.x);
            initial_data_.push_back(vertex.normal.y);
            initial_data_.push_back(vertex.normal.z);
            initial_data_.push_back(0.0f);
            initial_data_.push_back(vertex.uv0.x);
            initial_data_.push_back(vertex.uv0.y);
            initial_data_.push_back(vertex.tangent.x);
            initial_data_.push_back(vertex.tangent.y);
            initial_data_.push_back(vertex.tangent.z);
            initial_data_.push_back(vertex.tangent.w);
        }
    }

    RHIStatus StaticMeshVertexBuffer::record_upload(RHIDevice& device, RHIGraphicsCommandContext& context)
    {
        bool deterministic_failure = false;
        const RHIStatus status = record_mesh_buffer_upload(
            device, context, initial_data_.data(), initial_data_.size() * sizeof(float), RHIResourceUsage::VertexBuffer,
            RHIAccess::VertexBuffer, "StaticMesh.StaticMeshVertexBuffer", rhi_buffer_, deterministic_failure);
        return !status && deterministic_failure ? fail(status) : status;
    }

    void StaticMeshVertexBuffer::on_recording_committed() noexcept
    {
        // Retain the immutable CPU stream for future residency uploads.
    }

    void StaticMeshVertexBuffer::on_recording_discarded() noexcept
    {
        release_rhi();
    }

    void StaticMeshVertexBuffer::release_rhi() noexcept
    {
        rhi_buffer_.reset();
    }

    // --------------------------------------------------------------------------
    // ColorVertexBuffer: 不可变 CPU 流与 RHI buffer
    // --------------------------------------------------------------------------
    ColorVertexBuffer::ColorVertexBuffer(std::vector<std::array<std::uint8_t, 4>> colors)
        : initial_data_(std::move(colors))
    {
    }

    RHIStatus ColorVertexBuffer::record_upload(RHIDevice& device, RHIGraphicsCommandContext& context)
    {
        bool deterministic_failure = false;
        const RHIStatus status = record_mesh_buffer_upload(
            device, context, initial_data_.data(), initial_data_.size() * sizeof(initial_data_[0]),
            RHIResourceUsage::VertexBuffer, RHIAccess::VertexBuffer, "StaticMesh.ColorVertexBuffer", rhi_buffer_,
            deterministic_failure);
        return !status && deterministic_failure ? fail(status) : status;
    }

    void ColorVertexBuffer::on_recording_committed() noexcept
    {
        // Retain the immutable CPU stream for future residency uploads.
    }

    void ColorVertexBuffer::on_recording_discarded() noexcept
    {
        release_rhi();
    }

    void ColorVertexBuffer::release_rhi() noexcept
    {
        rhi_buffer_.reset();
    }

    // --------------------------------------------------------------------------
    // StaticMeshIndexBuffer: 不可变 CPU 流与 RHI buffer
    // --------------------------------------------------------------------------
    StaticMeshIndexBuffer::StaticMeshIndexBuffer(const StaticMeshIndexData& indices)
    {
        // The fixed two-width variant is decoded explicitly so the RHI binding
        // format and copied byte payload always describe the same index width.
        if (const auto* indices_u16 = std::get_if<std::vector<std::uint16_t>>(&indices))
        {
            format_ = RHIIndexFormat::UInt16;
            initial_data_.resize(indices_u16->size() * sizeof(std::uint16_t));
            std::memcpy(initial_data_.data(), indices_u16->data(), initial_data_.size());
        }
        else if (const auto* indices_u32 = std::get_if<std::vector<std::uint32_t>>(&indices))
        {
            format_ = RHIIndexFormat::UInt32;
            initial_data_.resize(indices_u32->size() * sizeof(std::uint32_t));
            std::memcpy(initial_data_.data(), indices_u32->data(), initial_data_.size());
        }
    }

    RHIStatus StaticMeshIndexBuffer::record_upload(RHIDevice& device, RHIGraphicsCommandContext& context)
    {
        bool deterministic_failure = false;
        const RHIStatus status = record_mesh_buffer_upload(
            device, context, initial_data_.data(), initial_data_.size(), RHIResourceUsage::IndexBuffer,
            RHIAccess::IndexBuffer, "StaticMesh.StaticMeshIndexBuffer", rhi_buffer_, deterministic_failure);
        return !status && deterministic_failure ? fail(status) : status;
    }

    void StaticMeshIndexBuffer::on_recording_committed() noexcept
    {
        // Retain the immutable CPU stream for future residency uploads.
    }

    void StaticMeshIndexBuffer::on_recording_discarded() noexcept
    {
        release_rhi();
    }

    void StaticMeshIndexBuffer::release_rhi() noexcept
    {
        rhi_buffer_.reset();
    }

    // --------------------------------------------------------------------------
    // StaticMeshRenderData: 完整网格的上传与驻留
    // --------------------------------------------------------------------------
    StaticMeshRenderData::StaticMeshRenderData(const StaticMesh& static_mesh)
        : position_vertex_buffer_(static_mesh.vertices()), static_mesh_vertex_buffer_(static_mesh.vertices()),
          color_vertex_buffer_(static_mesh.vertex_colors().empty()
                                   ? nullptr
                                   : std::make_unique<ColorVertexBuffer>(static_mesh.vertex_colors())),
          index_buffer_(static_mesh.indices()), sections_(static_mesh.sections())
    {
        valid_tangent_frame_ = static_mesh.has_valid_tangent_frame();
        // C++17 get_if keeps the fixed 16/32-bit index alternatives explicit
        // while retaining a width-independent range limit for section checks.
        const auto* indices_u16 = std::get_if<std::vector<std::uint16_t>>(&static_mesh.indices());
        if (indices_u16 != nullptr)
        {
            index_count_ = indices_u16->size();
        }
        else if (const auto* indices_u32 = std::get_if<std::vector<std::uint32_t>>(&static_mesh.indices()))
        {
            index_count_ = indices_u32->size();
        }
    }

    StaticMeshRenderData::~StaticMeshRenderData()
    {
        assert(ref_count_ == 0);
        if (owner_manager_)
        {
            const RHIStatus status = owner_manager_->release(*this);
            assert(status.succeeded());
        }
    }

    std::array<RenderResource*, 4> StaticMeshRenderData::resources()
    {
        return {&position_vertex_buffer_, &static_mesh_vertex_buffer_, color_vertex_buffer_.get(), &index_buffer_};
    }

    std::array<const RenderResource*, 4> StaticMeshRenderData::resources() const
    {
        return {&position_vertex_buffer_, &static_mesh_vertex_buffer_, color_vertex_buffer_.get(), &index_buffer_};
    }

    RHIStatus StaticMeshRenderData::validate_geometry() const
    {
        return RHIStatus::success();
    }

    void StaticMeshRenderData::reset_vertex_factory() noexcept
    {
        local_vertex_factory_.reset();
    }

    RenderResourceState StaticMeshRenderData::state() const
    {
        bool pending = false;
        bool ready = true;
        for (const RenderResource* resource : resources())
        {
            if (!resource)
            {
                continue;
            }
            if (resource->state() == RenderResourceState::Failed)
            {
                return RenderResourceState::Failed;
            }
            pending = pending || resource->state() == RenderResourceState::PendingUpload;
            ready = ready && resource->state() == RenderResourceState::Ready;
        }
        return pending ? RenderResourceState::PendingUpload
               : ready ? RenderResourceState::Ready
                       : RenderResourceState::Uninitialized;
    }

    RHIStatus StaticMeshRenderData::failure_status() const
    {
        for (const RenderResource* resource : resources())
        {
            if (resource && resource->state() == RenderResourceState::Failed)
            {
                return resource->failure_status();
            }
        }
        return RHIStatus::success();
    }

    void StaticMeshRenderData::retain() noexcept
    {
        assert(ref_thread_ == std::this_thread::get_id());
        ++ref_count_;
        reclaim_requested_ = false;
    }

    void StaticMeshRenderData::release() noexcept
    {
        assert(ref_thread_ == std::this_thread::get_id());
        assert(ref_count_ > 0);
        if (--ref_count_ == 0)
        {
            reclaim_requested_ = true;
        }
    }

    RHIStatus StaticMeshRenderData::prepare_current_recording()
    {
        if (is_drawable())
        {
            const RHIStatus stream_status = local_vertex_factory_->validate_streams();
            if (stream_status)
            {
                return RHIStatus::success();
            }
        }

        local_vertex_factory_.reset();
        if (!position_vertex_buffer_.buffer() || !static_mesh_vertex_buffer_.buffer() || !index_buffer_.buffer() ||
            (color_vertex_buffer_ && !color_vertex_buffer_->buffer()))
        {
            return RHIStatus::failure(RHIErrorCode::NotReady,
                                      "StaticMeshRenderData requires every candidate buffer in the current recording");
        }

        std::vector<VertexStreamComponent> components;
        components.push_back({ShaderVertexAttributeId::Position0, 0u, 0u, position_vertex_buffer_.stride(),
                              PixelFormat::R32G32B32A32Float, position_vertex_buffer_.buffer()});
        components.push_back({ShaderVertexAttributeId::Normal0, 1u, 0u, static_mesh_vertex_buffer_.stride(),
                              PixelFormat::R32G32B32A32Float, static_mesh_vertex_buffer_.buffer()});
        components.push_back({ShaderVertexAttributeId::TexCoord0, 1u, 16u, static_mesh_vertex_buffer_.stride(),
                              PixelFormat::R32G32Float, static_mesh_vertex_buffer_.buffer()});
        components.push_back({ShaderVertexAttributeId::Tangent0, 1u, 24u, static_mesh_vertex_buffer_.stride(),
                              PixelFormat::R32G32B32A32Float, static_mesh_vertex_buffer_.buffer()});
        if (color_vertex_buffer_)
        {
            components.push_back({ShaderVertexAttributeId::Color0, 2u, 0u, color_vertex_buffer_->stride(),
                                  PixelFormat::R8G8B8A8UNorm, color_vertex_buffer_->buffer()});
        }

        auto candidate = std::make_unique<LocalVertexFactory>(std::move(components));
        const RHIStatus validation = candidate->validate_streams();
        if (!validation)
        {
            return validation;
        }
        local_vertex_factory_ = std::move(candidate);
        return RHIStatus::success();
    }

    bool StaticMeshRenderData::is_drawable() const
    {
        return local_vertex_factory_ != nullptr && position_vertex_buffer_.buffer() != nullptr &&
               static_mesh_vertex_buffer_.buffer() != nullptr && index_buffer_.buffer() != nullptr &&
               (!color_vertex_buffer_ || color_vertex_buffer_->buffer() != nullptr);
    }

    RHIIndexBufferBinding StaticMeshRenderData::index_buffer_binding() const
    {
        RHIIndexBufferBinding binding;
        binding.buffer = index_buffer_.buffer();
        binding.format = index_buffer_.format();
        return binding;
    }
} // namespace toy3d
