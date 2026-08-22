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
        update_bounds();
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

    void StaticMeshComponent::update_bounds()
    {
        if (static_mesh_ == nullptr)
        {
            world_bounds_ = {};
            return;
        }

        const AxisAlignedBounds& local_bounds = static_mesh_->local_bounds();
        const Vector3 local_center(
            (local_bounds.minimum.x + local_bounds.maximum.x) * 0.5f,
            (local_bounds.minimum.y + local_bounds.maximum.y) * 0.5f,
            (local_bounds.minimum.z + local_bounds.maximum.z) * 0.5f);
        const Vector3 local_extent(
            (local_bounds.maximum.x - local_bounds.minimum.x) * 0.5f,
            (local_bounds.maximum.y - local_bounds.minimum.y) * 0.5f,
            (local_bounds.maximum.z - local_bounds.minimum.z) * 0.5f);
        const Matrix4& transform = world_transform();
        const Vector3 world_center = transform_position(
            transform, local_center);
        const Vector3 world_extent{
            std::abs(transform.at(0, 0)) * local_extent.x +
                std::abs(transform.at(1, 0)) * local_extent.y +
                std::abs(transform.at(2, 0)) * local_extent.z,
            std::abs(transform.at(0, 1)) * local_extent.x +
                std::abs(transform.at(1, 1)) * local_extent.y +
                std::abs(transform.at(2, 1)) * local_extent.z,
            std::abs(transform.at(0, 2)) * local_extent.x +
                std::abs(transform.at(1, 2)) * local_extent.y +
                std::abs(transform.at(2, 2)) * local_extent.z};
        world_bounds_.minimum = vec3(
            world_center.x - world_extent.x,
            world_center.y - world_extent.y,
            world_center.z - world_extent.z);
        world_bounds_.maximum = vec3(
            world_center.x + world_extent.x,
            world_center.y + world_extent.y,
            world_center.z + world_extent.z);
    }
}
