#pragma once

#include "gamescene/component/mesh_component.h"
#include "rendercore/geometry/static_mesh.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace toy3d
{
    class StaticMeshComponent final : public MeshComponent
    {
      public:
        explicit StaticMeshComponent(Actor& owner) : MeshComponent(owner)
        {
        }
        ~StaticMeshComponent() override = default;

        const StaticMeshRef& static_mesh() const
        {
            return static_mesh_;
        }
        void set_static_mesh(StaticMeshRef static_mesh);

        const std::vector<std::string>& material_slot_names() const override;
        MaterialInterfaceRef default_material_for_slot(std::uint32_t slot) const override;
        shader::VertexFactoryType vertex_factory_type() const override;
        bool set_material_override(std::uint32_t material_slot, MaterialInterfaceRef material) override;
        bool clear_material_override(std::uint32_t material_slot) override;
        bool has_material_override(std::uint32_t material_slot) const override;
        MaterialInterfaceRef material_for_slot(std::uint32_t material_slot) const override;

      private:
        bool supports_shadow_casting() const override;
        void send_material_overrides(std::vector<MaterialInterfaceRef> previous);
        void update_bounds() override;
        void on_render_state_removed() override;
        std::unique_ptr<PrimitiveSceneProxy> create_scene_proxy() const override;

        StaticMeshRef static_mesh_;
        std::vector<MaterialInterfaceRef> material_overrides_;
    };
} // namespace toy3d
