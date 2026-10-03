#include "gamescene/component/skeletal_mesh_component.h"

#include <cmath>
#include <utility>

#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "rendercore/scene/skeletal_mesh_scene_proxy.h"
#include "rendercore/scene_interface.h"

namespace toy3d
{
    const std::vector<std::string>& SkeletalMeshComponent::material_slot_names() const
    {
        static const std::vector<std::string> empty;
        return mesh_ ? mesh_->asset().data.material_slots : empty;
    }
    MaterialInterfaceRef SkeletalMeshComponent::default_material_for_slot(std::uint32_t slot) const
    {
        return mesh_ && slot < mesh_->material_slots().size() ? mesh_->material_slots()[slot] : nullptr;
    }
    shader::VertexFactoryType SkeletalMeshComponent::vertex_factory_type() const
    {
        return shader::VertexFactoryType::GPUSkin;
    }
    bool SkeletalMeshComponent::has_material_override(std::uint32_t slot) const
    {
        return slot < material_overrides_.size() && material_overrides_[slot] != nullptr;
    }
    bool SkeletalMeshComponent::supports_shadow_casting() const
    {
        if (!mesh_)
        {
            return true;
        }
        for (std::uint32_t slot = 0; slot < mesh_->material_slots().size(); ++slot)
        {
            std::string error;
            if (!validate_material_mesh_pass(material_for_slot(slot)->desc(), shader::ShaderPassRole::ShadowDepth,
                                             shader::VertexFactoryType::GPUSkin, error))
            {
                TOY_LOG_ERROR("Material slot {} rejected: {}", slot, error);
                return false;
            }
        }
        return true;
    }

    SkeletalMeshComponent::SkeletalMeshComponent(Actor& owner) : MeshComponent(owner)
    {
        set_tick_enabled(true);
    }

    bool SkeletalMeshComponent::tick_component(const WorldTickContext& context)
    {
        const auto status = evaluate_animation(context.delta_seconds);
        if (!status.succeeded())
        {
            TOY_LOG_ERROR("SkeletalMesh animation evaluation failed: {}", status.message);
        }
        return status.succeeded();
    }

    AssetStatus SkeletalMeshComponent::set_assets(SkeletalMeshRef mesh,
                                                  std::shared_ptr<const AnimationSequence> sequence)
    {
        if (!mesh)
        {
            if (sequence)
            {
                return AssetStatus{AssetErrorCode::Value, {}, {}, {}, {}, "An animation requires a skeletal mesh.", {}};
            }
            destroy_render_state();
            mesh_.reset();
            sequence_.reset();
            animation_ = {};
            deformer_ = {};
            evaluation_.reset();
            deformation_.reset();
            material_overrides_.clear();
            update_bounds();
            world().mark_content_changed();
            return AssetStatus::success();
        }
        if (cast_shadows())
        {
            for (const auto& material : mesh->material_slots())
            {
                std::string error;
                if (!validate_material_mesh_pass(material->desc(), shader::ShaderPassRole::ShadowDepth,
                                                 shader::VertexFactoryType::GPUSkin, error))
                {
                    return AssetStatus{AssetErrorCode::Value, {}, {}, {}, {}, error, {}};
                }
            }
        }
        AnimationInstance animation;
        std::vector<AnimationSequenceInput> inputs;
        if (sequence)
        {
            inputs.push_back({sequence, playback_settings_});
        }
        auto status = animation.set_sources(mesh->bone_layout(), inputs);
        if (!status.succeeded())
        {
            return status;
        }
        status = animation.update({0, sequence ? std::vector<double>{1} : std::vector<double>{}, lock_root_});
        if (!status.succeeded())
        {
            return status;
        }
        SkeletalMeshDeformer deformer;
        status = deformer.set_mesh(mesh);
        if (!status.succeeded())
        {
            return status;
        }
        const auto evaluated = animation.evaluate();
        if (!evaluated.succeeded())
        {
            return evaluated.status();
        }
        auto deformed = deformer.evaluate(*evaluated.value());
        if (!deformed.succeeded())
        {
            return deformed.status();
        }
        // Complete CPU candidates before retiring the previous registered proxy.
        destroy_render_state();
        mesh_ = std::move(mesh);
        sequence_ = std::move(sequence);
        animation_ = std::move(animation);
        deformer_ = std::move(deformer);
        evaluation_ = evaluated.value();
        deformation_ = std::make_shared<const SkeletalMeshDeformationData>(std::move(deformed).value());
        material_overrides_.assign(mesh_->material_slots().size(), nullptr);
        update_bounds();
        world().mark_content_changed();
        if (is_registered())
        {
            create_render_state();
        }
        return AssetStatus::success();
    }

