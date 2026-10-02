#include "animation_player.h"

#include <cmath>

namespace toy3d
{
    // --------------------------------------------------------------------------
    // AnimationPlayer: single sequence facade over the common instance pipeline
    // --------------------------------------------------------------------------
    AssetStatus AnimationPlayer::set_assets(const AssetId& skeleton_id,
                                            std::shared_ptr<const SkeletonAssetData> skeleton,
                                            std::shared_ptr<const AnimationSequenceAsset> sequence)
    {
        if (!skeleton)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, "animation player skeleton is missing", {}};
        }
        auto layout = std::make_shared<const AnimationBoneLayout>(skeleton_id, *skeleton);
        std::vector<AnimationSequenceInput> sources;
        if (sequence)
        {
            sources.push_back({std::make_shared<const AnimationSequence>(layout, *sequence), settings_});
        }
        const auto valid = instance_.set_sources(std::move(layout), sources);
        if (valid.succeeded())
        {
            has_sequence_ = !sources.empty();
        }
        return valid;
    }

    AssetStatus AnimationPlayer::set_settings(const AnimationPlaybackSettings& settings)
    {
        if (!std::isfinite(settings.rate) || settings.rate <= 0.0)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, "animation playback rate must be finite and positive", {}};
        }
        if (has_sequence_)
        {
            const auto valid = instance_.set_playback_settings(0, settings);
            if (!valid.succeeded())
            {
                return valid;
            }
        }
        settings_ = settings;
        return AssetStatus::success();
    }

    AssetStatus AnimationPlayer::seek(double time)
    {
        return instance_.seek(0, time);
    }

    AssetStatus AnimationPlayer::advance(double delta_time)
    {
        return instance_.update({delta_time, has_sequence_ ? std::vector<double>{1.0} : std::vector<double>{}, false});
    }

    AssetResult<std::shared_ptr<const AnimationEvaluation>> AnimationPlayer::evaluate() const
    {
        return instance_.evaluate();
    }

    void AnimationPlayer::play()
    {
        if (has_sequence_)
        {
            instance_.set_playing(0, true);
        }
    }

    void AnimationPlayer::pause()
    {
        if (has_sequence_)
        {
            instance_.set_playing(0, false);
        }
    }

    double AnimationPlayer::time() const
    {
        const auto* state = instance_.playback_state(0);
        return state ? state->time() : 0.0;
    }

    bool AnimationPlayer::playing() const
    {
        const auto* state = instance_.playback_state(0);
        return state && state->playing();
    }

    const AnimationPlaybackSettings& AnimationPlayer::settings() const
    {
        return settings_;
    }
} // namespace toy3d
