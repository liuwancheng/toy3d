#pragma once

#include "gamescene/component/scene_component.h"
#include "rendercore/geometry/static_mesh.h"

#include <cstdint>
#include <vector>

namespace toy3d
{
    class StaticMeshComponent final : public SceneComponent
    {
    public:
        explicit StaticMeshComponent(Actor& owner) : SceneComponent(owner) {}
        ~StaticMeshComponent() override = default;

        const StaticMeshRef& static_mesh() const { return static_mesh_; }
        void set_static_mesh(StaticMeshRef static_mesh);

        bool set_material_override(
            std::uint32_t material_slot,
            MaterialInstanceRef material);
        MaterialInstanceRef material_for_slot(std::uint32_t material_slot) const;

        PrimitiveId primitive_id() const { return primitive_id_; }
        const AxisAlignedBounds& world_bounds() const { return world_bounds_; }

    private:
        friend class World;

        void update_world_bounds();

        StaticMeshRef static_mesh_;
        std::vector<MaterialInstanceRef> material_overrides_;
        PrimitiveId primitive_id_;
        AxisAlignedBounds world_bounds_;
    };
}
