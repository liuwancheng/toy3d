#pragma once

#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/render_resource.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace toy3d
{
    class RenderResourceManager;

    class PositionVertexBuffer final : public RenderResource
    {
      public:
        explicit PositionVertexBuffer(const std::vector<StaticMeshVertex>& vertices);

        const RHIBufferRef& buffer() const
        {
            return rhi_buffer_;
        }
        std::uint32_t stride() const
        {
            return 16u;
        }

      private:
        RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context) override;
        void on_recording_committed() noexcept override;
        void on_recording_discarded() noexcept override;
        void release_rhi() noexcept override;
        std::vector<float> initial_data_;
        RHIBufferRef rhi_buffer_;
    };

    class StaticMeshVertexBuffer final : public RenderResource
    {
      public:
        explicit StaticMeshVertexBuffer(const std::vector<StaticMeshVertex>& vertices);

        const RHIBufferRef& buffer() const
        {
            return rhi_buffer_;
        }
        std::uint32_t stride() const
        {
            return 40u;
        }

      private:
        RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context) override;
        void on_recording_committed() noexcept override;
        void on_recording_discarded() noexcept override;
        void release_rhi() noexcept override;
        std::vector<float> initial_data_;
        RHIBufferRef rhi_buffer_;
    };

    class ColorVertexBuffer final : public RenderResource
    {
      public:
        explicit ColorVertexBuffer(std::vector<std::array<std::uint8_t, 4>> colors);

        const RHIBufferRef& buffer() const
        {
            return rhi_buffer_;
        }
        std::uint32_t stride() const
        {
            return 4u;
        }

      private:
        RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context) override;
        void on_recording_committed() noexcept override;
        void on_recording_discarded() noexcept override;
        void release_rhi() noexcept override;
        std::vector<std::array<std::uint8_t, 4>> initial_data_;
        RHIBufferRef rhi_buffer_;
    };

    class StaticMeshIndexBuffer final : public RenderResource
    {
      public:
        explicit StaticMeshIndexBuffer(const StaticMeshIndexData& indices);

        const RHIBufferRef& buffer() const
        {
            return rhi_buffer_;
        }
        RHIIndexFormat format() const
        {
            return format_;
        }

      private:
        RHIStatus record_upload(RHIDevice& device, RHIGraphicsCommandContext& context) override;
        void on_recording_committed() noexcept override;
        void on_recording_discarded() noexcept override;
        void release_rhi() noexcept override;
        std::vector<std::uint8_t> initial_data_;
        RHIIndexFormat format_ = RHIIndexFormat::UInt16;
        RHIBufferRef rhi_buffer_;
    };

    // Stable Render-side candidate copied from one immutable StaticMesh geometry.
    // It owns all buffer resources and never reads the source Asset after creation.
    class StaticMeshRenderData final : public std::enable_shared_from_this<StaticMeshRenderData>
    {
      public:
        explicit StaticMeshRenderData(const StaticMesh& static_mesh);
        ~StaticMeshRenderData();

        StaticMeshRenderData(const StaticMeshRenderData&) = delete;
        StaticMeshRenderData& operator=(const StaticMeshRenderData&) = delete;
        StaticMeshRenderData(StaticMeshRenderData&&) = delete;
        StaticMeshRenderData& operator=(StaticMeshRenderData&&) = delete;

        RHIStatus prepare_current_recording();
        RenderResourceState state() const;
        RHIStatus failure_status() const;
        std::size_t ref_count() const noexcept
        {
            return ref_count_;
        }

        bool is_drawable() const;
        bool has_valid_tangent_frame() const
        {
            return valid_tangent_frame_;
        }
        const LocalVertexFactory* vertex_factory() const
        {
            return local_vertex_factory_.get();
        }
        RHIIndexBufferBinding index_buffer_binding() const;
        const std::vector<StaticMeshSection>& sections() const
        {
            return sections_;
        }
        std::size_t index_count() const
        {
            return index_count_;
        }

      private:
        friend class RenderResourceManager;
        template <typename T> friend class RenderResourceRef;

        std::array<RenderResource*, 4> resources();
        std::array<const RenderResource*, 4> resources() const;
        RHIStatus validate_geometry() const;
        void reset_vertex_factory() noexcept;
        void retain() noexcept;
        void release() noexcept;

        RenderResourceManager* owner_manager_ = nullptr;
        std::thread::id ref_thread_{};
        std::size_t ref_count_ = 0;
        bool reclaim_requested_ = false;
        PositionVertexBuffer position_vertex_buffer_;
        StaticMeshVertexBuffer static_mesh_vertex_buffer_;
        std::unique_ptr<ColorVertexBuffer> color_vertex_buffer_;
        StaticMeshIndexBuffer index_buffer_;
        std::unique_ptr<LocalVertexFactory> local_vertex_factory_;
        std::vector<StaticMeshSection> sections_;
        std::size_t index_count_ = 0;
        bool valid_tangent_frame_ = false;
    };
} // namespace toy3d
