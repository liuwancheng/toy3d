#pragma once

#include "animation/animation_instance.h"

namespace toy3d
{
    // Convenience single-sequence facade; general blending lives in AnimationInstance.
    class AnimationPlayer
    {
      public:
        AssetStatus set_assets(const AssetId& skeleton_id, std::shared_ptr<const SkeletonAssetData> skeleton,
                               std::shared_ptr<const AnimationSequenceAsset> sequence);
        AssetStatus set_settings(const AnimationPlaybackSettings& settings);
        AssetStatus seek(double time);
        AssetStatus advance(double delta_time);
        AssetResult<std::shared_ptr<const AnimationEvaluation>> evaluate() const;
        void play();
        void pause();
        double time() const;
        bool playing() const;
        const AnimationPlaybackSettings& settings() const;

      private:
        AnimationInstance instance_;
        AnimationPlaybackSettings settings_;
        bool has_sequence_ = false;
    };
} // namespace toy3d
