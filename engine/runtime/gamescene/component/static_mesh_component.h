#pragma once

#include "gamescene/component/primitive_component.h"
#include "rendercore/geometry/static_mesh.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace toy3d
{
    class StaticMeshComponent final : public PrimitiveComponent
    {
      public:
        explicit StaticMeshComponent(Actor& owner) : PrimitiveComponent(owner)
        {
        }
        ~StaticMeshComponent() override = default;

        const StaticMeshRef& static_mesh() const
        {
            return static_mesh_;
        }
        void set_static_mesh(StaticMeshRef static_mesh);

        bool set_material_override(std::uint32_t material_slot, MaterialInterfaceRef material);
        bool clear_material_override(std::uint32_t material_slot);
        bool has_material_override(std::uint32_t material_slot) const;
        MaterialInterfaceRef material_for_slot(std::uint32_t material_slot) const;

      private:
        void send_material_overrides(std::vector<MaterialInterfaceRef> previous);
        void update_bounds() override;
        void on_render_state_removed() override;
        std::unique_ptr<PrimitiveSceneProxy> create_scene_proxy() const override;

        StaticMeshRef static_mesh_;
        std::vector<MaterialInterfaceRef> material_overrides_;
    };
} // namespace toy3d
