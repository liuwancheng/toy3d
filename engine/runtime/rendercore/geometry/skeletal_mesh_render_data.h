#pragma once

#include "asset/mesh/skeletal_mesh_asset.h"
#include "rendercore/geometry/gpu_skin_vertex_factory.h"
#include "rendercore/geometry/skin_weight_vertex_buffer.h"
#include "rendercore/geometry/static_mesh_render_data.h"

namespace toy3d
{
    // Immutable geometry candidate. Pose buffers belong to the instance proxy,
    // so multiple instances of this geometry retain independent animation state.
    class SkeletalMeshRenderData final
    {
      public:
        explicit SkeletalMeshRenderData(const SkeletalMeshAssetGeometry& geometry);
        ~SkeletalMeshRenderData() = default;
        SkeletalMeshRenderData(const SkeletalMeshRenderData&) = delete;
        SkeletalMeshRenderData& operator=(const SkeletalMeshRenderData&) = delete;

        RHIStatus begin_init(RenderResourceManager& manager);
        RHIStatus prepare_current_recording();
        RHIStatus release(RenderResourceManager& manager);
        bool is_drawable() const;
        const GPUSkinVertexFactory* vertex_factory() const
        {
            return vertex_factory_.get();
        }
        RHIIndexBufferBinding index_buffer_binding() const;
        const std::vector<StaticMeshAssetSection>& sections() const
        {
            return sections_;
        }
        const std::vector<std::vector<std::uint32_t>>& section_bone_maps() const
        {
            return bone_maps_;
        }
        std::uint32_t num_bone_influences() const
        {
            return num_bone_influences_;
        }
        std::size_t index_count() const
        {
            return index_count_;
        }

      private:
        std::array<RenderResource*, 5> resources();
        PositionVertexBuffer position_buffer_;
        StaticMeshVertexBuffer attributes_buffer_;
        ColorVertexBuffer color_buffer_;
        SkinWeightVertexBuffer skin_weights_buffer_;
        StaticMeshIndexBuffer index_buffer_;
        std::unique_ptr<GPUSkinVertexFactory> vertex_factory_;
        std::vector<StaticMeshAssetSection> sections_;
        std::vector<std::vector<std::uint32_t>> bone_maps_;
        std::uint32_t num_bone_influences_ = 0;
        std::size_t index_count_ = 0;
        bool valid_ = false;
        bool init_started_ = false;
    };
} // namespace toy3d
