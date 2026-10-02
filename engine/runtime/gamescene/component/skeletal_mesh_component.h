#pragma once

#include "animation/animation_instance.h"
#include "gamescene/component/primitive_component.h"
#include "rendercore/geometry/skeletal_mesh.h"
#include "rendercore/geometry/skeletal_mesh_deformation.h"

namespace toy3d
{
    class SkeletalMeshComponent final : public PrimitiveComponent
    {
      public:
        explicit SkeletalMeshComponent(Actor& owner);
        ~SkeletalMeshComponent() override = default;
        AssetStatus set_assets(SkeletalMeshRef mesh, std::shared_ptr<const AnimationSequence> sequence = {});
        AssetStatus set_animation(std::shared_ptr<const AnimationSequence> sequence);
        AssetStatus set_playback_settings(const AnimationPlaybackSettings& settings, bool lock_root = false);
        AssetStatus seek(double time);
        AssetStatus set_playing(bool playing);
        // Shared by this component's tick and explicit editor preview evaluation.
        AssetStatus evaluate_animation(double delta_seconds);
        const SkeletalMeshRef& skeletal_mesh() const;
        const SequencePlaybackState* playback_state() const;
        const std::shared_ptr<const AnimationEvaluation>& animation_evaluation() const;
        const std::shared_ptr<const SkeletalMeshDeformationData>& deformation() const;
        bool set_material_override(std::uint32_t slot, MaterialInterfaceRef material);
        bool clear_material_override(std::uint32_t slot);
        MaterialInterfaceRef material_for_slot(std::uint32_t slot) const;

      private:
        bool tick_component(const WorldTickContext& context) override;
        AssetStatus commit_animation(AnimationInstance candidate);
        void publish_pose();
        void update_bounds() override;
        void on_render_state_removed() override;
        std::unique_ptr<PrimitiveSceneProxy> create_scene_proxy() const override;
        void send_material_overrides(std::vector<MaterialInterfaceRef> previous);

        SkeletalMeshRef mesh_;
        std::shared_ptr<const AnimationSequence> sequence_;
        AnimationInstance animation_;
        SkeletalMeshDeformer deformer_;
        AnimationPlaybackSettings playback_settings_;
        bool lock_root_ = false;
        std::shared_ptr<const AnimationEvaluation> evaluation_;
        std::shared_ptr<const SkeletalMeshDeformationData> deformation_;
        std::vector<MaterialInterfaceRef> material_overrides_;
    };
} // namespace toy3d
