#pragma once

#include "rendercore/geometry/axis_aligned_bounds.h"
#include "rendercore/material/material.h"
#include "rendercore/render_id.h"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace toy3d
{
    struct StaticMeshVertex
    {
        vec3 position{0.0f};
        vec3 normal{0.0f, 1.0f, 0.0f};
        vec2 uv0{0.0f};
    };

    struct StaticMeshSection
    {
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::uint32_t material_slot = 0;
    };

    // A mesh owns exactly one index width. variant keeps that choice explicit
    // without maintaining two arrays or exposing an untyped byte buffer.
    using StaticMeshIndexData = std::variant<
        std::vector<std::uint16_t>,
        std::vector<std::uint32_t>>;

    struct StaticMeshDesc
    {
        std::vector<StaticMeshVertex> vertices;
        StaticMeshIndexData indices;
        std::vector<StaticMeshSection> sections;
        std::vector<MaterialInstanceRef> material_slots;
    };

    class StaticMesh
    {
    public:
        static std::shared_ptr<const StaticMesh> create(StaticMeshDesc desc);
        ~StaticMesh() = default;

        StaticMesh(const StaticMesh&) = delete;
        StaticMesh& operator=(const StaticMesh&) = delete;
        StaticMesh(StaticMesh&& other) noexcept;
        StaticMesh& operator=(StaticMesh&&) noexcept = delete;

        const std::vector<StaticMeshVertex>& vertices() const { return vertices_; }
        const StaticMeshIndexData& indices() const { return indices_; }
        const std::vector<StaticMeshSection>& sections() const { return sections_; }
        const std::vector<MaterialInstanceRef>& material_slots() const { return material_slots_; }
        const AxisAlignedBounds& local_bounds() const { return local_bounds_; }
        MeshRenderResourceId render_resource_id() const { return render_resource_id_; }

    private:
        StaticMesh(StaticMeshDesc desc, AxisAlignedBounds local_bounds);

        std::vector<StaticMeshVertex> vertices_;
        StaticMeshIndexData indices_;
        std::vector<StaticMeshSection> sections_;
        std::vector<MaterialInstanceRef> material_slots_;
        AxisAlignedBounds local_bounds_;
        MeshRenderResourceId render_resource_id_;
    };

    using StaticMeshRef = std::shared_ptr<const StaticMesh>;
}
