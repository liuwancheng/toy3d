#include "renderscene/geometry/static_mesh_render_data.h"

#include <cstring>
#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "renderscene/render_resource_manager.h"

namespace toy3d
{
    namespace
    {
        RHIStatus record_buffer_upload(
            RHIDevice& device,
            RHIGraphicsCommandContext& context,
            const void* initial_data,
            std::size_t initial_data_size,
            RHIResourceUsage usage,
            RHIAccess final_access,
            const char* debug_name,
            RHIBufferRef& out_buffer,
            bool& out_deterministic_failure)
        {
            out_deterministic_failure = false;
            if (initial_data == nullptr || initial_data_size == 0u)
            {
                out_deterministic_failure = true;
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "StaticMesh buffer upload requires a non-empty initial payload");
            }

            RHIBufferDesc desc;
            desc.size = static_cast<std::uint64_t>(initial_data_size);
            desc.usage = rhi_enum_or(usage, RHIResourceUsage::CopyDestination);
            desc.initial_access = RHIAccess::Common;
            desc.debug_name = debug_name;
            RHIResult<RHIBufferRef> created = device.create_buffer(desc);
            if (!created)
            {
                out_deterministic_failure =
                    created.status().code() == RHIErrorCode::InvalidArgument ||
                    created.status().code() == RHIErrorCode::Unsupported;
                return created.status();
            }
            RHIBufferRef candidate = std::move(created).value();

            RHIResourceTransition to_copy;
            to_copy.resource = candidate;
            to_copy.before = RHIAccess::Common;
            to_copy.after = RHIAccess::CopyDestination;
            RHIStatus status = context.transition_resources({to_copy});
            if (!status)
            {
                return status;
            }

            RHIBufferUploadDesc upload;
            upload.destination = candidate;
            upload.source.data = initial_data;
            upload.source.size = initial_data_size;
            status = context.upload_buffer(upload);
            if (!status)
            {
                return status;
            }

            RHIResourceTransition to_final;
            to_final.resource = candidate;
            to_final.before = RHIAccess::CopyDestination;
            to_final.after = final_access;
            status = context.transition_resources({to_final});
            if (!status)
            {
                return status;
            }

            out_buffer = std::move(candidate);
            return RHIStatus::success();
        }

        void release_float_payload(std::vector<float>& payload)
        {
            std::vector<float>().swap(payload);
        }

