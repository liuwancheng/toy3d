#include "animation_sequence.h"

#include "animation_sampling.h"

namespace toy3d
{
    // --------------------------------------------------------------------------
    // AnimationSequence: immutable validated samples in a shared bone domain
    // --------------------------------------------------------------------------
    AnimationSequence::AnimationSequence(std::shared_ptr<const AnimationBoneLayout> bone_layout,
                                         const AnimationSequenceAsset& asset)
        : bone_layout_(std::move(bone_layout)), asset_(asset)
    {
        if (!bone_layout_ || !bone_layout_->status().succeeded())
        {
            status_ = {AssetErrorCode::Value, {}, {}, {}, {}, "animation sequence requires a valid bone layout", {}};
            return;
        }
        status_ = validate_animation_compatibility(asset_, bone_layout_->skeleton_id(), bone_layout_->skeleton());
    }

    const AssetStatus& AnimationSequence::status() const
    {
        return status_;
    }

    const std::shared_ptr<const AnimationBoneLayout>& AnimationSequence::bone_layout() const
    {
        return bone_layout_;
    }

    double AnimationSequence::duration() const
    {
        return asset_.data.duration;
    }

    AssetResult<AnimationPose> AnimationSequence::sample(double time) const
    {
        if (!status_.succeeded())
        {
            return AssetResult<AnimationPose>(status_);
        }
        const auto sampled = animation_detail::sample_validated_pose(bone_layout_->skeleton(), asset_, time);
        if (!sampled.succeeded())
        {
            return AssetResult<AnimationPose>(sampled.status());
        }
        return AssetResult<AnimationPose>(AnimationPose{bone_layout_, sampled.value()});
    }
} // namespace toy3d
