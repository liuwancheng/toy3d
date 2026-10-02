#include "gamescene/component/static_mesh_component.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "logging/logger.h"
#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "rendercore/render_command.h"

namespace toy3d
{
    bool StaticMeshComponent::supports_shadow_casting() const
    {
        if (!static_mesh_)
        {
            return true;
        }
        for (std::uint32_t slot = 0; slot < static_mesh_->material_slots().size(); ++slot)
        {
            std::string error;
            if (!validate_material_mesh_pass(material_for_slot(slot)->desc(), shader::ShaderPassRole::ShadowDepth,
                                             shader::VertexFactoryType::Local, error))
            {
                TOY_LOG_ERROR("Material slot {} rejected: {}", slot, error);
                return false;
            }
        }
        return true;
    }

    void StaticMeshComponent::set_static_mesh(StaticMeshRef static_mesh)
    {
        if (static_mesh_ == static_mesh)
        {
            return;
        }
        if (static_mesh && cast_shadows())
        {
            for (const auto& material : static_mesh->material_slots())
            {
                std::string error;
                if (!validate_material_mesh_pass(material->desc(), shader::ShaderPassRole::ShadowDepth,
                                                 shader::VertexFactoryType::Local, error))
                {
                    TOY_LOG_ERROR("StaticMesh replacement rejected: {}", error);
                    return;
                }
            }
        }
        const bool rebuild_render_state = has_render_state();
        if (rebuild_render_state)
        {
            destroy_render_state();
        }

        static_mesh_ = std::move(static_mesh);
        world().mark_content_changed();
        material_overrides_.clear();
        if (static_mesh_ != nullptr)
        {
            material_overrides_.resize(static_mesh_->material_slots().size());
        }
        update_bounds();
        create_render_state();
    }

    bool StaticMeshComponent::set_material_override(std::uint32_t material_slot, MaterialInterfaceRef material)
    {
        if (static_mesh_ == nullptr || material_slot >= material_overrides_.size())
        {
            TOY_LOG_ERROR("A StaticMesh Material override requires an existing Material slot.");
            return false;
        }
        if (material == nullptr)
        {
            TOY_LOG_ERROR("A StaticMesh Material override must reference a MaterialInterface.");
            return false;
        }

        std::string error;
        if (!validate_material_geometry(material->desc(), shader::VertexFactoryType::Local,
                                        !static_mesh_->vertex_colors().empty(), error))
        {
            TOY_LOG_ERROR("Material slot {} rejected: {}", material_slot, error);
            return false;
        }
        if (cast_shadows() && !validate_material_mesh_pass(material->desc(), shader::ShaderPassRole::ShadowDepth,
                                                           shader::VertexFactoryType::Local, error))
        {
            TOY_LOG_ERROR("Material slot {} rejected: {}", material_slot, error);
            return false;
        }
        world().mark_content_changed();
        auto previous = material_overrides_;
        material_overrides_[material_slot] = std::move(material);
        send_material_overrides(std::move(previous));
        return true;
    }

    bool StaticMeshComponent::clear_material_override(std::uint32_t material_slot)
    {
        if (!static_mesh_ || material_slot >= material_overrides_.size())
        {
            TOY_LOG_ERROR("Clearing a StaticMesh Material override requires an existing slot.");
            return false;
        }
        if (!material_overrides_[material_slot])
        {
            return true;
        }
        std::string error;
        if (!validate_material_geometry(static_mesh_->material_slots()[material_slot]->desc(),
                                        shader::VertexFactoryType::Local, !static_mesh_->vertex_colors().empty(),
                                        error))
        {
            TOY_LOG_ERROR("Material slot {} rejected: {}", material_slot, error);
            return false;
        }
        if (cast_shadows() &&
            !validate_material_mesh_pass(static_mesh_->material_slots()[material_slot]->desc(),
                                         shader::ShaderPassRole::ShadowDepth, shader::VertexFactoryType::Local, error))
        {
            TOY_LOG_ERROR("Material slot {} rejected: {}", material_slot, error);
            return false;
        }
        world().mark_content_changed();
        auto previous = material_overrides_;
        material_overrides_[material_slot].reset();
        send_material_overrides(std::move(previous));
        return true;
    }

