#pragma once

#include "asset/animation/animation_asset.h"

#include <memory>

namespace toy3d
{
    // Immutable ordered bone domain. Construction validates a deep copy once.
    class AnimationBoneLayout
    {
      public:
        AnimationBoneLayout(const AssetId& skeleton_id, const SkeletonAssetData& skeleton);
        AnimationBoneLayout(const AnimationBoneLayout&) = default;
        AnimationBoneLayout& operator=(const AnimationBoneLayout&) = delete;
        const AssetStatus& status() const;
        const AssetId& skeleton_id() const;
        const std::string& reference_hash() const;
        const SkeletonAssetData& skeleton() const;
        bool compatible(const AnimationBoneLayout& other) const;

      private:
        AssetId skeleton_id_;
        SkeletonAssetData skeleton_;
        std::string reference_hash_;
        AssetStatus status_;
    };

    struct AnimationPose
    {
        std::shared_ptr<const AnimationBoneLayout> bone_layout;
        std::vector<Transform> local_transforms;
    };

    struct ComponentSpacePose
    {
        std::shared_ptr<const AnimationBoneLayout> bone_layout;
        std::vector<Matrix4> bone_matrices;
    };

    // Views are borrowed only for this synchronous blend call.
    struct WeightedAnimationPose
    {
        const AnimationPose* pose = nullptr;
        double weight = 0.0;
    };

    AssetStatus validate_animation_pose(const AnimationPose& pose);
    AssetResult<AnimationPose> make_reference_pose(std::shared_ptr<const AnimationBoneLayout> bone_layout);
    AssetStatus reset_to_reference_pose(AnimationPose& pose);
    AssetResult<AnimationPose> sample_animation_pose(std::shared_ptr<const AnimationBoneLayout> bone_layout,
                                                     const AnimationSequenceAsset& sequence, double time);
    AssetResult<AnimationPose> blend_animation_poses(const AnimationPose& first, const AnimationPose& second,
                                                     double alpha);
    AssetResult<AnimationPose> blend_animation_poses(std::shared_ptr<const AnimationBoneLayout> bone_layout,
                                                     const std::vector<WeightedAnimationPose>& inputs);
    // Explicit post-blend preview policy; hierarchy accumulation never locks the root.
    AssetStatus lock_animation_root(AnimationPose& pose);
    AssetResult<ComponentSpacePose> build_component_space_pose(const AnimationPose& pose);
} // namespace toy3d
