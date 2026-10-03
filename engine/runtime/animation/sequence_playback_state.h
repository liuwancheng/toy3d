#pragma once

#include "asset/animation/animation_asset.h"
#include "asset/scene/component_settings.h"

namespace toy3d
{
    // Continuous update interval, distinct from a discontinuous seek. Loops count includes
    // wrapping a previously sought end frame when positive playback resumes.
    struct SequencePlaybackInterval
    {
        double previous_time = 0.0;
        double current_time = 0.0;
        double advanced_time = 0.0;
        std::uint64_t loops_crossed = 0;
        bool seeked = false;
    };

    class SequencePlaybackState
    {
      public:
        AssetStatus initialize(double duration, const AnimationPlaybackSettings& settings);
        AssetStatus set_settings(const AnimationPlaybackSettings& settings);
        AssetStatus seek(double time);
        AssetStatus advance(double delta_time);
        void play();
        void pause();
        double time() const;
        double remaining_time() const;
        bool playing() const;
        const AnimationPlaybackSettings& settings() const;
        const SequencePlaybackInterval& interval() const;

      private:
        AnimationPlaybackSettings settings_;
        SequencePlaybackInterval interval_;
        double duration_ = 0.0;
        double time_ = 0.0;
        bool playing_ = false;
    };
} // namespace toy3d