    void StaticMeshComponent::send_material_overrides(std::vector<MaterialInterfaceRef> previous)
    {
        if (!has_render_state())
        {
            create_render_state();
            return;
        }
        std::vector<MaterialRenderProxy*> proxies;
        proxies.reserve(material_overrides_.size());
        for (std::uint32_t slot = 0u; slot < material_overrides_.size(); ++slot)
        {
            proxies.push_back(material_for_slot(slot)->material_render_proxy());
        }
        send_render_materials(std::move(proxies));
        // The preceding update stops borrowing old material proxies before its
        // owned GT references are dropped. Geometry and HitProxy identity stay.
        enqueue_render_command("ReleaseUpdatedStaticMeshMaterials",
                               [previous = std::move(previous)]() noexcept
                               {
                               });
    }

    bool StaticMeshComponent::has_material_override(std::uint32_t material_slot) const
    {
        return material_slot < material_overrides_.size() && material_overrides_[material_slot] != nullptr;
    }

    MaterialInterfaceRef StaticMeshComponent::material_for_slot(std::uint32_t material_slot) const
    {
        if (static_mesh_ == nullptr || material_slot >= material_overrides_.size())
        {
            return nullptr;
        }
        const MaterialInterfaceRef& material_override = material_overrides_[material_slot];
        return material_override != nullptr ? material_override : static_mesh_->material_slots()[material_slot];
    }

    void StaticMeshComponent::on_render_state_removed()
    {
        // Remove only borrows render_data. Keep its owner and override owners alive
        // until that earlier FIFO command has released every Render-side reference.
        enqueue_render_command("ReleaseRemovedStaticMeshReferences",
                               [mesh = static_mesh_, materials = material_overrides_]() noexcept
                               {
                               });
    }

    void StaticMeshComponent::update_bounds()
    {
        if (static_mesh_ == nullptr)
        {
            world_bounds_ = {};
            return;
        }

        const AxisAlignedBounds& local_bounds = static_mesh_->local_bounds();
        const Vector3 local_center((local_bounds.minimum.x + local_bounds.maximum.x) * 0.5f,
                                   (local_bounds.minimum.y + local_bounds.maximum.y) * 0.5f,
                                   (local_bounds.minimum.z + local_bounds.maximum.z) * 0.5f);
        const Vector3 local_extent((local_bounds.maximum.x - local_bounds.minimum.x) * 0.5f,
                                   (local_bounds.maximum.y - local_bounds.minimum.y) * 0.5f,
                                   (local_bounds.maximum.z - local_bounds.minimum.z) * 0.5f);
        const Matrix4& transform = world_transform();
        const Vector3 world_center = transform_position(transform, local_center);
        const Vector3 world_extent{
            std::abs(transform.at(0, 0)) * local_extent.x + std::abs(transform.at(1, 0)) * local_extent.y +
                std::abs(transform.at(2, 0)) * local_extent.z,
            std::abs(transform.at(0, 1)) * local_extent.x + std::abs(transform.at(1, 1)) * local_extent.y +
                std::abs(transform.at(2, 1)) * local_extent.z,
            std::abs(transform.at(0, 2)) * local_extent.x + std::abs(transform.at(1, 2)) * local_extent.y +
                std::abs(transform.at(2, 2)) * local_extent.z};
        world_bounds_.minimum =
            vec3(world_center.x - world_extent.x, world_center.y - world_extent.y, world_center.z - world_extent.z);
        world_bounds_.maximum =
            vec3(world_center.x + world_extent.x, world_center.y + world_extent.y, world_center.z + world_extent.z);
    }

    std::unique_ptr<PrimitiveSceneProxy> StaticMeshComponent::create_scene_proxy() const
    {
        if (static_mesh_ == nullptr)
        {
            return nullptr;
        }

        std::vector<MaterialRenderProxy*> material_render_proxies;
        material_render_proxies.reserve(static_mesh_->material_slots().size());
        for (std::uint32_t slot = 0u; slot < static_mesh_->material_slots().size(); ++slot)
        {
            const MaterialInterfaceRef material = material_for_slot(slot);
            material_render_proxies.push_back(material != nullptr ? material->material_render_proxy() : nullptr);
        }
        return std::make_unique<StaticMeshSceneProxy>(
            world_transform(), world_bounds_, visible(), static_mesh_->render_data(),
            std::move(material_render_proxies), owner().actor_id(), component_id(), cast_shadows(), receives_shadows());
    }
} // namespace toy3d
