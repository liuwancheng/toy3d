#include "gamescene/component/static_mesh_component.h"

#include "logging/logger.h"

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
}
