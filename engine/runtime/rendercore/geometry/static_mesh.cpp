#include "rendercore/geometry/static_mesh.h"

#include "logging/logger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool is_finite(const vec2& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool is_finite(const vec3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        std::size_t index_count(const StaticMeshIndexData& indices)
        {
            // The variant has only two fixed alternatives, so explicit get_if
            // branches make the selected index width visible to the reader.
            if (const auto* indices_u16 =
                std::get_if<std::vector<std::uint16_t>>(&indices))
            {
                return indices_u16->size();
            }
            if (const auto* indices_u32 =
                std::get_if<std::vector<std::uint32_t>>(&indices))
            {
                return indices_u32->size();
            }
            return 0;
        }

        bool indices_reference_existing_vertices(
            const StaticMeshIndexData& indices,
            std::size_t vertex_count)
        {
            // Validate the active index representation directly; get_if keeps
            // both supported widths and their identical bounds rule explicit.
            if (const auto* indices_u16 =
                std::get_if<std::vector<std::uint16_t>>(&indices))
            {
                for (std::uint16_t index : *indices_u16)
                {
                    if (static_cast<std::size_t>(index) >= vertex_count)
                    {
                        return false;
                    }
                }
                return true;
            }
            if (const auto* indices_u32 =
                std::get_if<std::vector<std::uint32_t>>(&indices))
            {
                for (std::uint32_t index : *indices_u32)
                {
                    if (static_cast<std::size_t>(index) >= vertex_count)
                    {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }
    }

    std::shared_ptr<const StaticMesh> StaticMesh::create(StaticMeshDesc desc)
    {
        if (desc.vertices.empty() || index_count(desc.indices) == 0)
        {
            TOY_LOG_ERROR("A StaticMesh requires vertex and index data.");
            return nullptr;
        }
        if (desc.sections.empty() || desc.material_slots.empty())
        {
            TOY_LOG_ERROR("A StaticMesh requires at least one Section and Material slot.");
            return nullptr;
        }
        if (std::any_of(
            desc.material_slots.begin(), desc.material_slots.end(),
            [](const MaterialInstanceRef& material) { return material == nullptr; }))
        {
            TOY_LOG_ERROR("Every StaticMesh Material slot must reference a MaterialInstance.");
            return nullptr;
        }
        if (!indices_reference_existing_vertices(desc.indices, desc.vertices.size()))
        {
            TOY_LOG_ERROR("StaticMesh index data references a missing vertex.");
            return nullptr;
        }

        const std::size_t mesh_index_count = index_count(desc.indices);
        for (const StaticMeshSection& section : desc.sections)
        {
            const std::size_t section_end =
                static_cast<std::size_t>(section.first_index) + section.index_count;
            if (section.index_count == 0 ||
                section.index_count % 3 != 0 ||
                section_end > mesh_index_count ||
                section.material_slot >= desc.material_slots.size())
            {
                TOY_LOG_ERROR("StaticMesh Section metadata is outside the mesh data or Material slots.");
                return nullptr;
            }
        }

        AxisAlignedBounds bounds;
        bounds.minimum = vec3(std::numeric_limits<float>::max());
        bounds.maximum = vec3(std::numeric_limits<float>::lowest());
        for (const StaticMeshVertex& vertex : desc.vertices)
        {
            if (!is_finite(vertex.position) ||
                !is_finite(vertex.normal) ||
                !is_finite(vertex.uv0))
            {
                TOY_LOG_ERROR("StaticMesh vertex data must contain finite values.");
                return nullptr;
            }
            bounds.minimum = glm::min(bounds.minimum, vertex.position);
            bounds.maximum = glm::max(bounds.maximum, vertex.position);
        }

        StaticMesh static_mesh(std::move(desc), bounds);
        return std::make_shared<StaticMesh>(std::move(static_mesh));
    }

    StaticMesh::StaticMesh(
        StaticMeshDesc desc,
        AxisAlignedBounds local_bounds)
        : vertices_(std::move(desc.vertices)),
          indices_(std::move(desc.indices)),
          sections_(std::move(desc.sections)),
          material_slots_(std::move(desc.material_slots)),
          local_bounds_(local_bounds),
          render_resource_id_(allocate_render_id<MeshRenderResourceId>())
    {
    }

    StaticMesh::StaticMesh(StaticMesh&& other) noexcept
        : vertices_(std::move(other.vertices_)),
          indices_(std::move(other.indices_)),
          sections_(std::move(other.sections_)),
          material_slots_(std::move(other.material_slots_)),
          local_bounds_(other.local_bounds_),
          render_resource_id_(other.render_resource_id_)
    {
        // The immutable asset's render identity transfers with its payload;
        // the short-lived factory source must not retain a duplicate ID.
        other.render_resource_id_ = {};
    }
}