    AssetStatus SkeletalMeshComponent::set_animation(std::shared_ptr<const AnimationSequence> sequence)
    {
        if (!mesh_)
        {
            return AssetStatus{AssetErrorCode::Value, {}, {}, {}, {}, "Animation requires a bound mesh.", {}};
        }
        auto candidate = animation_;
        std::vector<AnimationSequenceInput> inputs;
        if (sequence)
        {
            inputs.push_back({sequence, playback_settings_});
        }
        auto status = candidate.set_sources(mesh_->bone_layout(), inputs);
        if (status.succeeded())
        {
            status = candidate.update({0, sequence ? std::vector<double>{1} : std::vector<double>{}, lock_root_});
        }
        if (status.succeeded())
        {
            status = commit_animation(std::move(candidate));
        }
        if (status.succeeded())
        {
            sequence_ = std::move(sequence);
            world().mark_content_changed();
        }
        return status;
    }

    AssetStatus SkeletalMeshComponent::set_playback_settings(const AnimationPlaybackSettings& settings, bool lock_root)
    {
        SequencePlaybackState validation;
        auto status = validation.initialize(sequence_ ? sequence_->duration() : 0, settings);
        if (!status.succeeded())
        {
            return status;
        }
        if (mesh_)
        {
            auto candidate = animation_;
            if (sequence_)
            {
                status = candidate.set_playback_settings(0, settings);
            }
            if (status.succeeded())
            {
                status = candidate.update({0, sequence_ ? std::vector<double>{1} : std::vector<double>{}, lock_root});
            }
            if (status.succeeded())
            {
                status = commit_animation(std::move(candidate));
            }
            if (!status.succeeded())
            {
                return status;
            }
        }
        playback_settings_ = settings;
        lock_root_ = lock_root;
        world().mark_content_changed();
        return AssetStatus::success();
    }

    AssetStatus SkeletalMeshComponent::seek(double time)
    {
        auto candidate = animation_;
        const auto status = candidate.seek(0, time);
        return status.succeeded() ? commit_animation(std::move(candidate)) : status;
    }

    AssetStatus SkeletalMeshComponent::set_playing(bool playing)
    {
        return animation_.set_playing(0, playing);
    }

    AssetStatus SkeletalMeshComponent::evaluate_animation(double delta_seconds)
    {
        if (!std::isfinite(delta_seconds) || delta_seconds < 0)
        {
            return AssetStatus{
                AssetErrorCode::Value, {}, {}, {}, {}, "Animation delta must be finite and nonnegative.", {}};
        }
        if (!mesh_ || !sequence_ || !animation_.playback_state(0)->playing() || delta_seconds == 0)
        {
            return AssetStatus::success();
        }
        // Copy only per-instance clocks/weights; immutable assets and published outputs are shared.
        auto candidate = animation_;
        const auto status =
            candidate.update({delta_seconds, sequence_ ? std::vector<double>{1} : std::vector<double>{}, lock_root_});
        return status.succeeded() ? commit_animation(std::move(candidate)) : status;
    }

    AssetStatus SkeletalMeshComponent::commit_animation(AnimationInstance candidate)
    {
        const auto evaluated = candidate.evaluate();
        if (!evaluated.succeeded())
        {
            return evaluated.status();
        }
        auto deformed = deformer_.evaluate(*evaluated.value());
        if (!deformed.succeeded())
        {
            return deformed.status();
        }
        animation_ = std::move(candidate);
        evaluation_ = evaluated.value();
        deformation_ = std::make_shared<const SkeletalMeshDeformationData>(std::move(deformed).value());
        update_bounds();
        publish_pose();
        return AssetStatus::success();
    }

    void SkeletalMeshComponent::publish_pose()
    {
        auto* scene = world().scene_interface();
        if (scene && scene_proxy_identity())
        {
            scene->update_skeletal_mesh_pose(scene_proxy_identity(), deformation_, world_transform(), world_bounds_,
                                             visible(), cast_shadows(), receives_shadows());
            world().mark_scene_changed();
        }
    }

    const std::shared_ptr<const AnimationSequence>& SkeletalMeshComponent::animation_sequence() const
    {
        return sequence_;
    }
    const AnimationPlaybackSettings& SkeletalMeshComponent::playback_settings() const
    {
        return playback_settings_;
    }
    bool SkeletalMeshComponent::lock_root() const
    {
        return lock_root_;
    }
    const SkeletalMeshRef& SkeletalMeshComponent::skeletal_mesh() const
    {
        return mesh_;
    }

    const SequencePlaybackState* SkeletalMeshComponent::playback_state() const
    {
        return animation_.playback_state(0);
    }

    const std::shared_ptr<const AnimationEvaluation>& SkeletalMeshComponent::animation_evaluation() const
    {
        return evaluation_;
    }

    const std::shared_ptr<const SkeletalMeshDeformationData>& SkeletalMeshComponent::deformation() const
    {
        return deformation_;
    }

    MaterialInterfaceRef SkeletalMeshComponent::material_for_slot(std::uint32_t slot) const
    {
        if (!mesh_ || slot >= material_overrides_.size())
        {
            return nullptr;
        }
        return material_overrides_[slot] ? material_overrides_[slot] : mesh_->material_slots()[slot];
    }

