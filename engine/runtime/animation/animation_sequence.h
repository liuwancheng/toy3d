#pragma once

#include "animation/animation_pose.h"

namespace toy3d
{
    // Validated immutable sample snapshot, shared by nodes/instances with separate clocks.
    class AnimationSequence
    {
      public:
        AnimationSequence(std::shared_ptr<const AnimationBoneLayout> bone_layout, const AnimationSequenceAsset& asset);
        AnimationSequence(const AnimationSequence&) = default;
        AnimationSequence& operator=(const AnimationSequence&) = delete;
        const AssetStatus& status() const;
        const std::shared_ptr<const AnimationBoneLayout>& bone_layout() const;
        double duration() const;
        AssetResult<AnimationPose> sample(double time) const;

      private:
        std::shared_ptr<const AnimationBoneLayout> bone_layout_;
        AnimationSequenceAsset asset_;
        AssetStatus status_;
    };
} // namespace toy3d
