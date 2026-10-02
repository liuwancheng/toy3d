#pragma once

#include "animation/animation_sequence.h"
#include "animation/sequence_playback_state.h"

namespace toy3d
{
    struct AnimationSequenceInput
    {
        std::shared_ptr<const AnimationSequence> sequence;
        AnimationPlaybackSettings playback;
    };

    struct AnimationUpdateInput
    {
        double delta_time = 0.0;
        std::vector<double> weights;
        bool lock_root = false;
    };

    struct AnimationEvaluation
    {
        std::uint64_t revision = 0;
        AnimationPose local_pose;
        ComponentSpacePose component_pose;
    };

    // Single calling-thread owner. Update mutates clocks; Evaluate publishes immutable
    // owned results without advancing time. No Actor, Component or render ownership.
    class AnimationInstance
    {
      public:
        AssetStatus set_sources(std::shared_ptr<const AnimationBoneLayout> bone_layout,
                                const std::vector<AnimationSequenceInput>& inputs);
        AssetStatus update(const AnimationUpdateInput& input);
        AssetStatus seek(std::size_t source, double time);
        AssetStatus set_playback_settings(std::size_t source, const AnimationPlaybackSettings& settings);
        AssetStatus set_playing(std::size_t source, bool playing);
        AssetResult<std::shared_ptr<const AnimationEvaluation>> evaluate() const;
        // Borrowed until the next mutation of this instance.
        const SequencePlaybackState* playback_state(std::size_t source) const;
        const std::shared_ptr<const AnimationBoneLayout>& bone_layout() const;

      private:
        AssetStatus invalidate();
        std::shared_ptr<const AnimationBoneLayout> bone_layout_;
        std::vector<std::shared_ptr<const AnimationSequence>> sequences_;
        std::vector<SequencePlaybackState> playback_states_;
        std::vector<double> weights_;
        bool lock_root_ = false;
        std::uint64_t revision_ = 0;
        mutable std::shared_ptr<const AnimationEvaluation> cached_evaluation_;
    };
} // namespace toy3d
