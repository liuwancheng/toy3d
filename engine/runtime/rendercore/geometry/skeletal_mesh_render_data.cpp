#include "rendercore/geometry/skeletal_mesh_render_data.h"

#include <cassert>
#include <utility>

#include "rendercore/render_resource_manager.h"

namespace toy3d
{
    namespace
    {
        std::vector<StaticMeshVertex> mesh_vertices(const StaticMeshAssetGeometry& geometry)
        {
            std::vector<StaticMeshVertex> vertices;
            vertices.reserve(geometry.vertices.size());
            for (const auto& source : geometry.vertices)
            {
                StaticMeshVertex vertex;
                vertex.position = vec3(source.position.x, source.position.y, source.position.z);
                vertex.normal = vec3(source.normal.x, source.normal.y, source.normal.z);
                vertex.uv0 = vec2(source.uv0.x, source.uv0.y);
                vertex.tangent = vec4(source.tangent.x, source.tangent.y, source.tangent.z, source.tangent.w);
                vertices.push_back(vertex);
            }
            return vertices;
        }

        std::vector<std::array<std::uint8_t, 4>> mesh_colors(const StaticMeshAssetGeometry& geometry)
        {
            std::vector<std::array<std::uint8_t, 4>> colors;
            colors.reserve(geometry.vertices.size());
            for (const auto& vertex : geometry.vertices)
            {
                colors.push_back(vertex.color);
            }
            return colors;
        }
    } // namespace

    SkeletalMeshRenderData::SkeletalMeshRenderData(const SkeletalMeshAssetGeometry& geometry)
        : position_buffer_(mesh_vertices(geometry.mesh)), attributes_buffer_(mesh_vertices(geometry.mesh)),
          color_buffer_(mesh_colors(geometry.mesh)),
          skin_weights_buffer_(geometry.skin_weights, geometry.num_bone_influences),
          // C++17 variant reuses the explicit index-width ABI without assigning a StaticMesh asset identity.
          index_buffer_(StaticMeshIndexData(geometry.mesh.indices)), sections_(geometry.mesh.sections),
          bone_maps_(geometry.section_bone_maps), num_bone_influences_(geometry.num_bone_influences),
          index_count_(geometry.mesh.indices.size()), valid_(validate_skeletal_mesh_geometry(geometry).succeeded())
    {
        valid_tangent_frame_ = geometry.mesh.valid_tangent_frame;
    }

    SkeletalMeshRenderData::~SkeletalMeshRenderData()
    {
        assert(ref_count_ == 0);
        if (owner_manager_)
        {
            const RHIStatus status = owner_manager_->release(*this);
            assert(status.succeeded());
        }
    }

    std::array<RenderResource*, 5> SkeletalMeshRenderData::resources()
    {
        return {&position_buffer_, &attributes_buffer_, &color_buffer_, &skin_weights_buffer_, &index_buffer_};
    }

    std::array<const RenderResource*, 5> SkeletalMeshRenderData::resources() const
    {
        return {&position_buffer_, &attributes_buffer_, &color_buffer_, &skin_weights_buffer_, &index_buffer_};
    }

    RHIStatus SkeletalMeshRenderData::validate_geometry() const
    {
        return valid_ ? RHIStatus::success()
                      : RHIStatus::failure(RHIErrorCode::InvalidArgument, "Invalid skeletal mesh geometry.");
    }

    void SkeletalMeshRenderData::reset_vertex_factory() noexcept
    {
        vertex_factory_.reset();
    }

    RenderResourceState SkeletalMeshRenderData::state() const
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

    RHIStatus SkeletalMeshRenderData::failure_status() const
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

    void SkeletalMeshRenderData::retain() noexcept
    {
        assert(ref_thread_ == std::this_thread::get_id());
        ++ref_count_;
        reclaim_requested_ = false;
    }

    void SkeletalMeshRenderData::release() noexcept
    {
        assert(ref_thread_ == std::this_thread::get_id());
        assert(ref_count_ > 0);
        if (--ref_count_ == 0)
        {
            reclaim_requested_ = true;
        }
    }

    RHIStatus SkeletalMeshRenderData::prepare_current_recording()
    {
        if (is_drawable() && vertex_factory_->validate_streams())
        {
            return RHIStatus::success();
        }
        vertex_factory_.reset();
        if (!position_buffer_.buffer() || !attributes_buffer_.buffer() || !color_buffer_.buffer() ||
            !skin_weights_buffer_.buffer() || !index_buffer_.buffer())
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Skeletal mesh buffers are not recorded.");
        }
        std::vector<VertexStreamComponent> components{
            {ShaderVertexAttributeId::Position0, 0, 0, position_buffer_.stride(), PixelFormat::R32G32B32A32Float,
             position_buffer_.buffer()},
            {ShaderVertexAttributeId::Normal0, 1, 0, attributes_buffer_.stride(), PixelFormat::R32G32B32A32Float,
             attributes_buffer_.buffer()},
            {ShaderVertexAttributeId::TexCoord0, 1, 16, attributes_buffer_.stride(), PixelFormat::R32G32Float,
             attributes_buffer_.buffer()},
            {ShaderVertexAttributeId::Tangent0, 1, 24, attributes_buffer_.stride(), PixelFormat::R32G32B32A32Float,
             attributes_buffer_.buffer()},
            {ShaderVertexAttributeId::Color0, 2, 0, color_buffer_.stride(), PixelFormat::R8G8B8A8UNorm,
             color_buffer_.buffer()},
            {ShaderVertexAttributeId::BlendIndices0, 3, 0, skin_weights_buffer_.stride(), PixelFormat::R8G8B8A8UInt,
             skin_weights_buffer_.buffer()},
            {ShaderVertexAttributeId::BlendWeights0, 3, num_bone_influences_, skin_weights_buffer_.stride(),
             PixelFormat::R8G8B8A8UNorm, skin_weights_buffer_.buffer()}};
        if (num_bone_influences_ == max_skin_influences)
        {
            components.push_back({ShaderVertexAttributeId::BlendIndices1, 3, 4, skin_weights_buffer_.stride(),
                                  PixelFormat::R8G8B8A8UInt, skin_weights_buffer_.buffer()});
            components.push_back({ShaderVertexAttributeId::BlendWeights1, 3, 12, skin_weights_buffer_.stride(),
                                  PixelFormat::R8G8B8A8UNorm, skin_weights_buffer_.buffer()});
        }
        auto candidate = std::make_unique<GPUSkinVertexFactory>(std::move(components), num_bone_influences_);
        const auto status = candidate->validate_streams();
        if (!status)
        {
            return status;
        }
        vertex_factory_ = std::move(candidate);
        return RHIStatus::success();
    }

    bool SkeletalMeshRenderData::is_drawable() const
    {
        return vertex_factory_ && position_buffer_.buffer() && attributes_buffer_.buffer() && color_buffer_.buffer() &&
               skin_weights_buffer_.buffer() && index_buffer_.buffer();
    }

    RHIIndexBufferBinding SkeletalMeshRenderData::index_buffer_binding() const
    {
        return {index_buffer_.buffer(), 0, index_buffer_.format()};
    }
} // namespace toy3d
