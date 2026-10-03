#pragma once

#include "gamescene/component/primitive_component.h"
#include "rendercore/material/material.h"

namespace toy3d
{
    // Shared slot contract; concrete mesh components retain geometry and deformation ownership.
    class MeshComponent : public PrimitiveComponent
    {
      public:
        ~MeshComponent() override = default;
        virtual const std::vector<std::string>& material_slot_names() const = 0;
        virtual MaterialInterfaceRef default_material_for_slot(std::uint32_t slot) const = 0;
        virtual MaterialInterfaceRef material_for_slot(std::uint32_t slot) const = 0;
        virtual bool has_material_override(std::uint32_t slot) const = 0;
        virtual bool set_material_override(std::uint32_t slot, MaterialInterfaceRef material) = 0;
        virtual bool clear_material_override(std::uint32_t slot) = 0;
        virtual shader::VertexFactoryType vertex_factory_type() const = 0;

      protected:
        explicit MeshComponent(Actor& owner) : PrimitiveComponent(owner)
        {
        }
    };
} // namespace toy3d
