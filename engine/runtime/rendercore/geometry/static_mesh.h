#pragma once

#include "rendercore/geometry/axis_aligned_bounds.h"
#include "rendercore/material/material.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace toy3d
{
    class StaticMeshRenderData;

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
    using StaticMeshIndexData = std::variant<std::vector<std::uint16_t>, std::vector<std::uint32_t>>;

    struct StaticMeshDesc
    {
        std::vector<StaticMeshVertex> vertices;
        std::vector<std::array<std::uint8_t, 4>> vertex_colors;
        StaticMeshIndexData indices;
        std::vector<StaticMeshSection> sections;
        std::vector<MaterialInterfaceRef> material_slots;
        // Stable imported slot names survive geometry reconstruction and reordering.
        std::vector<std::string> material_slot_names;
    };

    class StaticMesh
    {
      public:
        static std::shared_ptr<const StaticMesh> create(StaticMeshDesc desc);
        ~StaticMesh();

        StaticMesh(const StaticMesh&) = delete;
        StaticMesh& operator=(const StaticMesh&) = delete;
        StaticMesh(StaticMesh&& other) noexcept;
        StaticMesh& operator=(StaticMesh&&) noexcept = delete;

        const std::vector<StaticMeshVertex>& vertices() const
        {
            return vertices_;
        }
        const std::vector<std::array<std::uint8_t, 4>>& vertex_colors() const
        {
            return vertex_colors_;
        }
        const StaticMeshIndexData& indices() const
        {
            return indices_;
        }
        const std::vector<StaticMeshSection>& sections() const
        {
            return sections_;
        }
        const std::vector<MaterialInterfaceRef>& material_slots() const
        {
            return material_slots_;
        }
        const std::vector<std::string>& material_slot_names() const
        {
            return material_slot_names_;
        }
        const AxisAlignedBounds& local_bounds() const
        {
            return local_bounds_;
        }
        StaticMeshRenderData* render_data() const noexcept
        {
            return render_data_.get();
        }

      private:
        StaticMesh(StaticMeshDesc desc, AxisAlignedBounds local_bounds);

        std::vector<StaticMeshVertex> vertices_;
        std::vector<std::array<std::uint8_t, 4>> vertex_colors_;
        StaticMeshIndexData indices_;
        std::vector<StaticMeshSection> sections_;
        std::vector<MaterialInterfaceRef> material_slots_;
        std::vector<std::string> material_slot_names_;
        AxisAlignedBounds local_bounds_;
        std::unique_ptr<StaticMeshRenderData> render_data_;
    };

    using StaticMeshRef = std::shared_ptr<const StaticMesh>;
} // namespace toy3d