        RHIStatus preserve_first_failure(
            const RHIStatus& first,
            const RHIStatus& next)
        {
            return first.succeeded() ? next : first;
        }
    }

    PositionVertexBuffer::PositionVertexBuffer(
        const std::vector<StaticMeshVertex>& vertices)
    {
        initial_data_.reserve(vertices.size() * 3u);
        for (const StaticMeshVertex& vertex : vertices)
        {
            initial_data_.push_back(vertex.position.x);
            initial_data_.push_back(vertex.position.y);
            initial_data_.push_back(vertex.position.z);
        }
    }

    RHIStatus PositionVertexBuffer::record_upload(
        RHIDevice& device,
        RHIGraphicsCommandContext& context)
    {
        bool deterministic_failure = false;
        const RHIStatus status = record_buffer_upload(
            device, context, initial_data_.data(),
            initial_data_.size() * sizeof(float),
            RHIResourceUsage::VertexBuffer, RHIAccess::VertexBuffer,
            "StaticMesh.PositionVertexBuffer", rhi_buffer_,
            deterministic_failure);
        return !status && deterministic_failure ? fail(status) : status;
    }

    void PositionVertexBuffer::on_recording_committed() noexcept
    {
        release_float_payload(initial_data_);
    }

    void PositionVertexBuffer::on_recording_discarded() noexcept
    {
        rhi_buffer_.reset();
    }

    void PositionVertexBuffer::release_rhi() noexcept
    {
        rhi_buffer_.reset();
        release_float_payload(initial_data_);
    }

    StaticMeshVertexBuffer::StaticMeshVertexBuffer(
        const std::vector<StaticMeshVertex>& vertices)
    {
        initial_data_.reserve(vertices.size() * 5u);
        for (const StaticMeshVertex& vertex : vertices)
        {
            initial_data_.push_back(vertex.normal.x);
            initial_data_.push_back(vertex.normal.y);
            initial_data_.push_back(vertex.normal.z);
            initial_data_.push_back(vertex.uv0.x);
            initial_data_.push_back(vertex.uv0.y);
        }
    }

    RHIStatus StaticMeshVertexBuffer::record_upload(
        RHIDevice& device,
        RHIGraphicsCommandContext& context)
    {
        bool deterministic_failure = false;
        const RHIStatus status = record_buffer_upload(
            device, context, initial_data_.data(),
            initial_data_.size() * sizeof(float),
            RHIResourceUsage::VertexBuffer, RHIAccess::VertexBuffer,
            "StaticMesh.StaticMeshVertexBuffer", rhi_buffer_,
            deterministic_failure);
        return !status && deterministic_failure ? fail(status) : status;
    }

    void StaticMeshVertexBuffer::on_recording_committed() noexcept
    {
        release_float_payload(initial_data_);
    }

    void StaticMeshVertexBuffer::on_recording_discarded() noexcept
    {
        rhi_buffer_.reset();
    }

    void StaticMeshVertexBuffer::release_rhi() noexcept
    {
        rhi_buffer_.reset();
        release_float_payload(initial_data_);
    }

    ColorVertexBuffer::ColorVertexBuffer(
        std::vector<std::array<std::uint8_t, 4>> colors)
        : initial_data_(std::move(colors))
    {
    }

    RHIStatus ColorVertexBuffer::record_upload(
        RHIDevice& device,
        RHIGraphicsCommandContext& context)
    {
        bool deterministic_failure = false;
        const RHIStatus status = record_buffer_upload(
            device, context, initial_data_.data(),
            initial_data_.size() * sizeof(initial_data_[0]),
            RHIResourceUsage::VertexBuffer, RHIAccess::VertexBuffer,
            "StaticMesh.ColorVertexBuffer", rhi_buffer_,
            deterministic_failure);
        return !status && deterministic_failure ? fail(status) : status;
    }

    void ColorVertexBuffer::on_recording_committed() noexcept
    {
        std::vector<std::array<std::uint8_t, 4>>().swap(initial_data_);
    }

    void ColorVertexBuffer::on_recording_discarded() noexcept
    {
        rhi_buffer_.reset();
    }

    void ColorVertexBuffer::release_rhi() noexcept
    {
        rhi_buffer_.reset();
        std::vector<std::array<std::uint8_t, 4>>().swap(initial_data_);
    }

    StaticMeshIndexBuffer::StaticMeshIndexBuffer(
        const StaticMeshIndexData& indices)
    {
        // The fixed two-width variant is decoded explicitly so the RHI binding
        // format and copied byte payload always describe the same index width.
        if (const auto* indices_u16 =
            std::get_if<std::vector<std::uint16_t>>(&indices))
        {
            format_ = RHIIndexFormat::UInt16;
            initial_data_.resize(indices_u16->size() * sizeof(std::uint16_t));
            std::memcpy(
                initial_data_.data(), indices_u16->data(), initial_data_.size());
        }
        else if (const auto* indices_u32 =
            std::get_if<std::vector<std::uint32_t>>(&indices))
        {
            format_ = RHIIndexFormat::UInt32;
            initial_data_.resize(indices_u32->size() * sizeof(std::uint32_t));
            std::memcpy(
                initial_data_.data(), indices_u32->data(), initial_data_.size());
        }
    }

    RHIStatus StaticMeshIndexBuffer::record_upload(
        RHIDevice& device,
        RHIGraphicsCommandContext& context)
    {
        bool deterministic_failure = false;
        const RHIStatus status = record_buffer_upload(
            device, context, initial_data_.data(), initial_data_.size(),
            RHIResourceUsage::IndexBuffer, RHIAccess::IndexBuffer,
            "StaticMesh.StaticMeshIndexBuffer", rhi_buffer_,
            deterministic_failure);
        return !status && deterministic_failure ? fail(status) : status;
    }

    void StaticMeshIndexBuffer::on_recording_committed() noexcept
    {
        std::vector<std::uint8_t>().swap(initial_data_);
    }

    void StaticMeshIndexBuffer::on_recording_discarded() noexcept
    {
        rhi_buffer_.reset();
    }

    void StaticMeshIndexBuffer::release_rhi() noexcept
    {
        rhi_buffer_.reset();
        std::vector<std::uint8_t>().swap(initial_data_);
    }

    StaticMeshRenderData::StaticMeshRenderData(const StaticMesh& static_mesh)
        : position_vertex_buffer_(static_mesh.vertices())
        , static_mesh_vertex_buffer_(static_mesh.vertices())
        , color_vertex_buffer_(static_mesh.vertex_colors().empty()
            ? nullptr
            : std::make_unique<ColorVertexBuffer>(static_mesh.vertex_colors()))
        , index_buffer_(static_mesh.indices())
        , sections_(static_mesh.sections())
    {
    }

    RHIStatus StaticMeshRenderData::begin_init(
        RenderResourceManager& manager)
    {
        RHIStatus status = manager.begin_init(position_vertex_buffer_);
        if (!status)
        {
            return status;
        }
        status = manager.begin_init(static_mesh_vertex_buffer_);
        if (!status)
        {
            manager.release(position_vertex_buffer_);
            return status;
        }
        if (color_vertex_buffer_)
        {
            status = manager.begin_init(*color_vertex_buffer_);
            if (!status)
            {
                manager.release(static_mesh_vertex_buffer_);
                manager.release(position_vertex_buffer_);
                return status;
            }
        }
        status = manager.begin_init(index_buffer_);
        if (!status)
        {
            if (color_vertex_buffer_)
            {
                manager.release(*color_vertex_buffer_);
            }
            manager.release(static_mesh_vertex_buffer_);
            manager.release(position_vertex_buffer_);
        }
        return status;
    }

    RHIStatus StaticMeshRenderData::prepare_current_recording()
    {
        local_vertex_factory_.reset();
        if (!position_vertex_buffer_.buffer() ||
            !static_mesh_vertex_buffer_.buffer() ||
            !index_buffer_.buffer() ||
            (color_vertex_buffer_ && !color_vertex_buffer_->buffer()))
        {
            return RHIStatus::failure(
                RHIErrorCode::NotReady,
                "StaticMeshRenderData requires every candidate buffer in the current recording");
        }

        std::vector<VertexStreamComponent> components;
        components.push_back({ShaderVertexAttributeId::Position0,
            0u, 0u, position_vertex_buffer_.stride(),
            PixelFormat::R32G32B32Float, position_vertex_buffer_.buffer()});
        components.push_back({ShaderVertexAttributeId::Normal0,
            1u, 0u, static_mesh_vertex_buffer_.stride(),
            PixelFormat::R32G32B32Float, static_mesh_vertex_buffer_.buffer()});
        components.push_back({ShaderVertexAttributeId::TexCoord0,
            1u, 12u, static_mesh_vertex_buffer_.stride(),
            PixelFormat::R32G32Float, static_mesh_vertex_buffer_.buffer()});
        if (color_vertex_buffer_)
        {
            components.push_back({ShaderVertexAttributeId::Color0,
                2u, 0u, color_vertex_buffer_->stride(),
                PixelFormat::R8G8B8A8UNorm, color_vertex_buffer_->buffer()});
        }

        auto candidate = std::make_unique<LocalVertexFactory>(
            std::move(components));
        const RHIStatus validation = candidate->validate_streams();
        if (!validation)
        {
            return validation;
        }
        local_vertex_factory_ = std::move(candidate);
        return RHIStatus::success();
    }

    RHIStatus StaticMeshRenderData::release(RenderResourceManager& manager)
    {
        local_vertex_factory_.reset();
        RHIStatus status = manager.release(index_buffer_);
        if (color_vertex_buffer_)
        {
            status = preserve_first_failure(
                status, manager.release(*color_vertex_buffer_));
        }
        status = preserve_first_failure(
            status, manager.release(static_mesh_vertex_buffer_));
        status = preserve_first_failure(
            status, manager.release(position_vertex_buffer_));
        return status;
    }

    bool StaticMeshRenderData::is_drawable() const
    {
        return local_vertex_factory_ != nullptr &&
            position_vertex_buffer_.buffer() != nullptr &&
            static_mesh_vertex_buffer_.buffer() != nullptr &&
            index_buffer_.buffer() != nullptr &&
            (!color_vertex_buffer_ || color_vertex_buffer_->buffer() != nullptr);
    }

    RHIIndexBufferBinding StaticMeshRenderData::index_buffer_binding() const
    {
        RHIIndexBufferBinding binding;
        binding.buffer = index_buffer_.buffer();
        binding.format = index_buffer_.format();
        return binding;
    }
}
