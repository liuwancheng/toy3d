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

    private:
        StaticMeshRef static_mesh_;
        std::vector<MaterialInstanceRef> material_overrides_;
    };
}