    bool SkeletalMeshComponent::set_material_override(std::uint32_t slot, MaterialInterfaceRef material)
    {
        if (!mesh_ || slot >= material_overrides_.size() || !material)
        {
            return false;
        }
        std::string error;
        if (!validate_material_geometry(material->desc(), shader::VertexFactoryType::GPUSkin, true,
                                        mesh_->asset().geometry.mesh.valid_tangent_frame, error))
        {
            TOY_LOG_ERROR("Material slot {} rejected: {}", slot, error);
            return false;
        }
        if (cast_shadows() && !validate_material_mesh_pass(material->desc(), shader::ShaderPassRole::ShadowDepth,
                                                           shader::VertexFactoryType::GPUSkin, error))
        {
            TOY_LOG_ERROR("Material slot {} rejected: {}", slot, error);
            return false;
        }
        auto previous = material_overrides_;
        material_overrides_[slot] = std::move(material);
        send_material_overrides(std::move(previous));
        world().mark_content_changed();
        return true;
    }

    bool SkeletalMeshComponent::clear_material_override(std::uint32_t slot)
    {
        if (!mesh_ || slot >= material_overrides_.size())
        {
            return false;
        }
        if (!material_overrides_[slot])
        {
            return true;
        }
        std::string error;
        if (!validate_material_geometry(mesh_->material_slots()[slot]->desc(), shader::VertexFactoryType::GPUSkin, true,
                                        mesh_->asset().geometry.mesh.valid_tangent_frame, error))
        {
            TOY_LOG_ERROR("Material slot {} rejected: {}", slot, error);
            return false;
        }
        if (cast_shadows() &&
            !validate_material_mesh_pass(mesh_->material_slots()[slot]->desc(), shader::ShaderPassRole::ShadowDepth,
                                         shader::VertexFactoryType::GPUSkin, error))
        {
            TOY_LOG_ERROR("Material slot {} rejected: {}", slot, error);
            return false;
        }
        auto previous = material_overrides_;
        material_overrides_[slot].reset();
        send_material_overrides(std::move(previous));
        world().mark_content_changed();
        return true;
    }

    void SkeletalMeshComponent::send_material_overrides(std::vector<MaterialInterfaceRef> previous)
    {
        if (!has_render_state())
        {
            return;
        }
        std::vector<MaterialRenderProxy*> materials;
        for (std::uint32_t slot = 0; slot < material_overrides_.size(); ++slot)
        {
            materials.push_back(material_for_slot(slot)->material_render_proxy());
        }
        send_render_materials(std::move(materials));
        enqueue_render_command("ReleaseUpdatedSkeletalMeshMaterials",
                               [previous = std::move(previous)]() noexcept
                               {
                               });
    }

    void SkeletalMeshComponent::on_render_state_removed()
    {
        enqueue_render_command("ReleaseRemovedSkeletalMeshMaterials",
                               [materials = material_overrides_]() noexcept
                               {
                               });
    }

    void SkeletalMeshComponent::update_bounds()
    {
        world_bounds_ = {};
        if (!deformation_ || !deformation_->has_mesh_bounds)
        {
            return;
        }
        const auto center = (deformation_->bounds_minimum + deformation_->bounds_maximum) * 0.5f;
        const auto extent = (deformation_->bounds_maximum - deformation_->bounds_minimum) * 0.5f;
        const auto& transform = world_transform();
        const auto world_center = transform_position(transform, center);
        Vector3 world_extent;
        for (std::size_t row = 0; row < 3; ++row)
        {
            world_extent.data()[row] = std::abs(transform.at(0, row)) * extent.x +
                                       std::abs(transform.at(1, row)) * extent.y +
                                       std::abs(transform.at(2, row)) * extent.z;
        }
        world_bounds_.minimum =
            vec3(world_center.x - world_extent.x, world_center.y - world_extent.y, world_center.z - world_extent.z);
        world_bounds_.maximum =
            vec3(world_center.x + world_extent.x, world_center.y + world_extent.y, world_center.z + world_extent.z);
    }

    std::unique_ptr<PrimitiveSceneProxy> SkeletalMeshComponent::create_scene_proxy() const
    {
        if (!mesh_ || !deformation_)
        {
            return nullptr;
        }
        std::vector<MaterialRenderProxy*> materials;
        for (std::uint32_t slot = 0; slot < mesh_->material_slots().size(); ++slot)
        {
            materials.push_back(material_for_slot(slot)->material_render_proxy());
        }
        return std::make_unique<SkeletalMeshSceneProxy>(world_transform(), world_bounds_, visible(), mesh_,
                                                        deformation_, std::move(materials), owner().actor_id(),
                                                        component_id(), cast_shadows(), receives_shadows());
    }
} // namespace toy3d
