#include "gamescene/component/static_mesh_component.h"

#include "logging/logger.h"

#include <cmath>
#include <utility>

namespace toy3d
{
    void StaticMeshComponent::set_static_mesh(StaticMeshRef static_mesh)
    {
        static_mesh_ = std::move(static_mesh);
        material_overrides_.clear();
        if (static_mesh_ != nullptr)
        {
            material_overrides_.resize(static_mesh_->material_slots().size());
        }
        mark_render_dirty(RenderDirtyFlags::State);
    }

    bool StaticMeshComponent::set_material_override(
        std::uint32_t material_slot,
        MaterialInstanceRef material)
    {
        if (static_mesh_ == nullptr || material_slot >= material_overrides_.size())
        {
            TOY_LOG_ERROR("A StaticMesh Material override requires an existing Material slot.");
            return false;
        }
        if (material == nullptr)
        {
            TOY_LOG_ERROR("A StaticMesh Material override must reference a MaterialInstance.");
            return false;
        }
        material_overrides_[material_slot] = std::move(material);
        mark_render_dirty(RenderDirtyFlags::State);
        return true;
    }

    MaterialInstanceRef StaticMeshComponent::material_for_slot(
        std::uint32_t material_slot) const
    {
        if (static_mesh_ == nullptr || material_slot >= material_overrides_.size())
        {
            return nullptr;
        }
        const MaterialInstanceRef& material_override = material_overrides_[material_slot];
        return material_override != nullptr
            ? material_override
            : static_mesh_->material_slots()[material_slot];
    }

    void StaticMeshComponent::update_world_bounds()
    {
        if (static_mesh_ == nullptr)
        {
            world_bounds_ = {};
            return;
        }

        const AxisAlignedBounds& local_bounds = static_mesh_->local_bounds();
        const vec3 local_center = (local_bounds.minimum + local_bounds.maximum) * 0.5f;
        const vec3 local_extent = (local_bounds.maximum - local_bounds.minimum) * 0.5f;
        const vec3 world_center = vec3(world_transform() * vec4(local_center, 1.0f));
        const mat4x4& transform = world_transform();
        const vec3 world_extent{
            std::abs(transform[0].x) * local_extent.x +
                std::abs(transform[1].x) * local_extent.y +
                std::abs(transform[2].x) * local_extent.z,
            std::abs(transform[0].y) * local_extent.x +
                std::abs(transform[1].y) * local_extent.y +
                std::abs(transform[2].y) * local_extent.z,
            std::abs(transform[0].z) * local_extent.x +
                std::abs(transform[1].z) * local_extent.y +
                std::abs(transform[2].z) * local_extent.z};
        world_bounds_.minimum = world_center - world_extent;
        world_bounds_.maximum = world_center + world_extent;
    }
}
